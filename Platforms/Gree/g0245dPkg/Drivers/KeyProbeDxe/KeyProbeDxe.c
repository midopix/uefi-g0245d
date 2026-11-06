/** @file
 *
 *  TEMPORARY v92 keypad diagnostic strip.  See KeyProbeDxe.inf.
 *
 *  v96: the strip NO LONGER depends solely on the periodic timer.  A GOP
 *  protocol-notify callback paints it once, synchronously, as soon as the
 *  framebuffer appears; the periodic timer (if it ever fires) refreshes it.
 *  A white "SYNC" marker block is painted above the strip by the synchronous
 *  path and painted BLACK by the first timer tick.  One boot photo therefore
 *  answers two questions at once:
 *     white marker still visible  -> EVT_TIMER never dispatched
 *     marker gone / strip refresh -> timer is alive, look at the raw columns
 *
 *  A 200 ms periodic timer reads the SAME registers KeypadDeviceLib reads and
 *  paints two rows of cells at the bottom of the panel with GOP Blt (the same
 *  path BootMarkDxe used successfully in v83..v89).  GOP is located inside
 *  the drawing helper, so this module needs no ordering depex and cannot block
 *  DXE dispatch.  All SPMI reads are bounded by Msm8996SpmiLib's 1000-poll
 *  timeout.
 *
 *  Cell columns (left -> right):
 *    0  PON INT_RT_STS bit0  (KPDPWR / power)
 *    1  PON INT_RT_STS bit1  (RESIN / volume down)
 *    2  PM8994 GPIO1 RT_STS  (home)
 *    3  PM8994 GPIO2 RT_STS  (volume up)
 *    4  PM8994 GPIO4 RT_STS  (cam snapshot)
 *    5  PM8994 GPIO5 RT_STS  (cam focus)
 *    6  heartbeat (alternates cyan / magenta -> proves the timer runs)
 *    7  v95 input-stack status: SimpleTextInEx exists
 *    8  v95 input-stack status: gST->ConIn non-NULL
 *
 *  Top row    : yellow = raw bit 1, red = raw bit 0, blue = SPMI read failed
 *  Bottom row : green = this source changed at least once since boot
 *
 *  Copyright (c) 2026, UniXNU contributors.
 *  SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 **/

#include <Uefi.h>

#include <Protocol/GraphicsOutput.h>
#include <Protocol/SimpleTextInEx.h>

#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiDriverEntryPoint.h>
#include <Library/DebugLib.h>
#include <Library/Msm8996SpmiLib.h>

#include <Msm8996Regs.h>

#define PROBE_PERIOD_100NS  2000000ULL   // 200 ms
#define PROBE_COLS          6
#define PROBE_PITCH         44
#define PROBE_CELL          36
#define PROBE_X0            20
#define RAW_FAILED          2            // sentinel: SPMI read failed

//
// v95: two extra status cells to the RIGHT of the heartbeat column (col 6).
// They sit at x = 20 + 7*44 = 328 and 20 + 8*44 = 372, i.e. past the
// heartbeat's [284,320] box, so the 6 raw columns + heartbeat sampled by
// read-keyprobe-strip.py are untouched.
//
#define PROBE_STACK_COL_STI    7
#define PROBE_STACK_COL_CONIN  8

//
// v96: white SYNC marker, three cells wide, 56 px ABOVE the top row
// (H-140 vs H-84).  The synchronous GOP-notify paint draws it WHITE; the
// periodic timer repaints it BLACK on its very first tick.  classify() in
// read-keyprobe-strip.py ignores white/black (low saturation), so the marker
// cannot disturb the 9-column strip detection.
//
#define SYNC_MARKER_Y_OFFSET   140
#define SYNC_MARKER_CELLS      3

#define PON_RT_STS_ADDR  (PM8994_PON_BASE + PM8994_PON_REG_INT_RT_STS)

