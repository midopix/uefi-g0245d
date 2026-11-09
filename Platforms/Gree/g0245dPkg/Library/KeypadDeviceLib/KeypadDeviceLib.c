/** @file
 *
 *  G0245D (msm8996 / PM8994) physical keypad device library -- v92.
 *
 *  Every key is read straight from the PMIC over the self-written, BOUNDED
 *  Msm8996SpmiLib (SPMI_TIMEOUT_POLLS=1000 -> EFI_TIMEOUT, no infinite spin).
 *  There is deliberately NO dependency on any XBL / SMEM / DAL / PlatformInfo
 *  protocol: v79..v90 proved that lifting vendor DXEs into Mu-Silicium hangs
 *  the single-threaded DXE dispatcher (see g0245d/PITFALLS.md D3/D4).
 *
 *  Hardware facts, recovered from the device's OWN DTB
 *  (Resources/DTBs/g0245d.dtb via tools/dump_gpio_keys.py):
 *
 *    /soc/gpio_keys/vol_up        gpios = <&pm8994_gpios 2 1>  KEY_VOLUMEUP
 *    /soc/gpio_keys/home          gpios = <&pm8994_gpios 1 1>  KEY_HOME
 *    /soc/gpio_keys/cam_snapshot  gpios = <&pm8994_gpios 4 1>
 *    /soc/gpio_keys/cam_focus     gpios = <&pm8994_gpios 5 1>  KEY_POWER2
 *    /soc/qcom,spmi@400f000/qcom,pm8994@0/qcom,power-on@800
 *        pon_1 qcom,pon-type = 0 (KPDPWR)  -> linux,code 116 KEY_POWER
 *        pon_2 qcom,pon-type = 1 (RESIN)   -> linux,code 114 KEY_VOLUMEDOWN
 *
 *  The gpios phandle is 0xf9 -> /soc/qcom,spmi@400f000/qcom,pm8994@0/gpios,
 *  i.e. these are PM8994 **PMIC GPIOs**, NOT TLMM SoC GPIOs.  PMIC GPIO n has
 *  its peripheral at PM8994_GPIO_BASE + 0x100*(n-1), and the real-time pad
 *  level is register 0x10 (pinctrl-spmi-gpio.c), bit0 = current level.
 *  flags = 0x1 means GPIO_ACTIVE_LOW: a PRESSED key reads 0.
 *
 *  PON polarity is NOT assumed.  The constructor snapshots INT_RT_STS (0x810)
 *  once at boot; GetKeys treats any bit that DIFFERS from that baseline as a
 *  pressed key.  That is correct whether KPDPWR_N / RESIN_N are active-high or
 *  active-low, and avoids having to trust a half-remembered Linux #define.
 *  KeyProbeDxe (temporary, removed in the clean follow-up) prints the raw
 *  values on screen so one boot photo settles the polarity for good.
 *
 *  Copyright (c) 2026, UniXNU contributors.
 *  SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 **/

#include <Uefi.h>

#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/KeypadDeviceLib.h>
#include <Library/Msm8996SpmiLib.h>

#include <Msm8996Regs.h>

#define MS2NS(ms)  (((UINT64)(ms)) * 1000000ULL)

//
// Debounce / repeat timings.  Delta from KeypadDxe is nanoseconds
// (KeypadController.c: GetTimeInNanoSecond(GetPerformanceCounter delta)).
//
#define KEY_DEBOUNCE_NS   MS2NS (20)
#define KEY_REPEAT_1ST_NS MS2NS (500)
#define KEY_REPEAT_NS     MS2NS (100)

//
// v96: when the arch-timer clock is unusable (CNTFRQ_EL0 == 0 at LK hand-off),
// GetTimeInNanoSecond() divides by zero and every GetKeys() call receives a
// Delta of 0.  Key->Ctx.Time then never reaches KEY_DEBOUNCE_NS (20 ms) and
// NO key is ever pushed into the EFI key queue -- the exact "all buttons dead"
// symptom of v93..v95, which changing PMIC pads / ConIn could not fix.
// Fall back to counting consecutive pressed samples instead of wall time.
//
#define KEY_DEBOUNCE_SAMPLES  3

#define PON_RT_STS_ADDR  (PM8994_PON_BASE + PM8994_PON_REG_INT_RT_STS)

