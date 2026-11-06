/** @file
 *
 *  PM8994 regulator / GPIO control via SPMI.
 *
 *  Register layout and voltage-range tables follow Linux mainline
 *  drivers/regulator/qcom_spmi-regulator.c ("common" register layout used
 *  by PM8994: 0x40 RANGE, 0x41 SET, 0x45 MODE, 0x46 EN) and
 *  drivers/pinctrl/qcom/pinctrl-spmi-gpio.c.
 *
 *  Rails wired on g0245d (all values extracted from the device DTB):
 *    UFS  vdda-phy  = L28 @ 0.925 V  (N300 -> nldo1 range 2, 12.5 mV step)
 *    UFS  vdda-pll  = L12 @ 1.8 V    (P600 -> pldo range 2, 50 mV step)
 *    UFS  vccq      = L25 @ 1.2 V    (N600 -> nldo3 range 0, 12.5 mV step)
 *    UFS  vccq2     = S4  @ 1.8 V    (SMPS -> smps range 1, 25 mV step)
 *    DSI  vdda      = L2  @ 1.25 V   (N600 -> nldo3 range 0, 12.5 mV step)
 *    DSI  vddio     = L14 @ 1.8 V    (P600 -> pldo range 2, 50 mV step)
 *
 *  Copyright (c) 2026, UniXNU contributors.
 *
 *  SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 **/

#ifndef __MSM8996_PMIC_LIB_H__
#define __MSM8996_PMIC_LIB_H__

#include <Uefi.h>

/** Rail identifiers for the supplies the g0245d board actually uses. **/
typedef enum {
  PmicRailUfsVddaPhy = 0,   ///< PM8994 L28, 0.925 V
  PmicRailUfsVddaPll,       ///< PM8994 L12, 1.8 V
  PmicRailUfsVccq,          ///< PM8994 L25, 1.2 V
  PmicRailUfsVccq2,         ///< PM8994 S4,  1.8 V
  PmicRailDsiVdda,          ///< PM8994 L2,  1.25 V
  PmicRailDsiVddio,         ///< PM8994 L14, 1.8 V
  PmicRailCount
} MSM8996_PMIC_RAIL;

/**
  Enable a regulator rail at its DTB-declared voltage.

  Reads TYPE/SUBTYPE at runtime and selects the voltage range exactly the
  way the mainline driver does, so no subtype assumption is baked in. If
  the rail is already enabled the call is a no-op (hot hand-off).

  @param  Rail     Rail identifier.
  @param  Uv       Requested voltage in microvolts.

  @retval EFI_SUCCESS            Rail enabled (or already on).
  @retval EFI_UNSUPPORTED        Unknown subtype for this rail.
  @retval other                  SPMI transaction error.
**/
EFI_STATUS
EFIAPI
Msm8996PmicEnableRail (
  IN MSM8996_PMIC_RAIL  Rail,
  IN UINT32             Uv
  );

/**
  Enable the PMIC GPIO that gates the panel backlight power
  (qcom,platform-bklight-en-gpio = pm8994 gpio 14 on g0245d).

  @retval EFI_SUCCESS   GPIO configured as driven output.
  @retval other         SPMI transaction error.
**/
EFI_STATUS
EFIAPI
Msm8996PmicBacklightGpioOn (
  VOID
  );

/**
  Bring up the PM8994 WLED boost that actually feeds the panel backlight.

  g0245d's ft8716 node declares "bl_ctrl_dcs" and takes lab/ibb from this
  block, so there is no discrete enable GPIO -- the boost must be programmed.
  Parameters are taken verbatim from the device DTB
  (qcom,leds@d800): vref 350 mV, switch 800 kHz, OVP 29.5 V, ILIM 980 mA,
  boost duty 26 ns, hybrid dimming, threshold 625, sync delay 800 us,
  full-scale current 25 mA on strings 1/2/3.

  The sequence mirrors qpnp_wled_config() followed by one brightness write
  and the boost enable, so the panel has light as soon as DSI owns the bus.

  @param  Level   Brightness 0..4095 (0 keeps the boost off).

  @retval EFI_SUCCESS   Boost programmed and enabled.
  @retval other         SPMI transaction error.
**/
EFI_STATUS
EFIAPI
Msm8996PmicWledInit (
  IN UINT16  Level
  );

#endif /* __MSM8996_PMIC_LIB_H__ */