STATIC CONST UINT16 mGpioRtSts[4] = {
  PM8994_GPIO_BASE + 0x100 * (1 - 1) + PMIC_GPIO_REG_RT_STS,
  PM8994_GPIO_BASE + 0x100 * (2 - 1) + PMIC_GPIO_REG_RT_STS,
  PM8994_GPIO_BASE + 0x100 * (4 - 1) + PMIC_GPIO_REG_RT_STS,
  PM8994_GPIO_BASE + 0x100 * (5 - 1) + PMIC_GPIO_REG_RT_STS,
};

STATIC EFI_EVENT mTimerEvent     = NULL;
STATIC EFI_EVENT mGopNotifyEvent = NULL;
STATIC VOID     *mGopRegistration = NULL;
STATIC UINT8     mPrev[PROBE_COLS];
STATIC UINT8     mChanged[PROBE_COLS];
STATIC UINT32    mTick = 0;

STATIC
VOID
FillCell (
  IN EFI_GRAPHICS_OUTPUT_PROTOCOL  *Gop,
  IN UINT32                         X,
  IN UINT32                         Y,
  IN UINT8                          Red,
  IN UINT8                          Green,
  IN UINT8                          Blue
  )
{
  EFI_GRAPHICS_OUTPUT_BLT_PIXEL  Color;

  Color.Red      = Red;
  Color.Green    = Green;
  Color.Blue     = Blue;
  Color.Reserved = 0;

  Gop->Blt (
         Gop,
         &Color,
         EfiBltVideoFill,
         0, 0,
         X, Y,
         PROBE_CELL, PROBE_CELL,
         0
         );
}