//
// PMIC GPIO real-time-status addresses, in the order GPIO1,2,4,5.
//
STATIC CONST UINT16 mGpioRtSts[4] = {
  PM8994_GPIO_BASE + 0x100 * (1 - 1) + PMIC_GPIO_REG_RT_STS,   // 0xC010 home
  PM8994_GPIO_BASE + 0x100 * (2 - 1) + PMIC_GPIO_REG_RT_STS,   // 0xC110 vol_up
  PM8994_GPIO_BASE + 0x100 * (4 - 1) + PMIC_GPIO_REG_RT_STS,   // 0xC310 cam_snapshot
  PM8994_GPIO_BASE + 0x100 * (5 - 1) + PMIC_GPIO_REG_RT_STS,   // 0xC410 cam_focus
};

//
// v94 (ADAPTIVE): configure the four gpio_keys PMIC pads as inputs with a
// pull-up chosen AT RUNTIME, not from a guessed compile-time constant.
//
// v93 assumed DIG_PULL_CTL == 0 means "30 uA pull-up" (the mainline
// pinctrl-spmi-gpio.c encoding).  That was the ONE unverified part of the
// whole tree (PITFALLS.md D9), and the keys were still dead after v93 was
// flashed.  Instead of sweeping one guessed value per build, we now try
// candidates 0..3, read the pad's RT_STS after each, and keep the FIRST one
// that biases the (active-low) line high.  No photo is needed to pick a value.
//
// KeyProbeDxe's existing colour strip then tells us which case we are in:
//   top row YELLOW -> bias worked, GPIO keys should work;
//   top row BLUE   -> SPMI read failed (channel / arbiter), NOT a pull value;
//   top row RED    -> pad still driven low or the writes did not land.
//
// MODE_CTL layout (Linux pinctrl-spmi-gpio.c): dir = bits[6:4] (0x70),
// function = bits[3:1] (0x0E), out_value = bit0.  v93 cleared only bit 4;
// v94..v105 cleared 0x70.  v106 clears BOTH: a leftover non-zero function
// selects a special/DTEST mux, so the input stage never sees the physical
// pin and no pull value can produce a key edge.
//
#define PMIC_GPIO_MODE_FIELD_MASK  0x7E
// Bias encodings 0..3 are pull-ups (0 = 30 uA).  4 is PULL_DOWN and 5 is
// PULL_DISABLE; neither can bias an active-low key line high.
#define PULL_CTL_CANDIDATES       4
#define PAD_PROBE_READS           3      // tolerate a briefly held key / glitch

STATIC
EFI_STATUS
ReadPadLevel (
  IN  UINT16  Base,
  OUT UINT8   *Level
  )
{
  EFI_STATUS  Status;
  UINT8       RtSts;

  RtSts = 0;
  Status = Msm8996SpmiRead (
             PM8994_SID,
             (UINT16)(Base + PMIC_GPIO_REG_RT_STS),
             &RtSts, 1
             );
  if (EFI_ERROR (Status)) {
    return Status;
  }

  *Level = (UINT8)(RtSts & 1);
  return EFI_SUCCESS;
}

