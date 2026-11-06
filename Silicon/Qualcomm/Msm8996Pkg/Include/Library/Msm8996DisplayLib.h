/** @file
 *
 *  MSM8996 cold display bring-up -- library interface.
 *
 *  Brings the whole MDP5 -> DSI0 -> ft8716 panel chain up from a cold
 *  (SBL1-only) power state:
 *
 *    PMIC rails (PM8994 L2/L14 via SPMI)
 *      -> MMSS GDSCs (mmagic_mdss + mdss) and MDSS clock branches
 *      -> MDP/AXI MMSS RCG2 sources (GPLL0-derived)
 *      -> 14nm DSI PHY (LDO + 5 lanes + vendor D-PHY timing bytes)
 *      -> 14nm DSI PLL (full vco_set_rate port from dsi_pll_14nm.c)
 *      -> DSI host (reset + command DMA panel init + video-mode config)
 *      -> MDP5 pipeline (RGB0 pipe -> LM0 -> CTL0 -> INTF1 @ 0x901000)
 *      -> backlight PMIC GPIO14
 *
 *  All register provenance is documented in the implementation file.
 *
 *  Copyright (c) 2026, UniXNU contributors.
 *
 *  SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 **/

#ifndef __MSM8996_DISPLAY_LIB_H__
#define __MSM8996_DISPLAY_LIB_H__

#include <Uefi.h>

/**
  Bring the display chain up from cold.

  Hot hand-off safe: if the MDSS GDSC reports already-on (a previous boot
  stage left the pipeline running), the call returns EFI_SUCCESS without
  touching anything.

  @retval EFI_SUCCESS            Pipeline running, panel initialised.
  @retval EFI_TIMEOUT            PLL lock / clock halt poll failed.
  @retval others                 PMIC or programming error.
**/
EFI_STATUS
EFIAPI
Msm8996DisplayColdInit (
  VOID
  );

/**
  Framebuffer the MDP5 scanout was last programmed to read from.

  Normally MSM8996_CONT_SPLASH_BASE, but the cold path probes it by painting a
  test pattern and reading the result back, so a board whose bootloader uses a
  different carve-out gets the right address instead of a black screen.

  @return Physical address of the scanout source buffer.
**/
UINT64
EFIAPI
Msm8996DisplayGetFrameBuffer (
  VOID
  );

#define MSM8996_DISPLAY_DEBUG_GROUP_WORDS  24
#define MSM8996_DISPLAY_DEBUG_GROUPS       6

/**
  Read the live display register file into a caller-supplied buffer.

  Group layout (24 words each):

    0: MDP5 pipeline (DISP_INTF_SEL, CTL0, RGB0, INTF1 timing)
    1: DSI0 controller 0x000-0x05c  (words at offsets 0x00, 0x04, ... 0x5c)
    2: DSI0 controller 0x060-0x0bc
    3: DSI0 controller 0x0c0-0x11c
    4: 14nm PLL 0x00-0x3c, then CMN_CLK_CFG0/1, CMN_CTRL_0, CMN_LDO_CNTRL
    5: data lane 0 TIMING_CTRL_4..11, then clock lane TIMING_CTRL_4..11

  Unused words in a group are zero.  Used to render a register dump on the
  panel itself so a single photo reports what the hardware really holds.

  @param[out] Buffer   MSM8996_DISPLAY_DEBUG_GROUPS * ..._GROUP_WORDS UINT32s.
  @param[in]  Groups   Number of groups to fill (clamped implicitly by Buffer).
**/
VOID
EFIAPI
Msm8996DisplayGetDebugSnapshot (
  OUT UINT32 *Buffer,
  IN  UINTN  Groups
  );

#endif /* __MSM8996_DISPLAY_LIB_H__ */
