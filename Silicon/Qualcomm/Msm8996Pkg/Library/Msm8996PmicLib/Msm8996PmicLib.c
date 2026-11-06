/** @file
 *
 *  PM8994 regulator / GPIO control via the SPMI arbiter.
 *
 *  Voltage range tables are lifted from Linux mainline
 *  drivers/regulator/qcom_spmi-regulator.c (GPL-2.0, (c) 2014 Sony Mobile
 *  Communications Inc.); the set-point math mirrors
 *  spmi_regulator_common_set_voltage: the RANGE register selects a range,
 *  the SET register is the index inside that range.
 *
 *  Copyright (c) 2026, UniXNU contributors.
 *
 *  SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 **/

#include <Uefi.h>
#include <Library/BaseLib.h>
#include <Library/DebugLib.h>

#include <Library/Msm8996SpmiLib.h>
#include <Library/Msm8996PmicLib.h>
#include <Msm8996Regs.h>

//
// Voltage range tables (mainline qcom_spmi-regulator.c). Each entry:
// range selector, min_uV, min_sel_uV, max_uV, step_uV.
//
typedef struct {
  UINT8   Sel;
  UINT32  MinUv;
  UINT32  MinSelUv;
  UINT32  MaxUv;
  UINT32  StepUv;
} PMIC_VOLTAGE_RANGE;

// nldo1: single range (750000..1537500 by 12500), sel 2.
STATIC CONST PMIC_VOLTAGE_RANGE mNldo1[] = {
  { 2, 750000, 750000, 1537500, 12500 },
};

// nldo3: range 0 covers 375000..1537500 by 12500 (usable spread).
STATIC CONST PMIC_VOLTAGE_RANGE mNldo3[] = {
  { 0, 375000, 375000, 1537500, 12500 },
};

// pldo: range 2 covers 1750000..4900000 by 50000.
STATIC CONST PMIC_VOLTAGE_RANGE mPldo[] = {
  { 2, 1750000, 1750000, 4900000, 50000 },
};

// smps: range 1 covers 1575000..3125000 by 25000.
STATIC CONST PMIC_VOLTAGE_RANGE mSmps[] = {
  { 1, 1575000, 1575000, 3125000, 25000 },
};

typedef enum {
  RailClassNldo1,
  RailClassNldo3,
  RailClassPldo,
  RailClassSmps,
} PMIC_RAIL_CLASS;

typedef struct {
  UINT16  Periph;
  UINT32  Uv;
} PMIC_RAIL_CONFIG;

STATIC CONST PMIC_RAIL_CONFIG mRails[PmicRailCount] = {
  { PM8994_L28_PERIPH, 925000 },   // ufs vdda-phy
  { PM8994_L12_PERIPH, 1800000 },  // ufs vdda-pll
  { PM8994_L25_PERIPH, 1200000 },  // ufs vccq
  { PM8994_S4_PERIPH, 1800000 },   // ufs vccq2
  { PM8994_L2_PERIPH, 1250000 },   // dsi vdda
  { PM8994_L14_PERIPH, 1800000 },  // dsi vddio
};