STATIC
VOID
ConfigureGpioPads (
  VOID
  )
{
  UINT8       Val;
  UINT16      Base;
  UINTN       Index;
  UINTN       Cand;
  UINTN       Read;
  UINT8       Level;
  UINT8       GpioNums[4] = { 1, 2, 4, 5 };
  BOOLEAN     Found;
  EFI_STATUS  Status;

  for (Index = 0; Index < 4; Index++) {
    Base = (UINT16)(PM8994_GPIO_BASE + 0x100 * (GpioNums[Index] - 1));

    //
    // 1) Force normal GPIO / digital INPUT: clear dir (0x70) AND function
    //    (0x0E) in MODE_CTL.  A leftover function mux keeps the input stage
    //    off the physical pin (v106; see the note above).
    //
    Val = 0;
    Status = Msm8996SpmiRead (
               PM8994_SID,
               (UINT16)(Base + PMIC_GPIO_REG_MODE_CTL),
               &Val, 1
               );
    if (EFI_ERROR (Status)) {
      continue;                 // SPMI not reachable for this peripheral
    }
    Val = (UINT8)(Val & ~PMIC_GPIO_MODE_FIELD_MASK);
    Msm8996SpmiWrite (
      PM8994_SID, (UINT16)(Base + PMIC_GPIO_REG_MODE_CTL), &Val, 1
      );

    //
    // 2) v106: the ONE change under test -- DIG_IN_CTL (0x43) selects the
    //    input source.  Mainline pmic_gpio_config_set() writes 0 here when
    //    dtest_buffer == 0, selecting the PHYSICAL PIN.  A non-zero leftover
    //    from the stock bootloader routes a DTEST buffer into the input
    //    stage, so RT_STS reports a constant internal value and never
    //    changes when a key is pressed -- exactly the 2026-11-09 symptom
    //    ("top row fixed red/yellow, bottom row never green").
    //
    //    Deliberately NOT touching DIG_VIN_CTL (0x41) or DIG_OUT_CTL (0x45):
    //    this board's gpio_keys DT node carries no pinctrl-* / qcom,vin-sel,
    //    so the power domain was set up by the bootloader and we must not
    //    overwrite it with a guessed value.  One variable per flash.
    //
    Val = 0;
    Msm8996SpmiWrite (
      PM8994_SID, (UINT16)(Base + PMIC_GPIO_REG_DIG_IN_CTL), &Val, 1
      );

    // 3) Master-enable the pad.
    Msm8996SpmiUpdateBits (
      PM8994_SID, (UINT16)(Base + PMIC_GPIO_REG_EN_CTL),
      PMIC_GPIO_MASTER_EN, PMIC_GPIO_MASTER_EN
      );

    // 4) Try each pull-up encoding; keep the first that makes the line read high.
    Found = FALSE;
    for (Cand = 0; Cand < PULL_CTL_CANDIDATES; Cand++) {
      Val = (UINT8)Cand;
      Status = Msm8996SpmiWrite (
                 PM8994_SID,
                 (UINT16)(Base + PMIC_GPIO_REG_DIG_PULL_CTL),
                 &Val, 1
                 );
      if (EFI_ERROR (Status)) {
        continue;
      }

      for (Read = 0; Read < PAD_PROBE_READS; Read++) {
        Status = ReadPadLevel (Base, &Level);
        if (EFI_ERROR (Status)) {
          break;
        }
        if (Level != 0) {
          Found = TRUE;
          break;
        }
      }
      if (Found) {
        break;
      }
    }

    // 5) Conservative fallback: restore the mainline 30 uA pull-up value.
    if (!Found) {
      Val = 0;
      Msm8996SpmiWrite (
        PM8994_SID, (UINT16)(Base + PMIC_GPIO_REG_DIG_PULL_CTL), &Val, 1
        );
    }
  }
}

typedef struct {
  KEY_CONTEXT Ctx;
  BOOLEAN     PrevPressed;
  BOOLEAN     Reported;
  UINTN       SampleCount;   // v96: consecutive pressed samples when Delta == 0
} KEY_PRIVATE;

//
// Index -> key:
//   0 volume up     -> SCAN_UP
//   1 volume down   -> SCAN_DOWN
//   2 power         -> CHAR_CARRIAGE_RETURN (Enter)
//   3 home          -> SCAN_ESC
//   4 cam snapshot  -> SCAN_LEFT  (spare)
//   5 cam focus     -> SCAN_RIGHT (spare)
//
#define KEY_VOL_UP     0
#define KEY_VOL_DOWN   1
#define KEY_POWER      2
#define KEY_HOME       3
#define KEY_CAM_SNAP   4
#define KEY_CAM_FOCUS  5
#define KEY_COUNT      6

STATIC KEY_PRIVATE mKeys[KEY_COUNT];

STATIC UINT8    mPonBaseline      = 0;
STATIC BOOLEAN  mPonBaselineValid = FALSE;

//
// v95: per-pad usability instead of v94's AllGpioLow blanket veto.
//
// A PMIC GPIO key is active-low, so a pad that has NEVER been observed at
// level 1 cannot be distinguished from a key that is permanently held.  v94
// suppressed ALL FOUR GPIO keys whenever any pad was stuck low (or when SPMI
// reads failed), turning one bad pad into "no keys work at all" -- exactly the
// v94 symptom.  v95 tracks usability PER PAD: the first time a pad reads 1 it
// becomes usable, and only then can its key be reported.
//
STATIC BOOLEAN  mPadUsable[4] = { FALSE, FALSE, FALSE, FALSE };

/**
  v95: seed per-pad usability from the post-bias idle levels.  A pad reading
  1 here means ConfigureGpioPads()' adaptive pull scan succeeded for it, so
  its key is live from the very first GetKeys() call.  Pads that stay low (or
  whose SPMI read fails) are skipped PER PAD in GetKeys() instead of vetoing
  all four keys the way v94's AllGpioLow guard did.
**/
STATIC
VOID
ProbePadUsability (
  VOID
  )
{
  UINT8   Level;
  UINTN   Index;

  for (Index = 0; Index < 4; Index++) {
    Level = 0;
    if (!EFI_ERROR (Msm8996SpmiRead (PM8994_SID, mGpioRtSts[Index], &Level, 1))) {
      if ((Level & 1) != 0) {
        mPadUsable[Index] = TRUE;
      }
    }
  }
}

