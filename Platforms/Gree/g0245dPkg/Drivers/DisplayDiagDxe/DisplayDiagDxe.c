/** @file
  DisplayDiagDxe v10 -- GOP-PATH ruler calibration + redraw + menu-geometry
  replica.

  Every previous ruler probe (v8/v9) wrote the framebuffer DIRECTLY and came
  out pixel-perfect, yet every menu rendered through the GOP blitter shows a
  deterministic middle-column vertical displacement that survives both cache
  strategies (v74 uncached slow writes, v75 write-back + per-blit flush).
  The GOP write path itself has never been calibrated statically.

  v10 draws through gop->Blt only (SimpleFbDxe -> FbBltFlipped):

  - t=0    : the v9 ruler (stripes, centre marker, edge bands, colour bar,
             reference lines, row numbers) rendered with EfiBltVideoFill.
  - t=10s  : once per second a tick counter is redrawn through
             EfiBltBufferToVideo + a background fill (mimics the countdown
             row redraw cadence of the real menu).
  - t=180s : the menu's exact card geometry is painted over the ruler with
             EfiBltVideoFill at the GOP coordinates computed from Ui.c
             (Scale=4, Margin=90, CardHeight=209, Pitch=221, MenuTop=556),
             including the four-entry overflow past VisibleRows=2 and the
             status-row background fills -- so one photo shows whether the
             blitter places fills correctly.

  Holds 4 minutes total, then boot continues.  No direct framebuffer
  stores, no MMIO.
**/

#include <Uefi.h>

#include <Protocol/GraphicsOutput.h>

#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiDriverEntryPoint.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>

#define FB_WIDTH   1080
#define FB_HEIGHT  1920

STATIC EFI_GRAPHICS_OUTPUT_PROTOCOL  *mGop;

STATIC
EFI_GRAPHICS_OUTPUT_BLT_PIXEL
Pix (
  IN UINT8  R,
  IN UINT8  G,
  IN UINT8  B
  )
{
  EFI_GRAPHICS_OUTPUT_BLT_PIXEL  P;

  P.Blue     = B;
  P.Green    = G;
  P.Red      = R;
  P.Reserved = 0;
  return P;
}

STATIC
VOID
Fill (
  IN UINTN  X,
  IN UINTN  Y,
  IN UINTN  W,
  IN UINTN  H,
  IN UINT8  R,
  IN UINT8  G,
  IN UINT8  B
  )
{
  EFI_GRAPHICS_OUTPUT_BLT_PIXEL  Color;

  Color = Pix (R, G, B);
  mGop->Blt (mGop, &Color, EfiBltVideoFill, 0, 0, (UINTN)X, (UINTN)Y, (UINTN)W, (UINTN)H, 0);
}

STATIC CONST UINT8  mDigits[10][5] = {
  { 0x3E, 0x51, 0x49, 0x45, 0x3E },   // 0
  { 0x00, 0x42, 0x7F, 0x40, 0x00 },   // 1
  { 0x42, 0x61, 0x51, 0x49, 0x46 },   // 2
  { 0x21, 0x41, 0x45, 0x4B, 0x31 },   // 3
  { 0x18, 0x14, 0x12, 0x7F, 0x10 },   // 4
  { 0x27, 0x45, 0x45, 0x45, 0x39 },   // 5
  { 0x3C, 0x4A, 0x49, 0x49, 0x30 },   // 6
  { 0x01, 0x71, 0x09, 0x05, 0x03 },   // 7
  { 0x36, 0x49, 0x49, 0x49, 0x36 },   // 8
  { 0x06, 0x49, 0x49, 0x29, 0x1E },   // 9
};

//
// One 5x7 font digit, rendered as Scale x Scale GOP fills (yellow).
//
STATIC
VOID
Digit (
  IN UINTN  X,
  IN UINTN  Y,
  IN UINTN  Value,
  IN UINTN  Scale
  )
{
  UINTN  C;
  UINTN  R;
  UINTN  S;
  UINTN  T;

  for (C = 0; C < 5; C++) {
    for (R = 0; R < 7; R++) {
      if ((mDigits[Value % 10][C] & (1 << R)) != 0) {
        for (T = 0; T < Scale; T++) {
          for (S = 0; S < Scale; S++) {
            Fill (X + C * Scale + S, Y + R * Scale + T, 1, 1, 255, 255, 0);
          }
        }
      }
    }
  }
}

STATIC
VOID
Num (
  IN UINTN  X,
  IN UINTN  Y,
  IN UINTN  Value,
  IN UINTN  Digits,
  IN UINTN  Scale
  )
{
  CHAR8  Buf[12];
  UINTN  I;
  UINTN  Len;

  Len = 0;
  if (Value == 0) {
    Buf[Len++] = '0';
  } else {
    while (Value > 0) {
      Buf[Len++] = (CHAR8)('0' + (Value % 10));
      Value     /= 10;
    }
  }

  while (Len < Digits) {
    Buf[Len++] = '0';
  }

  for (I = 0; I < Digits; I++) {
    Digit (X + I * 6 * Scale, Y, Buf[Digits - 1 - I] - '0', Scale);
  }
}