/**
  Map a runtime-read (type, subtype) pair to a range table, exactly as the
  mainline supported_regulators[] mapping does for PM8994 (dig major rev >= 1).

  NOTE: subtype 0x08 is ambiguous on purpose -- P50 (LDO) and GP_CTL (SMPS)
  share it; the TYPE register disambiguates (LDO = 0x04, BUCK/SMPS = 0x03).
**/
STATIC
EFI_STATUS
RailClassFromSubtype (
  IN  UINT8           Type,
  IN  UINT8           Subtype,
  OUT PMIC_RAIL_CLASS *Class
  )
{
  switch (Subtype) {
    case PMIC_SUBTYPE_N300:
      *Class = RailClassNldo1;
      return EFI_SUCCESS;

    case PMIC_SUBTYPE_N600:
    case PMIC_SUBTYPE_N1200:
    case PMIC_SUBTYPE_N600_ST:
    case PMIC_SUBTYPE_N1200_ST:
      *Class = RailClassNldo3;
      return EFI_SUCCESS;

    // (P50 intentionally absent: subtype 0x08 is shared with SMPS GP_CTL
    //  and is handled by the TYPE-disambiguated case below.)
    case PMIC_SUBTYPE_P150:
    case PMIC_SUBTYPE_P300:
    case PMIC_SUBTYPE_P600:
    case PMIC_SUBTYPE_P1200:
    case PMIC_SUBTYPE_LV_P50:
    case PMIC_SUBTYPE_LV_P150:
    case PMIC_SUBTYPE_LV_P300:
    case PMIC_SUBTYPE_LV_P600:
    case PMIC_SUBTYPE_LV_P1200:
      *Class = RailClassPldo;
      return EFI_SUCCESS;

    case 0x08:                                  // P50 (LDO) vs GP_CTL (SMPS)
      if (Type == 0x04) {                       // SPMI_REGULATOR_TYPE_LDO
        *Class = RailClassPldo;
        return EFI_SUCCESS;
      } else if (Type == 0x03) {                // SPMI_REGULATOR_TYPE_BUCK
        *Class = RailClassSmps;
        return EFI_SUCCESS;
      }

      return EFI_UNSUPPORTED;

    default:
      return EFI_UNSUPPORTED;
  }
}

STATIC
CONST PMIC_VOLTAGE_RANGE *
RailRangeTable (
  IN PMIC_RAIL_CLASS Class
  )
{
  switch (Class) {
    case RailClassNldo1: return mNldo1;
    case RailClassNldo3: return mNldo3;
    case RailClassPldo:  return mPldo;
    case RailClassSmps:  return mSmps;
  }

  return NULL;
}

/**
  Enable a regulator rail at its DTB-declared voltage.
**/
EFI_STATUS
EFIAPI
Msm8996PmicEnableRail (
  IN MSM8996_PMIC_RAIL  Rail,
  IN UINT32             Uv
  )
{
  EFI_STATUS            Status;
  CONST PMIC_RAIL_CONFIG *Cfg;
  UINT8                 Type;
  UINT8                 Subtype;
  PMIC_RAIL_CLASS       Class;
  CONST PMIC_VOLTAGE_RANGE *Table;
  CONST PMIC_VOLTAGE_RANGE *Range;
  UINT8                 En;
  UINT8                 Buf[2];
  UINT8                 Sel;

  if ((UINT32)Rail >= PmicRailCount) {
    return EFI_INVALID_PARAMETER;
  }

  Cfg = &mRails[Rail];

  //
  // Hot hand-off: rail already enabled -> do not disturb the RPM's setup.
  //
  Status = Msm8996SpmiRead (PM8994_SID, Cfg->Periph + PMIC_REG_ENABLE, &En, 1);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "%a: rail %u read failed %r\n", gEfiCallerBaseName, Rail, Status));
    return Status;
  }

  if ((En & PMIC_ENABLE_BIT) != 0) {
    return EFI_SUCCESS;
  }

  //
  // Read the runtime subtype and derive the voltage range table.
  //
  Status = Msm8996SpmiRead (PM8994_SID, Cfg->Periph + PMIC_REG_TYPE, &Type, 1);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = Msm8996SpmiRead (PM8994_SID, Cfg->Periph + PMIC_REG_SUBTYPE, &Subtype, 1);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = RailClassFromSubtype (Type, Subtype, &Class);
  if (EFI_ERROR (Status)) {
    DEBUG ((
      DEBUG_ERROR,
      "%a: rail %u periph 0x%x unsupported subtype 0x%x (type 0x%x)\n",
      gEfiCallerBaseName, Rail, Cfg->Periph, Subtype, Type
      ));
    return Status;
  }

  Table = RailRangeTable (Class);
  Range = NULL;
  for (Sel = 0; Sel < 4; Sel++) {
    CONST PMIC_VOLTAGE_RANGE *R = &Table[Sel];

    if ((R->StepUv == 0) || (Uv < R->MinSelUv) || (Uv > R->MaxUv)) {
      continue;
    }

    Range = R;
    break;
  }

  if (Range == NULL) {
    return EFI_UNSUPPORTED;
  }

  Buf[0] = Range->Sel;
  Buf[1] = (UINT8)((Uv - Range->MinSelUv) / Range->StepUv);

  Status = Msm8996SpmiWrite (PM8994_SID, Cfg->Periph + PMIC_REG_VOLTAGE_RANGE, Buf, 2);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  //
  // HPM (high power mode) then master-enable.
  //
  Status = Msm8996SpmiUpdateBits (
             PM8994_SID, Cfg->Periph + PMIC_REG_MODE,
             PMIC_MODE_HPM, PMIC_MODE_HPM
             );
  if (EFI_ERROR (Status)) {
    return Status;
  }

  return Msm8996SpmiUpdateBits (
           PM8994_SID, Cfg->Periph + PMIC_REG_ENABLE,
           PMIC_ENABLE_BIT, PMIC_ENABLE_BIT
           );
}

