/** @file
  BootMarkDxe #5 - boot progress marker, palette index 5 (yellow).
  Drawn right AFTER SmemDxe to bisect the vendor-foundation HANG.
  See BootMark.h for the shared body and the design notes.

  Copyright (c) 2026, UniXNU contributors.
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#define MARK_INDEX  5
#include "BootMark.h"
