//
//  AArch64 early assembly hooks for the Gree G0245D (msm8996).
//
//  Runs before any C environment exists: no stack, no relocation, no globals.
//  Only do things that cannot wait until PlatformInitialize().
//
//  Copyright (c) 2012-2013, ARM Limited. All rights reserved.
//  SPDX-License-Identifier: BSD-2-Clause-Patent
//

#include <AsmMacroLib.h>

ASM_FUNC (PlatformAssemblyInitialize)
  //
  // The previous boot stage (aboot/LK) has already set up the basic CPU
  // state we need. Nothing to do here.
  //
  //
  // TODO(REA): if bring-up shows the MMU/caches are still enabled at hand-off,
  //   the relevant SCTLR_ELx sanitisation belongs here rather than in C.
  //   Verify against emmc_appsboot.mbn before adding anything.
  //
  ret