/**
  Paint the whole strip plus the v96 SYNC marker.

  @param TimerRan  TRUE when called from the periodic timer callback (the
                   marker is cleared to black), FALSE for the one-shot
                   synchronous paint (marker left white).
**/
STATIC
VOID
DrawProbeStrip (
  IN EFI_GRAPHICS_OUTPUT_PROTOCOL  *Gop,
  IN BOOLEAN                        TimerRan
  )
{
  EFI_SIMPLE_TEXT_INPUT_EX_PROTOCOL  *StiEx;
  EFI_STATUS                         Status;
  EFI_STATUS                         StackStatus;
  UINT8                              Pon;
  UINT8                              Gpio[4];
  UINT8                              Cur[PROBE_COLS];
  UINT32                             Height;
  UINT32                             X;
  UINT32                             StackX;
  UINT32                             MarkerY;
  UINT8                              MarkerR;
  UINT8                              MarkerG;
  UINT8                              MarkerB;
  UINTN                              Index;

  if ((Gop == NULL) || (Gop->Mode == NULL) || (Gop->Mode->Info == NULL)) {
    return;
  }

  Height = Gop->Mode->Info->VerticalResolution;
  if (Height < 200) {
    return;
  }

  //
  // Raw PON bits.
  //
  Pon = 0;
  Status = Msm8996SpmiRead (PM8994_SID, PON_RT_STS_ADDR, &Pon, 1);
  if (EFI_ERROR (Status)) {
    Cur[0] = RAW_FAILED;
    Cur[1] = RAW_FAILED;
  } else {
    Cur[0] = (UINT8)((Pon >> 0) & 1);
    Cur[1] = (UINT8)((Pon >> 1) & 1);
  }

  //
  // Raw PMIC GPIO pad levels.
  //
  for (Index = 0; Index < 4; Index++) {
    Gpio[Index] = 0;
    Status = Msm8996SpmiRead (
               PM8994_SID,
               mGpioRtSts[Index],
               &Gpio[Index],
               1
               );
    if (EFI_ERROR (Status)) {
      Cur[2 + Index] = RAW_FAILED;
    } else {
      Cur[2 + Index] = (UINT8)(Gpio[Index] & 1);
    }
  }

  //
  // Accumulate "ever changed" history. mPrev starts as 0xFF (set in entry).
  //
  for (Index = 0; Index < PROBE_COLS; Index++) {
    if ((mPrev[Index] != 0xFF) && (Cur[Index] != mPrev[Index])) {
      mChanged[Index] = 1;
    }
    mPrev[Index] = Cur[Index];
  }

  mTick++;

  //
  // Top row = current raw value. Bottom row = changed-since-boot.
  //
  for (Index = 0; Index < PROBE_COLS; Index++) {
    X = PROBE_X0 + (UINT32)Index * PROBE_PITCH;

    if (Cur[Index] == RAW_FAILED) {
      FillCell (Gop, X, Height - 84, 0x20, 0x20, 0xFF);       // blue
    } else if (Cur[Index] != 0) {
      FillCell (Gop, X, Height - 84, 0xFF, 0xFF, 0x00);       // yellow
    } else {
      FillCell (Gop, X, Height - 84, 0xFF, 0x00, 0x00);       // red
    }

    if (mChanged[Index] != 0) {
      FillCell (Gop, X, Height - 44, 0x00, 0xFF, 0x00);       // green
    } else {
      FillCell (Gop, X, Height - 44, 0x30, 0x30, 0x30);       // dark grey
    }
  }

  //
  // Heartbeat in the last data column (only top row).
  //
  X = PROBE_X0 + PROBE_COLS * PROBE_PITCH;
  if ((mTick & 1) != 0) {
    FillCell (Gop, X, Height - 84, 0x00, 0xDC, 0xDC);         // cyan
  } else {
    FillCell (Gop, X, Height - 84, 0xDC, 0x00, 0xDC);         // magenta
  }

  //
  // v95: input-stack status cells, to the RIGHT of the heartbeat.
  //   col 7 GREEN = at least one SimpleTextInEx instance exists
  //          RED  = none (ConIn never connected / KeypadDxe never started)
  //   col 8 GREEN = gST->ConIn is non-NULL
  //          RED  = no console input handle at all
  //
  StiEx = NULL;
  StackStatus = gBS->LocateProtocol (
                       &gEfiSimpleTextInputExProtocolGuid,
                       NULL,
                       (VOID **)&StiEx
                       );
  StackX = PROBE_X0 + (UINT32)PROBE_STACK_COL_STI * PROBE_PITCH;
  if (!EFI_ERROR (StackStatus) && (StiEx != NULL)) {
    FillCell (Gop, StackX, Height - 84, 0x00, 0xFF, 0x00);   // green
  } else {
    FillCell (Gop, StackX, Height - 84, 0xFF, 0x00, 0x00);   // red
  }

  StackX = PROBE_X0 + (UINT32)PROBE_STACK_COL_CONIN * PROBE_PITCH;
  if (gST->ConIn != NULL) {
    FillCell (Gop, StackX, Height - 84, 0x00, 0xFF, 0x00);   // green
  } else {
    FillCell (Gop, StackX, Height - 84, 0xFF, 0x00, 0x00);   // red
  }

  //
  // v96 SYNC marker: WHITE on the synchronous one-shot paint, BLACK after
  // the first timer tick.  Three adjacent cells make it unmistakable in a
  // phone photo while remaining outside the strip's column geometry.
  //
  MarkerY = Height - SYNC_MARKER_Y_OFFSET;
  if (TimerRan) {
    MarkerR = 0x00;
    MarkerG = 0x00;
    MarkerB = 0x00;
  } else {
    MarkerR = 0xFF;
    MarkerG = 0xFF;
    MarkerB = 0xFF;
  }

  for (Index = 0; Index < SYNC_MARKER_CELLS; Index++) {
    FillCell (
      Gop,
      PROBE_X0 + (UINT32)Index * PROBE_PITCH,
      MarkerY,
      MarkerR, MarkerG, MarkerB
      );
  }
}

