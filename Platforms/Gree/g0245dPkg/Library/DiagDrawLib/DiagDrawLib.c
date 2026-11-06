/** @file
  DiagDrawLib -- framebuffer hex renderer (see Include/Library/DiagDrawLib.h).

  Copyright (c) 2026, UniXNU contributors. All rights reserved.
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <Uefi.h>
#include <Library/DiagDrawLib.h>

//
// 5x7 font, row-major, MSB = leftmost pixel.  Glyph order:
// '0'..'9', 'A'..'F', ':'.
//
STATIC CONST UINT8  mFont[17][7] = {
  { 0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E },  // 0
  { 0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E },  // 1
  { 0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F },  // 2
  { 0x1F, 0x02, 0x04, 0x02, 0x01, 0x11, 0x0E },  // 3
  { 0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02 },  // 4
  { 0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E },  // 5
  { 0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E },  // 6
  { 0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08 },  // 7
  { 0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E },  // 8
  { 0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C },  // 9
  { 0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11 },  // A
  { 0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E },  // B
  { 0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E },  // C
  { 0x1C, 0x12, 0x11, 0x11, 0x11, 0x12, 0x1C },  // D
  { 0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F },  // E
  { 0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10 },  // F
  { 0x00, 0x04, 0x00, 0x00, 0x00, 0x04, 0x00 },  // :
};

#define GLYPH_SCALE  5
#define GLYPH_ADV    (6 * GLYPH_SCALE)

VOID
DiagSurfaceInit (
  OUT DIAG_SURFACE  *Surface,
  IN  UINT32        *Fb,
  IN  UINTN         Width,
  IN  UINTN         Height,
  IN  UINTN         Stride
  )
{
  Surface->Fb     = Fb;
  Surface->Width  = Width;
  Surface->Height = Height;
  Surface->Stride = Stride;
}

VOID
DiagFillRect (
  IN DIAG_SURFACE  *Surface,
  IN UINTN         X,
  IN UINTN         Y,
  IN UINTN         W,
  IN UINTN         H,
  IN UINT32        Color
  )
{
  UINTN  Row;

  if ((Surface == NULL) || (Surface->Fb == NULL)) {
    return;
  }

  for (Row = 0; Row < H; Row++) {
    UINTN   Col;
    UINT32  *P;

    if ((Y + Row) >= Surface->Height) {
      break;
    }

    P = Surface->Fb + (Y + Row) * Surface->Stride + X;
    for (Col = 0; Col < W; Col++) {
      if ((X + Col) >= Surface->Width) {
        break;
      }

      P[Col] = Color;
    }
  }
}

STATIC
VOID
DrawGlyph (
  IN DIAG_SURFACE  *Surface,
  IN UINTN         X,
  IN UINTN         Y,
  IN CHAR8         Ch,
  IN UINT32        Color
  )
{
  INTN  Gi = -1;
  UINTN Row;

  if ((Ch >= '0') && (Ch <= '9')) {
    Gi = Ch - '0';
  } else if ((Ch >= 'A') && (Ch <= 'F')) {
    Gi = 10 + (Ch - 'A');
  } else if (Ch == ':') {
    Gi = 16;
  }

  if (Gi < 0) {
    return;
  }

  for (Row = 0; Row < 7; Row++) {
    UINTN  Col;
    UINT8  Bits = mFont[Gi][Row];

    for (Col = 0; Col < 5; Col++) {
      if ((Bits & (0x10 >> Col)) != 0) {
        DiagFillRect (Surface, X + Col * GLYPH_SCALE, Y + Row * GLYPH_SCALE,
                      GLYPH_SCALE, GLYPH_SCALE, Color);
      }
    }
  }
}

VOID
DiagDrawText (
  IN DIAG_SURFACE  *Surface,
  IN UINTN         X,
  IN UINTN         Y,
  IN CONST CHAR8   *Text,
  IN UINT32        Color
  )
{
  while ((Text != NULL) && (*Text != 0)) {
    DrawGlyph (Surface, X, Y, *Text++, Color);
    X += GLYPH_ADV;
  }
}

VOID
DiagDrawHex (
  IN DIAG_SURFACE  *Surface,
  IN UINTN         X,
  IN UINTN         Y,
  IN UINT64        Value,
  IN UINTN         Digits,
  IN UINT32        Color
  )
{
  UINTN  I;

  for (I = Digits; I > 0; I--) {
    UINT32  Nib = (UINT32)((Value >> ((I - 1) * 4)) & 0xF);
    CHAR8   Ch  = (Nib < 10) ? (CHAR8)('0' + Nib) : (CHAR8)('A' + Nib - 10);

    DrawGlyph (Surface, X, Y, Ch, Color);
    X += GLYPH_ADV;
  }
}

VOID
DiagDrawRow (
  IN DIAG_SURFACE  *Surface,
  IN UINTN         Index,
  IN UINT32        Value,
  IN BOOLEAN       ValueValid
  )
{
  UINTN  X = 60;
  UINTN  Y = DIAG_ROW_Y0 + Index * DIAG_ROW_PITCH;

  DiagFillRect (Surface, 30, Y + 10, 18, 40, ValueValid ? DIAG_COL_GREEN : DIAG_COL_RED);

  DrawGlyph (Surface, X, Y, (CHAR8)('0' + (Index / 10) % 10), DIAG_COL_YELLOW);
  X += GLYPH_ADV;
  DrawGlyph (Surface, X, Y, (CHAR8)('0' + Index % 10), DIAG_COL_YELLOW);
  X += GLYPH_ADV;
  DrawGlyph (Surface, X, Y, ':', DIAG_COL_YELLOW);
  X += GLYPH_ADV;

  if (ValueValid) {
    DiagDrawHex (Surface, X, Y, Value, 8, DIAG_COL_WHITE);
  } else {
    //
    // Red dashes = "this row was reached, its read never returned".
    //
    DiagFillRect (Surface, X, Y + 14, 8 * GLYPH_ADV - 6, 8, DIAG_COL_RED);
  }
}

VOID
DiagPaintFault (
  IN DIAG_SURFACE  *Surface,
  IN UINT64        Far,
  IN UINT64        Elr,
  IN UINT64        Esr
  )
{
  UINTN  Y0 = DIAG_ROW_Y0 + 28 * DIAG_ROW_PITCH;

  //
  // Red band behind the fault banner so it cannot be mistaken for a normal
  // row.  The table above stays readable.
  //
  DiagFillRect (Surface, 0, Y0, Surface->Width, 260, DIAG_COL_RED);

  DiagDrawText (Surface, 60, Y0 + 20, "FAR", DIAG_COL_WHITE);
  DiagDrawHex (Surface, 60 + 4 * GLYPH_ADV, Y0 + 20, Far, 16, DIAG_COL_WHITE);

  DiagDrawText (Surface, 60, Y0 + 100, "ELR", DIAG_COL_WHITE);
  DiagDrawHex (Surface, 60 + 4 * GLYPH_ADV, Y0 + 100, Elr, 16, DIAG_COL_WHITE);

  DiagDrawText (Surface, 60, Y0 + 180, "ESR", DIAG_COL_WHITE);
  DiagDrawHex (Surface, 60 + 4 * GLYPH_ADV, Y0 + 180, Esr, 16, DIAG_COL_WHITE);
}
