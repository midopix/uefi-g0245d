/** @file
  DiagDrawLib -- render hex numbers straight into the framebuffer.

  Used by DisplayDiagDxe (the scanout dump) and by CrashLogDxe (so a caught
  CPU fault can be photographed instead of being lost to a cold reset).

  Deliberately dumb: no GOP lookups, no console, no allocation.  The caller
  hands in a surface (base pointer, size, stride) and this library pokes
  pixels.  That keeps it usable from an exception handler.

  Copyright (c) 2026, UniXNU contributors. All rights reserved.
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#ifndef DIAG_DRAW_LIB_H_
#define DIAG_DRAW_LIB_H_

#include <Uefi.h>

typedef struct {
  UINT32    *Fb;       // framebuffer base (32bpp, XRGB/BGRX)
  UINTN     Width;     // pixels
  UINTN     Height;    // pixels
  UINTN     Stride;    // pixels per scanline
} DIAG_SURFACE;

#define DIAG_COL_BG      0xFF1A1A1AU
#define DIAG_COL_WHITE   0xFFFFFFFFU
#define DIAG_COL_GREEN   0xFF22CC44U
#define DIAG_COL_YELLOW  0xFF22CCF0U
#define DIAG_COL_RED     0xFF2828E8U

//
// Row geometry shared by the dump and the fault banner.
//
#define DIAG_ROW_Y0      140
#define DIAG_ROW_PITCH   55

VOID
DiagSurfaceInit (
  OUT DIAG_SURFACE  *Surface,
  IN  UINT32        *Fb,
  IN  UINTN         Width,
  IN  UINTN         Height,
  IN  UINTN         Stride
  );

VOID
DiagFillRect (
  IN DIAG_SURFACE  *Surface,
  IN UINTN         X,
  IN UINTN         Y,
  IN UINTN         W,
  IN UINTN         H,
  IN UINT32        Color
  );

VOID
DiagDrawText (
  IN DIAG_SURFACE  *Surface,
  IN UINTN         X,
  IN UINTN         Y,
  IN CONST CHAR8   *Text,
  IN UINT32        Color
  );

/**
  Draw "WIDTH" hex digits of Value (no prefix).
**/
VOID
DiagDrawHex (
  IN DIAG_SURFACE  *Surface,
  IN UINTN         X,
  IN UINTN         Y,
  IN UINT64        Value,
  IN UINTN         Digits,
  IN UINT32        Color
  );

/**
  Draw one dump row: "NN:VVVVVVVV".  ValueValid=FALSE draws red dashes, which
  is what remains on screen if the following MMIO read aborted the CPU.
**/
VOID
DiagDrawRow (
  IN DIAG_SURFACE  *Surface,
  IN UINTN         Index,
  IN UINT32        Value,
  IN BOOLEAN       ValueValid
  );

/**
  Paint the fault banner at the bottom of the screen (leaves the row table
  above untouched): FAR, ELR, ESR.
**/
VOID
DiagPaintFault (
  IN DIAG_SURFACE  *Surface,
  IN UINT64        Far,
  IN UINT64        Elr,
  IN UINT64        Esr
  );

#endif // DIAG_DRAW_LIB_H_