STATIC
EFI_GRAPHICS_OUTPUT_PROTOCOL *
LocateGop (
  IN VOID  *Registration
  )
{
  EFI_GRAPHICS_OUTPUT_PROTOCOL  *Gop;
  EFI_STATUS                    Status;

  Gop = NULL;
  Status = gBS->LocateProtocol (
                  &gEfiGraphicsOutputProtocolGuid,
                  Registration,
                  (VOID **)&Gop
                  );
  if (EFI_ERROR (Status)) {
    return NULL;
  }
  return Gop;
}

/**
  v96: one-shot synchronous paint.  Fires when DisplayDxe installs GOP
  (and immediately at registration time if GOP already exists).
**/
STATIC
VOID
EFIAPI
OnGopInstalled (
  IN EFI_EVENT  Event,
  IN VOID       *Context
  )
{
  EFI_GRAPHICS_OUTPUT_PROTOCOL  *Gop;

  Gop = LocateGop (mGopRegistration);
  if (Gop != NULL) {
    DrawProbeStrip (Gop, FALSE);
  }
}

/**
  Periodic refresh.  If this never fires, the white SYNC marker left by the
  synchronous paint stays on screen -- which is exactly the v96 diagnostic.
**/
STATIC
VOID
EFIAPI
ProbeTimerHandler (
  IN EFI_EVENT  Event,
  IN VOID       *Context
  )
{
  EFI_GRAPHICS_OUTPUT_PROTOCOL  *Gop;

  Gop = LocateGop (NULL);
  if (Gop != NULL) {
    DrawProbeStrip (Gop, TRUE);
  }
}

EFI_STATUS
EFIAPI
KeyProbeDxeEntryPoint (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_GRAPHICS_OUTPUT_PROTOCOL  *Gop;
  EFI_STATUS                    Status;
  UINTN                         Index;

  for (Index = 0; Index < PROBE_COLS; Index++) {
    mPrev[Index]    = 0xFF;
    mChanged[Index] = 0;
  }

  //
  // v96: paint synchronously as soon as GOP is available.  RegisterProtocolNotify
  // covers the normal case (DisplayDxe dispatches after us); the direct
  // LocateProtocol covers the case where GOP was already installed.
  //
  Gop = LocateGop (NULL);
  if (Gop != NULL) {
    DrawProbeStrip (Gop, FALSE);
  }

  Status = gBS->CreateEvent (
                  EVT_NOTIFY_SIGNAL,
                  TPL_CALLBACK,
                  OnGopInstalled,
                  NULL,
                  &mGopNotifyEvent
                  );
  if (!EFI_ERROR (Status)) {
    Status = gBS->RegisterProtocolNotify (
                    &gEfiGraphicsOutputProtocolGuid,
                    mGopNotifyEvent,
                    &mGopRegistration
                    );
    if (EFI_ERROR (Status)) {
      DEBUG ((DEBUG_WARN, "KeyProbeDxe: RegisterProtocolNotify failed %r\n", Status));
      gBS->CloseEvent (mGopNotifyEvent);
      mGopNotifyEvent = NULL;
    }
  } else {
    DEBUG ((DEBUG_WARN, "KeyProbeDxe: GOP notify event failed %r\n", Status));
  }

  Status = gBS->CreateEvent (
                  EVT_TIMER | EVT_NOTIFY_SIGNAL,
                  TPL_CALLBACK,
                  ProbeTimerHandler,
                  NULL,
                  &mTimerEvent
                  );
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_WARN, "KeyProbeDxe: CreateEvent failed %r\n", Status));
    return Status;
  }

  Status = gBS->SetTimer (mTimerEvent, TimerPeriodic, PROBE_PERIOD_100NS);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_WARN, "KeyProbeDxe: SetTimer failed %r\n", Status));
    gBS->CloseEvent (mTimerEvent);
    mTimerEvent = NULL;
    return Status;
  }

  return EFI_SUCCESS;
}
