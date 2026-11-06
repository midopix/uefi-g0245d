/** @file
  BootMarkDxe - boot progress marker (shared body).

  Included by BootMark0.c .. BootMark3.c, each of which defines MARK_INDEX
  before including this header.  The colour is taken from a fixed palette so a
  boot photo identifies *which* markers ran even if the on-panel position is
  mirrored/flipped by the display pipeline.

  Design notes (learned the hard way, see PITFALLS.md):
   - Do NOT rely on -DMARK_INDEX via INF [BuildOptions]: this project's build
     chain drops INF-level [BuildOptions], which silently produced four
     byte-identical modules in v84 (all drawing one overlapping square).
   - Do NOT call gBS->UnloadImage(ImageHandle) from inside the entry point:
     it frees the pages the CPU is executing from.  That faulted the v84 boot
     right after the first square.
   - Leave a hole at GOP x<90 for CrashLogDxe's green "hook alive" square.

  Copyright (c) 2026, UniXNU contributors.
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <Uefi.h>
#include <Protocol/GraphicsOutput.h>

#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiDriverEntryPoint.h>
#include <Library/DebugLib.h>

#ifndef MARK_INDEX
#error "MARK_INDEX must be defined by the including .c file"
#endif

//
// Palette: orange, red, magenta, cyan, blue, yellow, lime, white.
// Indices 5/6/7 are the v89 vendor-interleave markers used to bisect which
// of the four vendor foundation modules HANGS (the dispatcher is
// single-threaded, so a non-returning entry point blocks everything after it).
//
STATIC CONST UINT8  mPalette[8][3] = {
  { 0xFF, 0x8C, 0x00 },   // 0 orange  (SimpleFb after)
  { 0xDC, 0x00, 0x00 },   // 1 red     (UfsHost after)
  { 0xFF, 0x00, 0xFF },   // 2 magenta (GraphicsConsole after)
  { 0x00, 0xDC, 0xDC },   // 3 cyan    (Fat after)
  { 0x00, 0x00, 0xFF },   // 4 blue    (PlatformInfo after)
  { 0xFF, 0xFF, 0x00 },   // 5 yellow  (SmemDxe after)
  { 0x00, 0xFF, 0x00 },   // 6 lime    (DALSys after)
  { 0xFF, 0xFF, 0xFF },   // 7 white   (SPMI after)
};

EFI_STATUS
EFIAPI
BootMarkDxeEntryPoint (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_GRAPHICS_OUTPUT_PROTOCOL  *Gop;

  if (!EFI_ERROR (gBS->LocateProtocol (&gEfiGraphicsOutputProtocolGuid, NULL, (VOID **)&Gop)) && (Gop != NULL)) {
    EFI_GRAPHICS_OUTPUT_BLT_PIXEL  Color;
    UINT32                         X;

    Color.Red      = mPalette[MARK_INDEX][0];
    Color.Green    = mPalette[MARK_INDEX][1];
    Color.Blue     = mPalette[MARK_INDEX][2];
    Color.Reserved = 0;

    //
    // 60x100 square, one slot per marker (140px pitch), starting at x=100 so
    // that CrashLogDxe's 80x80 green square at (0,0) stays visible.
    //
    X = 100 + MARK_INDEX * 140;
    Gop->Blt (Gop, &Color, EfiBltVideoFill, 0, 0, X, 10, 60, 100, 0);
  } else {
    DEBUG ((DEBUG_WARN, "BootMarkDxe[%d]: no GOP, marker skipped\n", (UINT32)MARK_INDEX));
  }

  return EFI_SUCCESS;
}