/**
  Enable the PMIC GPIO that gates the panel backlight power.
**/
EFI_STATUS
EFIAPI
Msm8996PmicBacklightGpioOn (
  VOID
  )
{
  EFI_STATUS  Status;
  UINT16      Base;
  UINT8       Val;

  //
  // pm8994 gpio 14: peripheral base 0xC000 + 0x100 * (14 - 1).
  // Configure: function = digital output, pull disabled, CMOS buffer,
  // then master-enable.
  //
  Base = PM8994_GPIO_BASE + 0x100 * (PM8994_BKLIGHT_GPIO - 1);

  Val = PMIC_GPIO_MODE_DIGITAL_OUTPUT;
  Status = Msm8996SpmiWrite (PM8994_SID, Base + PMIC_GPIO_REG_MODE_CTL, &Val, 1);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Val = PMIC_GPIO_PULL_DISABLE;
  Status = Msm8996SpmiWrite (PM8994_SID, Base + PMIC_GPIO_REG_DIG_PULL_CTL, &Val, 1);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  // OUT_CTL: strength 2 (bits 1:0 = 2), CMOS push-pull (bits 5:4 = 0).
  Val = 0x02;
  Status = Msm8996SpmiWrite (PM8994_SID, Base + PMIC_GPIO_REG_DIG_OUT_CTL, &Val, 1);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  return Msm8996SpmiUpdateBits (
           PM8994_SID, Base + PMIC_GPIO_REG_EN_CTL,
           PMIC_GPIO_MASTER_EN, PMIC_GPIO_MASTER_EN
           );
}

/*
  ---------------------------------------------------------------------------
  PM8994 WLED (panel backlight boost)
  ---------------------------------------------------------------------------
*/

/**
  Read-modify-write an 8-bit PMIC register.

  @param  Addr      16-bit regmap address (PPID = high byte, offset = low).
  @param  KeepMask  Bits preserved from the current value.
  @param  OrValue   Bits forced on.
**/
STATIC
EFI_STATUS
WledRmw (
  IN UINT16  Addr,
  IN UINT8   KeepMask,
  IN UINT8   OrValue
  )
{
  EFI_STATUS  Status;
  UINT8       Val;

  Status = Msm8996SpmiRead (PM8994_SID, Addr, &Val, 1);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Val = (UINT8)((Val & KeepMask) | OrValue);
  return Msm8996SpmiWrite (PM8994_SID, Addr, &Val, 1);
}

STATIC
EFI_STATUS
WledWrite (
  IN UINT16  Addr,
  IN UINT8   Value
  )
{
  return Msm8996SpmiWrite (PM8994_SID, Addr, &Value, 1);
}

/**
  Write 0xA5 to a peripheral's SEC_ACCESS register so the write-protected
  registers (DISP_SEL, TEST5) accept the following write.
**/
STATIC
EFI_STATUS
WledSecUnlock (
  IN UINT16  Base
  )
{
  return WledWrite (Base + WLED_SEC_ACCESS, WLED_SEC_UNLOCK_KEY);
}