/**
  Library constructor: take the power-on PON RT_STS snapshot.

  This runs before the DXE driver's entry point, but Msm8996SpmiLib is pure
  MMIO with a bounded poll -- it cannot hang the dispatcher even if the PMIC
  is unresponsive (worst case ~1 ms -> EFI_TIMEOUT).
**/
RETURN_STATUS
EFIAPI
KeypadDeviceConstructor (
  VOID
  )
{
  UINT8       Pon;
  EFI_STATUS  Status;

  SetMem (mKeys, sizeof (mKeys), 0);

  ConfigureGpioPads ();
  ProbePadUsability ();

  Status = Msm8996SpmiRead (PM8994_SID, PON_RT_STS_ADDR, &Pon, 1);
  if (!EFI_ERROR (Status)) {
    mPonBaseline      = Pon;
    mPonBaselineValid = TRUE;
  } else {
    mPonBaselineValid = FALSE;
  }

  return RETURN_SUCCESS;
}

/**
  Reset: (re)initialise the six key contexts and their EFI key codes.
**/
EFI_STATUS
EFIAPI
KeypadDeviceReset (
  KEYPAD_DEVICE_PROTOCOL *This
  )
{
  UINTN  Index;

  for (Index = 0; Index < KEY_COUNT; Index++) {
    SetMem (&mKeys[Index].Ctx, sizeof (KEY_CONTEXT), 0);
    mKeys[Index].Ctx.State        = KEYSTATE_RELEASED;
    mKeys[Index].Ctx.Time         = 0;
    mKeys[Index].Ctx.Repeat       = FALSE;
    mKeys[Index].Ctx.Longpress    = FALSE;
    mKeys[Index].PrevPressed      = FALSE;
    mKeys[Index].Reported         = FALSE;
    mKeys[Index].SampleCount      = 0;
  }

  mKeys[KEY_VOL_UP].Ctx.KeyData.Key.ScanCode       = SCAN_UP;
  mKeys[KEY_VOL_DOWN].Ctx.KeyData.Key.ScanCode     = SCAN_DOWN;
  mKeys[KEY_POWER].Ctx.KeyData.Key.UnicodeChar     = CHAR_CARRIAGE_RETURN;
  mKeys[KEY_HOME].Ctx.KeyData.Key.ScanCode         = SCAN_ESC;
  mKeys[KEY_CAM_SNAP].Ctx.KeyData.Key.ScanCode     = SCAN_LEFT;
  mKeys[KEY_CAM_FOCUS].Ctx.KeyData.Key.ScanCode    = SCAN_RIGHT;

  return EFI_SUCCESS;
}

/**
  Edge/debounce/repeat state machine.  A key is reported once after 20 ms of
  continuous press, again after 500 ms, then every 100 ms (auto repeat).
**/
STATIC
VOID
UpdateKey (
  IN  KEY_PRIVATE        *Key,
  IN  KEYPAD_RETURN_API  *Api,
  IN  BOOLEAN             Pressed,
  IN  UINT64              Delta
  )
{
  if (!Pressed) {
    Key->PrevPressed          = FALSE;
    Key->Reported             = FALSE;
    Key->Ctx.State            = KEYSTATE_RELEASED;
    Key->Ctx.Time             = 0;
    Key->Ctx.Repeat           = FALSE;
    Key->Ctx.Longpress        = FALSE;
    return;
  }

  if (!Key->PrevPressed) {
    //
    // Fresh press: swallow this sample as debounce, start timing from 0.
    //
    Key->PrevPressed   = TRUE;
    Key->Ctx.State     = KEYSTATE_PRESSED;
    Key->Ctx.Time      = 0;
    Key->Reported      = FALSE;
    Key->Ctx.Longpress = FALSE;
    Key->SampleCount   = 0;
    return;
  }

  Key->SampleCount++;
  Key->Ctx.Time += Delta;

  if (!Key->Reported) {
    //
    // v96: two ways through debounce.  With a working arch-timer clock use
    // the normal 20 ms; when Delta is 0 (CNTFRQ == 0 / divide-by-zero in
    // GetTimeInNanoSecond) fall back to 3 consecutive pressed samples.
    // Without this the key is NEVER reported on this board.
    //
    if ((Key->Ctx.Time >= KEY_DEBOUNCE_NS) ||
        ((Delta == 0) && (Key->SampleCount >= KEY_DEBOUNCE_SAMPLES)))
    {
      Api->PushEfikeyBufTail (Api, &Key->Ctx.KeyData);
      Key->Reported    = TRUE;
      Key->Ctx.Time    = 0;
      Key->SampleCount = 0;
    }
    return;
  }

  //
  // v96: auto-repeat needs a REAL clock.  If Delta is 0, keep exactly one
  // key event per press and never repeat -- otherwise a busy WaitForEvent
  // loop would fire hundreds of duplicate keys per second.
  //
  if (Delta == 0) {
    return;
  }

  if (!Key->Ctx.Longpress) {
    if (Key->Ctx.Time >= KEY_REPEAT_1ST_NS) {
      Api->PushEfikeyBufTail (Api, &Key->Ctx.KeyData);
      Key->Ctx.Longpress = TRUE;
      Key->Ctx.Repeat    = TRUE;
      Key->Ctx.Time      = 0;
    }
  } else if (Key->Ctx.Time >= KEY_REPEAT_NS) {
    Api->PushEfikeyBufTail (Api, &Key->Ctx.KeyData);
    Key->Ctx.Time = 0;
  }
}