//
// The tick row: cleared and redrawn once per second through the same two
// operations the menu uses for its countdown row (bg fill + text blit).
//
STATIC
VOID
TickDraw (
  IN UINTN  Seconds
  )
{
  Fill (150, 250, 780, 90, 12, 13, 16);
  Num (200, 265, Seconds, 3, 5);
  Num (500, 265, Seconds * 7 % 100, 2, 5);
}

//
// Menu-geometry replica at the exact Ui.c coordinates (Scale=4):
//   Margin=90, CardHeight=209, Pitch=221, MenuTop=556, Badge=114.
// The real menu draws ALL entries (loop over EntryCount) even though
// VisibleRows=2, so entries 2 and 3 land past the footer boundary; the
// status rows are cleared afterwards by DrawStatusPortrait's bg fills.
//
STATIC
VOID
ReplicaDraw (
  VOID
  )
{
  UINTN  Idx;

  for (Idx = 0; Idx < 4; Idx++) {
    UINTN  Y = 556 + Idx * 221;

    if (Idx == 0) {
      Fill (90, Y, 900, 209, 0x2A, 0x5B, 0xD7);           // mAccent (selected)
      Fill (90, Y, 8, 209, 255, 255, 255);                // mMarker strip
    } else {
      Fill (90, Y, 900, 209, 0x17, 0x1A, 0x20);           // mRow
    }

    Fill (128, Y + 47, 114, 114, Idx == 0 ? 0x1B : 0x26, Idx == 0 ? 0x44 : 0x2A, Idx == 0 ? 0xA6 : 0x31);
  }

  //
  // Footer: rule + status-row background fills, exactly where the UI puts
  // them (FooterTop=1100, FooterPitch=76, rows cleared 2*76 tall).
  //
  Fill (90, 1043, 900, 2, 0x26, 0x2A, 0x31);
  Fill (0, 1100, FB_WIDTH, 152, 0x0B, 0x0C, 0x0F);
  Fill (0, 1176, FB_WIDTH, 152, 0x0B, 0x0C, 0x0F);
  Fill (0, 1404, FB_WIDTH, 152, 0x0B, 0x0C, 0x0F);
  Fill (0, 1708, FB_WIDTH, 152, 0x0B, 0x0C, 0x0F);
}

EFI_STATUS
EFIAPI
DisplayDiagDxeEntryPoint (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_STATUS  Status;
  UINTN       Sec;
  UINTN       X;
  UINTN       Y;

  Status = gBS->LocateProtocol (&gEfiGraphicsOutputProtocolGuid, NULL, (VOID **)&mGop);
  if (EFI_ERROR (Status) || (mGop == NULL)) {
    return EFI_NOT_READY;
  }

  //
  // Mid-gray background.
  //
  Fill (0, 0, FB_WIDTH, FB_HEIGHT, 96, 96, 96);

  //
  // Full-height 20-px black/white ruler stripes.
  //
  for (X = 0; X < FB_WIDTH; X += 20) {
    Fill (X, 0, 10, FB_HEIGHT, 0, 0, 0);
    Fill (X + 10, 0, 10, FB_HEIGHT, 255, 255, 255);
  }

  //
  // RED center marker at cols 536..543.
  //
  Fill (536, 0, 8, FB_HEIGHT, 255, 0, 0);

  //
  // White edge bands: rows 0..19 and 1900..1919.
  //
  Fill (0, 0, FB_WIDTH, 20, 255, 255, 255);
  Fill (0, FB_HEIGHT - 20, FB_WIDTH, 20, 255, 255, 255);

  //
  // Primary color bars, rows 900..999, 6 blocks of 180 px:
  // R G B | Y C M.
  //
  Fill (0,   900, 180, 100, 255, 0,   0);
  Fill (180, 900, 180, 100, 0,   255, 0);
  Fill (360, 900, 180, 100, 0,   0,   255);
  Fill (540, 900, 180, 100, 255, 255, 0);
  Fill (720, 900, 180, 100, 0,   255, 255);
  Fill (900, 900, 180, 100, 255, 0,   255);

  //
  // Reference lines every 100 rows + row numbers every 200 rows.
  //
  for (Y = 100; Y < FB_HEIGHT; Y += 100) {
    Fill (0, Y, FB_WIDTH, 2, 255, 255, 255);
  }

  for (Y = 200; Y < FB_HEIGHT; Y += 200) {
    Num (480, Y + 10, Y, 4, 4);
  }

  //
  // Hold and exercise the redraw paths.  Photo any time after the screen
  // settles; after t=180 s the menu replica is on screen.
  //
  for (Sec = 0; Sec < 240; Sec++) {
    if ((Sec >= 10) && (Sec < 180)) {
      TickDraw (Sec);
    }

    if (Sec == 180) {
      ReplicaDraw ();
    }

    gBS->Stall (1000000);
  }

  return EFI_SUCCESS;
}