/**
  Coarse microsecond delay.  The WLED sequence only needs "at least this
  long" between two SPMI writes (sync latch, sync delay per string), so a
  paused loop is adequate and avoids dragging TimerLib into a BASE library.
**/
STATIC
VOID
WledDelayUs (
  IN UINT32  Us
  )
{
  UINT32  i;

  for (i = 0; i < Us * 40; i++) {
    CpuPause ();
  }
}

/**
  Push a brightness level into all three current sinks and latch it.
**/
STATIC
EFI_STATUS
WledSetLevel (
  IN UINT16  Level
  )
{
  EFI_STATUS  Status;
  UINT8       n;
  UINT16      Str;

  for (n = 1; n <= 3; n++) {
    Str = (UINT16)(PM8994_WLED_SINK_BASE + 0x50 + n * WLED_SINK_STRIDE);

    Status = WledWrite (Str + WLED_SINK_BRIGHT_LSB, (UINT8)(Level & 0xFF));
    if (EFI_ERROR (Status)) {
      return Status;
    }

    Status = WledWrite (Str + WLED_SINK_BRIGHT_MSB, (UINT8)((Level >> 8) & 0x0F));
    if (EFI_ERROR (Status)) {
      return Status;
    }
  }

  Status = WledWrite (PM8994_WLED_SINK_BASE + WLED_SINK_SYNC, WLED_SYNC_LATCH);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  WledDelayUs (1000);

  return WledWrite (PM8994_WLED_SINK_BASE + WLED_SINK_SYNC, WLED_SYNC_RESET);
}