/**
  Poll all six keys and push EFI key events.
**/
EFI_STATUS
KeypadDeviceGetKeys (
  KEYPAD_DEVICE_PROTOCOL *This,
  KEYPAD_RETURN_API      *KeypadReturnApi,
  UINT64                  Delta
  )
{
  UINT8     Pon;
  UINT8     Gpio[4];
  UINT8     Changed;
  BOOLEAN   Pressed[KEY_COUNT];
  BOOLEAN   GpioValid[4];
  EFI_STATUS Status;
  UINTN      Index;

  if (KeypadReturnApi == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  for (Index = 0; Index < KEY_COUNT; Index++) {
    Pressed[Index] = FALSE;
  }

  //
  // PON (power + volume down): XOR against the boot snapshot.
  //
  Pon = mPonBaseline;
  Status = Msm8996SpmiRead (PM8994_SID, PON_RT_STS_ADDR, &Pon, 1);
  if (!EFI_ERROR (Status)) {
    if (!mPonBaselineValid) {
      mPonBaseline      = Pon;
      mPonBaselineValid = TRUE;
    }

    Changed = (UINT8)(Pon ^ mPonBaseline);
    Pressed[KEY_POWER]    = ((Changed & PON_KPDPWR_N_BIT) != 0);
    Pressed[KEY_VOL_DOWN] = ((Changed & PON_RESIN_N_BIT) != 0);
  }

  //
  // PMIC GPIO keys (volume up, home, camera): active-low, bit0 = pad level.
  //
  // v95: PER-PAD usability instead of v94's AllGpioLow blanket veto.  v94
  // threw away ALL FOUR keys whenever any single pad read 0 (or SPMI failed),
  // which is exactly how one un-biased pad turned into "no keys respond".
  // Here a pad becomes usable the first time it reads 1 (released level), and
  // only usable pads can report a press.  A stuck-low / unreadable pad is
  // skipped on its own, without killing its three neighbours.
  //
  for (Index = 0; Index < 4; Index++) {
    Gpio[Index]      = 1;
    GpioValid[Index] = FALSE;
    Status = Msm8996SpmiRead (PM8994_SID, mGpioRtSts[Index], &Gpio[Index], 1);
    if (EFI_ERROR (Status)) {
      continue;
    }

    GpioValid[Index] = TRUE;
    if ((Gpio[Index] & 1) != 0) {
      mPadUsable[Index] = TRUE;      // observed a released level -> pad is live
    }
  }

  if (GpioValid[0] && mPadUsable[0]) {
    Pressed[KEY_HOME]      = ((Gpio[0] & 1) == 0);
  }
  if (GpioValid[1] && mPadUsable[1]) {
    Pressed[KEY_VOL_UP]    = ((Gpio[1] & 1) == 0);
  }
  if (GpioValid[2] && mPadUsable[2]) {
    Pressed[KEY_CAM_SNAP]  = ((Gpio[2] & 1) == 0);
  }
  if (GpioValid[3] && mPadUsable[3]) {
    Pressed[KEY_CAM_FOCUS] = ((Gpio[3] & 1) == 0);
  }

  for (Index = 0; Index < KEY_COUNT; Index++) {
    UpdateKey (&mKeys[Index], KeypadReturnApi, Pressed[Index], Delta);
  }

  return EFI_SUCCESS;
}