EFI_STATUS
EFIAPI
Msm8996PmicWledInit (
  IN UINT16  Level
  )
{
  EFI_STATUS  Status;
  UINT8       n;
  UINT16      Ctrl;
  UINT16      Sink;
  UINT16      Str;

  Ctrl = PM8994_WLED_CTRL_BASE;
  Sink = PM8994_WLED_SINK_BASE;

  if (Level > WLED_MAX_LEVEL) {
    Level = WLED_MAX_LEVEL;
  }

  //
  // LCD (not AMOLED) -- DISP_SEL bit7 = 0.  SEC_ACCESS first.
  //
  Status = WledSecUnlock (Ctrl);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = WledRmw (Ctrl + WLED_CTRL_DISP_SEL, 0x7F, 0x00);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  //
  // Feedback: automatic (DTB qcom,fdbk-output = "auto" -> 0).
  //
  Status = WledRmw (Ctrl + WLED_CTRL_FDBK_OP, 0xF8, 0x00);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  //
  // VREF: 300 mV + 25 mV * code.  350 mV -> code 2.
  //
  Status = WledRmw (Ctrl + WLED_CTRL_VREF, 0xF0, (350 - 300) / 25);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  //
  // ILIM: 980 mA -> 980 / 280 = 3, with the OVERWRITE bit so the trim
  // default is replaced.
  //
  Status = WledRmw (Ctrl + WLED_CTRL_ILIM, 0xF8, (UINT8)((980 / 280) | 0x80));
  if (EFI_ERROR (Status)) {
    return Status;
  }

  //
  // Soft-start ramp: 2 for LCD, 0 for AMOLED.
  //
  Status = WledWrite (Ctrl + WLED_CTRL_SOFTSTART_RAMP, 2);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  //
  // Max boost duty: 26 mV..156 ns in 52 ns steps; DTB says 26 ns -> code 0.
  //
  Status = WledRmw (Ctrl + WLED_CTRL_BOOST_DUTY, 0xFC, 26 / 52);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  //
  // Switching frequency: DTB says 800 kHz -> code 0x0B (plus OVERWRITE).
  //
  Status = WledRmw (Ctrl + WLED_CTRL_SWITCH_FREQ, 0xF0, (UINT8)(WLED_SWITCH_FREQ_800KHZ_CODE | 0x80));
  if (EFI_ERROR (Status)) {
    return Status;
  }

  //
  // OVP: <= 29.5 V maps to code 1 (LCD only).
  //
  Status = WledRmw (Ctrl + WLED_CTRL_OVP, 0x03, 1);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  //
  // Sink modulator: 9600 kHz modulator (code 1), phase staggering on,
  // matching acc clock, 8-bit dimming resolution, hybrid dimming on.
  // Field order matters -- each step only clears its own bits.
  //
  Status = WledRmw (Sink + WLED_SINK_MOD, 0x3F, (UINT8)(1 << 6));
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = WledRmw (Sink + WLED_SINK_MOD, 0xDF, (UINT8)(1 << 5));
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = WledRmw (Sink + WLED_SINK_MOD, 0xE7, (UINT8)(1 << 3));
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = WledRmw (Sink + WLED_SINK_MOD, 0xFD, 0);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = WledRmw (Sink + WLED_SINK_MOD, 0xFB, (UINT8)(1 << 2));
  if (EFI_ERROR (Status)) {
    return Status;
  }

  //
  // Hybrid dimming threshold: fls(625 / 78) - 1 = 3.
  //
  Status = WledRmw (Sink + WLED_SINK_HYB_THRES, 0xF8, 3);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  //
  // TEST5 selects hybrid dimming mode.
  //
  Status = WledSecUnlock (Sink);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = WledWrite (Sink + WLED_SINK_TEST5, WLED_TEST5_HYBRID);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  //
  // Start with every sink off, then enable strings 1..3 (DTB
  // qcom,led-strings-list = <0x10203>).
  //
  Status = WledWrite (Sink + WLED_SINK_CURR_SINK, 0x00);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  for (n = 1; n <= 3; n++) {
    Str = (UINT16)(Sink + 0x50 + n * WLED_SINK_STRIDE);

    //
    // Modulator on, gate drive off (hybrid dimming).
    //
    Status = WledRmw (Str + WLED_SINK_MOD_EN, 0x7F, 0x80);
    if (EFI_ERROR (Status)) {
      return Status;
    }

    Status = WledRmw (Str + WLED_SINK_MOD_EN, 0xFE, 0x00);
    if (EFI_ERROR (Status)) {
      return Status;
    }

    //
    // Sync delay: 800 us / 200 us = 4.
    //
    Status = WledRmw (Str + WLED_SINK_SYNC_DLY, 0xF8, (UINT8)(800 / 200));
    if (EFI_ERROR (Status)) {
      return Status;
    }

    //
    // Full-scale current: 25000 uA / 2500 uA = 10.
    //
    Status = WledRmw (Str + WLED_SINK_FS_CURR, 0xF0, (UINT8)(25000 / 2500));
    if (EFI_ERROR (Status)) {
      return Status;
    }

    //
    // CABC off.
    //
    Status = WledRmw (Str + WLED_SINK_CABC, 0x7F, 0x00);
    if (EFI_ERROR (Status)) {
      return Status;
    }

    //
    // Enable this string's current sink.
    //
    Status = WledRmw (Sink + WLED_SINK_CURR_SINK, 0x00, (UINT8)(1 << (n + 4)));
    if (EFI_ERROR (Status)) {
      return Status;
    }
  }

  //
  // Latch the sink configuration.
  //
  Status = WledWrite (Sink + WLED_SINK_SYNC, WLED_SYNC_LATCH);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  WledDelayUs (1000);

  Status = WledWrite (Sink + WLED_SINK_SYNC, WLED_SYNC_RESET);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  if (Level == 0) {
    return EFI_SUCCESS;
  }

  Status = WledSetLevel (Level);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  //
  // Finally enable the boost.  From here the panel has light even if the
  // DSI/MDP5 side is still being brought up -- which is exactly the
  // signal we want on a board with no serial console.
  //
  return WledRmw (Ctrl + WLED_CTRL_MODULE_EN, 0x7F, 0x80);
}
