/** @file
 *
 *  MSM8996 cold display bring-up implementation.
 *
 *  ============================================================================
 *  PROVENANCE
 *  ============================================================================
 *  Every offset and sequence below was extracted from Linux v5.10 mainline
 *  drivers (fetched into tools/kref/ and tools/linux-src during this port):
 *
 *    * MMSS CC GDSC / CBCR / RCG2 offsets ........ mmcc-msm8996.c
 *    * 14nm PLL parameter math + commit sequence . dsi_pll_14nm.c
 *    * 14nm PHY lane programming + reset ......... dsi_phy_14nm.c (v5.10)
 *    * DSI host ctrl/timing/command-DMA .......... dsi_host.c
 *    * MDP5 pipeline registers ................... mdp5.xml.h (resolved for
 *      the g0245d DTB by tools/gen_display_regs.py: CTL0=0x2000, LM0=0x45000,
 *      PIPE_RGB0=0x15000, INTF1=0x6B800, relative to MDP base 0x901000)
 *    * Panel data (name, geometry, on-commands, reset sequence, D-PHY timing
 *      bytes, t-clk-post/pre) .................... this device's own DTB
 *      (qcom,mdss_dsi_ft8716_1080p_video node, decompiled g0245d_main.dts)
 *
 *  PANEL FACTS (g0245d, ft8716 1080p video mode)
 *  ------------------------------------------------------------
 *    1080x1920, RGB888 (24 bpp), 4 lanes, burst mode, hsync pulse.
 *    hfp=32 hbp=32 hsw=4;  vfp=16 vbp=16 vsw=2;  60 Hz.
 *      htotal = 1080+32+32+4  = 1148
 *      vtotal = 1920+16+16+2  = 1954
 *      pclk   = 1148 * 1954 * 60 = 134,708,352 Hz
 *    4 lanes / 24 bpp => lane bitclk = pclk * 6 = 808,250,112 Hz
 *      byteclk = bitclk/8 = 101,031,264 Hz
 *    VCO = bitclk * n1 with n1 = 2  => 1,616,500,224 Hz (in 1.3-2.6 GHz)
 *      pixel  = vco / n1 / 2 / n2 with n2 = 3  => back to pclk.
 *      PLL CLK_CFG0 = n1 | (n2 << 4) = 0x32.
 *
 *  D-PHY lane timing uses the vendor-tuned bytes straight from the DTB
 *  (qcom,mdss-dsi-panel-timings-8996), which map 1:1 onto TIMING_CTRL_4..11.
 *
 *  Copyright (c) 2026, UniXNU contributors.
 *
 *  SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 **/

#include <Uefi.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/IoLib.h>
#include <Library/Msm8996DisplayLib.h>
#include <Library/Msm8996PmicLib.h>
#include <Library/TimerLib.h>

#include <Msm8996Regs.h>
#include <Msm8996DsiRegs.h>

//==============================================================================
// Panel geometry / clocking constants (see provenance block above)
//==============================================================================

#define PANEL_WIDTH              1080
#define PANEL_HEIGHT             1920
#define PANEL_HFP                32
#define PANEL_HBP                32
#define PANEL_HSW                4
#define PANEL_VFP                16
#define PANEL_VBP                16
#define PANEL_VSW                2

#define PANEL_HTOTAL             (PANEL_WIDTH + PANEL_HFP + PANEL_HBP + PANEL_HSW)
#define PANEL_VTOTAL             (PANEL_HEIGHT + PANEL_VFP + PANEL_VBP + PANEL_VSW)
#define PANEL_PCLK_HZ            (PANEL_HTOTAL * PANEL_VTOTAL * 60ULL)
#define PANEL_BITCLK_HZ          (PANEL_PCLK_HZ * 6ULL)   // 24 bpp / 4 lanes
#define PANEL_BYTECLK_HZ         (PANEL_BITCLK_HZ / 8ULL)
#define PLL_N1                   2ULL
#define PLL_N2                   3ULL
#define PANEL_VCO_HZ             (PANEL_BITCLK_HZ * PLL_N1)

// LANES as the dsi_ctrl_config lane-enable count (4 => LANE0..LANE3).
#define PANEL_LANES              4

//==============================================================================
// 14nm DSI PHY / PLL register offsets (dsi.xml.h REG_DSI_14nm_PHY_*)
//==============================================================================

// Common block (DSI0 PHY base 0x994400).
#define D14_CMN_CLK_CFG0         0x10
#define D14_CMN_CLK_CFG1         0x14
#define D14_CMN_GLBL_TEST_CTRL   0x18
#define D14_CMN_CTRL_0           0x1C
#define D14_CMN_CTRL_1           0x20
#define D14_CMN_PLL_CNTRL        0x48
#define D14_CMN_LDO_CNTRL        0x4C

// PLL block (DSI0 PLL base 0x994A00).
#define D14_PLL_IE_TRIM              0x00
#define D14_PLL_IP_TRIM              0x04
#define D14_PLL_IPTAT_TRIM           0x10
#define D14_PLL_CLKBUFLR_EN          0x1C
#define D14_PLL_SYSCLK_EN_RESET      0x28
#define D14_PLL_RESETSM_CNTRL        0x2C
#define D14_PLL_RESETSM_CNTRL2       0x30
#define D14_PLL_RESETSM_CNTRL5       0x3C
#define D14_PLL_KVCO_DIV_REF1        0x40
#define D14_PLL_KVCO_DIV_REF2        0x44
#define D14_PLL_KVCO_COUNT1          0x48
#define D14_PLL_KVCO_COUNT2          0x4C
#define D14_PLL_VREF_CFG1            0x5C
#define D14_PLL_VCO_DIV_REF1         0x6C
#define D14_PLL_VCO_DIV_REF2         0x70
#define D14_PLL_VCO_COUNT1           0x74
#define D14_PLL_VCO_COUNT2           0x78
#define D14_PLL_PLLLOCK_CMP1         0x7C
#define D14_PLL_PLLLOCK_CMP2         0x80
#define D14_PLL_PLLLOCK_CMP3         0x84
#define D14_PLL_PLLLOCK_CMP_EN       0x88
#define D14_PLL_DEC_START            0x90
#define D14_PLL_SSC_EN_CENTER        0x94
#define D14_PLL_SSC_ADJ_PER1         0x98
#define D14_PLL_SSC_ADJ_PER2         0x9C
#define D14_PLL_SSC_PER1             0xA0
#define D14_PLL_SSC_PER2             0xA4
#define D14_PLL_SSC_STEP_SIZE1       0xA8
#define D14_PLL_SSC_STEP_SIZE2       0xAC
#define D14_PLL_DIV_FRAC_START1      0xB4
#define D14_PLL_DIV_FRAC_START2      0xB8
#define D14_PLL_DIV_FRAC_START3      0xBC
#define D14_PLL_TXCLK_EN             0xC0
#define D14_PLL_PLL_CRCTRL           0xC4
#define D14_PLL_RESET_SM_READY_STATUS 0xCC
#define D14_PLL_PLL_MISC1            0xE8
#define D14_PLL_CP_SET_CUR           0xF0
#define D14_PLL_PLL_ICPMSET          0xF4
#define D14_PLL_PLL_ICPCSET          0xF8
#define D14_PLL_PLL_ICP_SET          0xFC
#define D14_PLL_PLL_LPF1             0x100
#define D14_PLL_PLL_LPF2_POSTDIV     0x104
#define D14_PLL_PLL_BANDGAP          0x108

// Lane blocks (DSI0 lane base 0x994700 + 0x80 * lane, CK lane = idx 4).
#define D14_LN_CFG0                 0x00
#define D14_LN_CFG1                 0x04
#define D14_LN_CFG2                 0x08
#define D14_LN_CFG3                 0x0C
#define D14_LN_TEST_DATAPATH        0x10
#define D14_LN_TEST_STR             0x14
#define D14_LN_TIMING_CTRL_4        0x18
#define D14_LN_TIMING_CTRL_5        0x1C
#define D14_LN_TIMING_CTRL_6        0x20
#define D14_LN_TIMING_CTRL_7        0x24
#define D14_LN_TIMING_CTRL_8        0x28
#define D14_LN_TIMING_CTRL_9        0x2C
#define D14_LN_TIMING_CTRL_10       0x30
#define D14_LN_TIMING_CTRL_11       0x34
#define D14_LN_STRENGTH_CTRL_0      0x38
#define D14_LN_STRENGTH_CTRL_1      0x3C
#define D14_LN_VREG_CNTRL           0x64

//==============================================================================
// DSI host register offset corrections
//==============================================================================
//
// DSI host block (0x994000) offsets for the 6G / HW rev >= 1.2 controller,
// taken from the vendor host driver (tools/upstream/mdss_dsi_host.c, which is
// the 4.4/sdm845-era "6G" generation and uses the same register map as
// MSM8996's 1.3 controller).
//
// The msm-upstream dsi.xml.h table that generated Msm8996DsiRegs.h numbers the
// host block for the 4.14+ driver, which re-laid out several registers.  Using
// those offsets here silently wrote the wrong registers:
//
//   register            xml.h (wrong)   vendor / hardware   what xml.h address
//                                                           actually is
//   DSI_CTRL            0x000           0x004               DSI_6G_HW_VERSION
//   DSI_STATUS          0x004           0x008               DSI_CTRL
//   DSI_FIFO_STATUS     0x008           0x00c               DSI_STATUS0
//   DSI_VID_CFG0        0x00c           0x010               DSI_FIFO_STATUS
//   DSI_VID_CFG1        0x01c           0x020               DSI_ACTIVE_H
//   DSI_ACTIVE_H        0x020           0x024               DSI_ACTIVE_V
//   DSI_ACTIVE_V        0x024           0x028               DSI_TOTAL
//   DSI_TOTAL           0x028           0x02c               DSI_ACTIVE_HSYNC
//   DSI_ACTIVE_HSYNC    0x02c           0x030               VSYNC_HPOS
//   VSYNC_HPOS          0x030           0x034               VSYNC_VPOS
//   VSYNC_VPOS          0x034           0x038               CMD_DMA_CTRL
//   CMD_DMA_CTRL        0x038           0x03c               CMD_CFG0
//   TRIG_CTRL           0x080           0x084               (unused)
//   TRIG_DMA            0x08c           0x090               (unused)
//   LANE_CTRL           0x0a8           0x0ac               LANE_SWAP_CTRL
//   LANE_SWAP_CTRL      0x0ac           0x0b0               DLN0_PHY_ERR
//   HS_TIMER_CTRL       0x0b8           0x0bc               TIMEOUT_STATUS
//   TIMEOUT_STATUS      0x0bc           0x0c0               CLKOUT_TIMING_CTRL
//   CLKOUT_TIMING_CTRL  0x0c0           0x0c4               EOT_PACKET_CTRL
//   EOT_PACKET_CTRL     0x0c8           0x0cc               (unused)
//   CLK_CTRL            0x118           0x11c               CLK_STATUS
//   INTR_CTRL           0x10c           0x110               ERR_INT_MASK0
//
// So every write the bring-up code thought it was making landed one register
// away from the intended target: "enable video mode" wrote the PHY video
// timing register, "set t-clk-pre/post" wrote the EOT control register (0),
// the clock gates were never enabled, and so on.  The controller kept running
// on whatever aboot had left in the real registers, which is exactly the
// "panel frames the stream with its own timing generator" artefact chain that
// v33..v50 tripped over from several different angles.
//
//------------------------------------------------------------------------------
// DSI host register offsets -- CORRECTED (v54).
//
// These were uniformly +4 off (DSI_REG_CTRL was 0x0004, ACTIVE_H was 0x0024, ...),
// which is why "the panel samples a third of each scanline and repeats it three
// times" survived v33..v53. A previous attempt reasoned that DSI_CTRL must be
// 0x0004 because 0x0000 is "the hardware-version register" and shifted the whole
// table by +4. That reasoning was wrong: in the generated msm8996 layout
// (Msm8996DsiRegs.h, from Linux drivers/gpu/drm/msm/dsi/dsi.xml.h)
// REG_DSI_CTRL and REG_DSI_6G_HW_VERSION are BOTH at 0x0000 -- the DSI register
// file simply puts CTRL first:
//
//     REG_DSI_CTRL    0x0000   REG_DSI_STATUS0          0x0004
//     REG_DSI_VID_CFG0 0x000c  REG_DSI_ACTIVE_H         0x0020
//
// With the +4 table every write landed one register late: "enable video mode"
// became a write to STATUS0 (read-only, ignored), the video timing went into
// VID_CFG0/VID_CFG1 and so on, DSI_CTRL kept whatever aboot's command-mode
// splash had programmed, and the panel kept framing the stream with its own
// timing generator -- the exact 1/3 + triple-repeat artefact.
//
// Values below are taken verbatim from REG_DSI_* in Msm8996DsiRegs.h.
//------------------------------------------------------------------------------
#define DSI_REG_CTRL               0x0000
#define DSI_REG_STATUS             0x0004
#define DSI_REG_FIFO_STATUS        0x0008
#define DSI_REG_VID_CFG0           0x000C
#define DSI_REG_VID_CFG1           0x001C
#define DSI_REG_ACTIVE_H           0x0020
#define DSI_REG_ACTIVE_V           0x0024
#define DSI_REG_TOTAL              0x0028
#define DSI_REG_ACTIVE_HSYNC       0x002C
#define DSI_REG_ACTIVE_VSYNC_HPOS  0x0030
#define DSI_REG_ACTIVE_VSYNC_VPOS  0x0034
#define DSI_REG_CMD_DMA_CTRL       0x0038
#define DSI_REG_CMD_CFG0           0x003C
#define DSI_REG_CMD_CFG1           0x0040
#define DSI_REG_DMA_BASE           0x0044
#define DSI_REG_DMA_LEN            0x0048
#define DSI_REG_TRIG_CTRL          0x0080
#define DSI_REG_TRIG_DMA           0x008C
#define DSI_REG_LANE_STATUS        0x00A4
#define DSI_REG_LANE_CTRL          0x00A8
#define DSI_REG_LANE_SWAP_CTRL     0x00AC
#define DSI_REG_HS_TIMER_CTRL      0x00B8
#define DSI_REG_TIMEOUT_STATUS     0x00BC
#define DSI_REG_CLKOUT_TIMING_CTRL 0x00C0
#define DSI_REG_EOT_PACKET_CTRL    0x00C8
#define DSI_REG_ERR_INT_MASK0      0x0108
#define DSI_REG_INTR_CTRL          0x010C
#define DSI_REG_RESET              0x0114
#define DSI_REG_CLK_CTRL           0x0118

//==============================================================================
// MDP5 register offsets (already resolved for this DTB; base 0x901000)
//==============================================================================

#define MDP_REG(x)              (MSM8996_MDP_BASE + MSM8996_MDP_REG_OFFSET + (x))

// Top level.
#define MDP_DISP_INTF_SEL       MDP_REG (0x00000004)
#define MDP5_INTF_SEL_INTF1_DSI (0x1 << 8)        // INTF1 field, INTF_DSI = 1

//
// CTL layer stage encoding (mdp5_ctl.c enum mdp_mixer_stage_id):
//   STAGE0 = 0, STAGE1 = 1, ... STAGE6 = 6, STAGE_BASE = 7, STAGE_UNUSED = 8.
// STAGE_BASE is what a plane with the lowest zpos gets, and mdp5 doesn't
// support a plane with zpos 0 (it's reserved for the background/border color),
// so a full-screen opaque plane sits on STAGE_BASE -- not STAGE0.
//
#define MDP_STAGE_BASE          7

// CTL0 (base 0x2000).
#define MDP_CTL0_LAYER          MDP_REG (0x00002000)   // RGB0 = stage << 9
#define MDP_CTL0_LAYER_EXT      MDP_REG (0x00002010)   // per-stage EXT/CSC flags
#define MDP_CTL0_OP_MODE        MDP_REG (0x00002014)   // INTF_NUM = 2 << 4
#define MDP_CTL0_FLUSH          MDP_REG (0x00002018)
#define MDP_FLUSH_LM0           0x00000040
#define MDP_FLUSH_RGB0          0x00000008
#define MDP_FLUSH_CTL           0x00020000

// LM0 (base 0x45000).
#define MDP_LM0_COLOR_OUT       MDP_REG (0x00045000)   // STAGE0_FG = 1
#define MDP_LM0_OUT_SIZE        MDP_REG (0x00045004)
#define MDP_LM0_BLEND_OP_MODE   MDP_REG (0x00045020)   // 0 = opaque base layer

// SSPP_RGB0 (base 0x15000).
#define MDP_RGB0_SRC_SIZE       MDP_REG (0x00015000)   // height<<16 | width
#define MDP_RGB0_SRC_IMG_SIZE   MDP_REG (0x00015004)
#define MDP_RGB0_SRC_XY         MDP_REG (0x00015008)
#define MDP_RGB0_OUT_SIZE       MDP_REG (0x0001500C)
#define MDP_RGB0_OUT_XY         MDP_REG (0x00015010)
#define MDP_RGB0_SRC0_ADDR      MDP_REG (0x00015014)
#define MDP_RGB0_SRC_STRIDE_A   MDP_REG (0x00015024)
#define MDP_RGB0_SRC_FORMAT     MDP_REG (0x00015030)
#define MDP_RGB0_SRC_UNPACK     MDP_REG (0x00015034)
#define MDP_RGB0_SRC_OP_MODE    MDP_REG (0x00015038)
#define MDP_RGB0_OP_MODE        MDP_REG (0x00015200)
#define MDP_RGB0_SCALE_CONFIG   MDP_REG (0x00015204)

//
// Pipe source format, upstream mdp5_plane.c + mdp_format.c (XRGB8888 entry):
//   G_BPC[1:0]=3, B_BPC[3:2]=3, R_BPC[5:4]=3, A_BPC[7:6]=0,
//   CPP[10:9]=cpp-1=3 (4 bytes per pixel), ROT90[11]=0,
//   UNPACK_COUNT[13:12]=cnt-1=3 (4 elements), UNPACK_TIGHT[17]=1,
//   FETCH_TYPE[20:19]=0 (interleaved), CHROMA_SAMP[24:23]=0.
//
// v33..v43 wrote (4 << 9) for CPP: bit 9-10 = 0 and bit 11 = 1, i.e. CPP = 0
// (nonsense) AND ROT90 = 1 (the pipe was told to rotate the framebuffer by
// 90 degrees).  The upstream value is 0x0002363F.
//
#define PIPE_FMT_RGB888                                                \
  ((3 << 0) /* G_BPC = 8bpc */  | (3 << 2) /* B_BPC */  |               \
   (3 << 4) /* R_BPC */         | (3 << 9) /* CPP = 4 bytes/px */ |      \
   (3 << 12) /* UNPACK_COUNT = 4 */ | 0x00020000 /* UNPACK_TIGHT */)

// Unpack elements, upstream XRGB8888: ELEM0=1, ELEM1=0, ELEM2=2, ELEM3=3
// (one byte per element, shifted by 8).  v33..v43 used 0x00010203, which
// permutes R and X and is why the panel showed no colour at all.
#define PIPE_UNPACK_RGB888  0x03020001

// INTF1 (base 0x6B800).
#define MDP_INTF1_TIMING_EN     MDP_REG (0x0006B800)
#define MDP_INTF1_HSYNC_CTL     MDP_REG (0x0006B808)   // width | start<<16
#define MDP_INTF1_VSYNC_PERIOD  MDP_REG (0x0006B80C)
#define MDP_INTF1_VSYNC_LEN     MDP_REG (0x0006B814)
#define MDP_INTF1_DISPLAY_VSTART MDP_REG (0x0006B81C)
#define MDP_INTF1_DISPLAY_VEND  MDP_REG (0x0006B824)
#define MDP_INTF1_DISPLAY_HCTL  MDP_REG (0x0006B83C)   // start | end<<16
#define MDP_INTF1_UNDERFLOW     MDP_REG (0x0006B848)
#define MDP_INTF1_HSYNC_SKEW    MDP_REG (0x0006B84C)
#define MDP_INTF1_POLARITY      MDP_REG (0x0006B850)
#define MDP_INTF1_PANEL_FORMAT  MDP_REG (0x0006B890)   // 0x213F = RGB888 8bpc
#define MDP_INTF1_ACTIVE_HCTL   MDP_REG (0x0006B840)
#define MDP_INTF1_BORDER_COLOR  MDP_REG (0x0006B844)
#define MDP_INTF1_ACTIVE_VSTART MDP_REG (0x0006B82C)
#define MDP_INTF1_ACTIVE_VEND   MDP_REG (0x0006B834)
#define MDP_INTF1_FRAME_LINE_COUNT_EN MDP_REG (0x0006B8A8)

//==============================================================================
// DSI host video-mode field encodings (dsi_host.c enums, v5.10)
//==============================================================================

#define TRAFFIC_MODE_BURST      2
#define VID_DST_FORMAT_RGB888   3
#define TRIGGER_NONE            0
#define TRIGGER_SW              1

#define DSI_VID_CFG0_BUILD \
  (0x10000000 |                        /* PULSE_MODE_HSA_HE (hsync pulse) */ \
   0x00008000 |                        /* EOF_BLLP_POWER_STOP (always)   */ \
   0x00001000 |                        /* BLLP_POWER_STOP (always)       */ \
   ((TRAFFIC_MODE_BURST) << 8) |       /* burst mode                     */ \
   ((VID_DST_FORMAT_RGB888) << 4) |    /* RGB888                         */ \
   (0))                                /* virtual channel 0              */

#define DSI_CLK_CTRL_ENABLE_CLKS \
  (0x00000001 | 0x00000002 | 0x00000004 | 0x00000008 | \
   0x00000010 | 0x00000020 | 0x00000200)

//==============================================================================
// Command DMA scratch (inside cont_splash carve-out, past the 8 MiB FB)
//==============================================================================

#define DSI_CMD_DMA_SCRATCH     (MSM8996_CONT_SPLASH_BASE + 0x02000000)
#define DSI_CMD_DMA_SCRATCH_SZ  0x10000

//==============================================================================
// Scanout source buffer
//==============================================================================
//
// The address the MDP5 SSPP_RGB0 will be pointed at.  Defaults to the
// cont_splash_mem carve-out this device's DTB declares, but is re-probed at
// run time (see "Scanout source probe" below) and published through
// Msm8996DisplayGetFrameBuffer() so DisplayDxe's GOP describes the buffer the
// hardware really scans -- writing pixels to a buffer nobody scans is
// indistinguishable from "the display driver does nothing".
//
STATIC UINT64  mScanoutBase = MSM8996_CONT_SPLASH_BASE;

//
// Set once Mdp5Configure() has pointed SSPP_RGB0 at mScanoutBase.  After that
// the live SRC0_ADDR only reflects *our* choice, so re-probing it on a later
// call (hot hand-off path runs Mdp5Configure again) would ratify our own
// value instead of recovering aboot's.
//
STATIC BOOLEAN  mScanoutProgrammed = FALSE;

STATIC
UINT64
Mdp5ProbeScanoutBase (
  VOID
  );

//==============================================================================
// ft8716 on-commands -- decoded from the DTB property
// qcom,mdss-dsi-on-command (168 bytes, 18 packets, 7-byte DT header
// [dtype,last,vc,ack,wait,dlen_hi,dlen_lo] + payload). Decoded 2026-10-27.
//==============================================================================

typedef struct {
  UINT8     DataType;   // 0x05 DCS short, 0x15 generic short, 0x39 long
  UINT8     WaitMs;
  UINT8     Payload[4];
  UINT8     Len;
} DSI_PANEL_CMD;

STATIC CONST DSI_PANEL_CMD mPanelOnCommands[] = {
  // dt     wait  payload                          len
  { 0x05, 120,  { 0x11, 0x00, 0, 0 }, 2 },   // exit sleep, wait 120 ms
  { 0x15,   1,  { 0x53, 0x2c, 0, 0 }, 2 },   // 0x53 = 0x2c (backlight ctl)
  { 0x05,   5,  { 0x29, 0x00, 0, 0 }, 2 },   // display on, wait 5 ms
  { 0x15,   1,  { 0x55, 0x01, 0, 0 }, 2 },
  { 0x15,   0,  { 0x00, 0x80, 0, 0 }, 2 },
  { 0x39,   0,  { 0xff, 0x87, 0x16, 0x01 }, 4 },
  { 0x15,   0,  { 0x00, 0x80, 0, 0 }, 2 },
  { 0x39,   0,  { 0xff, 0x87, 0x16, 0x00 }, 3 },
  { 0x15,   0,  { 0x00, 0xb0, 0, 0 }, 2 },
  { 0x15,   0,  { 0xca, 0x03, 0, 0 }, 2 },
  { 0x15,   0,  { 0x00, 0xb2, 0, 0 }, 2 },
  { 0x15,   0,  { 0xca, 0x04, 0, 0 }, 2 },
  { 0x15,   0,  { 0x00, 0xb5, 0, 0 }, 2 },
  { 0x15,   0,  { 0xca, 0x03, 0, 0 }, 2 },
  { 0x15,   0,  { 0x00, 0x00, 0, 0 }, 2 },
  { 0x39,   0,  { 0xff, 0x00, 0x00, 0x00 }, 4 },
  { 0x15,   0,  { 0x00, 0x80, 0, 0 }, 2 },
  { 0x39,   0,  { 0xff, 0x00, 0x00, 0x00 }, 3 },
};

//==============================================================================
// Vendor D-PHY timing bytes (DTB qcom,mdss-dsi-panel-timings-8996).
// Per lane: {TIMING_CTRL_4..11} = {hs_exit, hs_zero, hs_prepare, hs_trail,
//                                   hs_rqst, ta_go|ta_sure<<4, ta_get, trig3}.
// First four entries are the data lanes, the fifth is the clock lane.
//==============================================================================

STATIC CONST UINT8 mLaneTiming[5][8] = {
  { 0x23, 0x1f, 0x07, 0x09, 0x05, 0x03, 0x04, 0xa0 },
  { 0x23, 0x1f, 0x07, 0x09, 0x05, 0x03, 0x04, 0xa0 },
  { 0x23, 0x1f, 0x07, 0x09, 0x05, 0x03, 0x04, 0xa0 },
  { 0x23, 0x1f, 0x07, 0x09, 0x05, 0x03, 0x04, 0xa0 },
  { 0x23, 0x19, 0x08, 0x09, 0x05, 0x03, 0x04, 0xa0 },  // clock lane
};

// DTB qcom,mdss-dsi-t-clk-post = 0x13, t-clk-pre = 0x2e.
#define T_CLK_POST   0x13
#define T_CLK_PRE    0x2e

//==============================================================================
// Small helpers
//==============================================================================

STATIC
VOID
DsiPoll (
  IN  UINTN   Address,
  IN  UINT32  Mask,
  IN  UINT32  Expected,
  IN  UINT32  MaxUs
  )
{
  UINT32  Elapsed = 0;

  while (Elapsed < MaxUs) {
    if ((MmioRead32 (Address) & Mask) == Expected) {
      return;
    }

    MicroSecondDelay (10);
    Elapsed += 10;
  }

  DEBUG ((
    DEBUG_ERROR,
    "%a: poll timeout @0x%lx mask 0x%x\n",
    gEfiCallerBaseName, (UINT64)Address, Mask
    ));
}

STATIC
VOID
Rcg2Set (
  IN UINTN  CmdRcgr,
  IN UINT32 Cfg
  )
{
  UINTN  Base = MSM8996_MMSS_CC_BASE + CmdRcgr;

  MmioWrite32 (Base + RCG2_CMD_OFFSET, RCG2_CMD_UPDATE);
  MmioWrite32 (Base + RCG2_CFG_OFFSET, Cfg);
  MmioWrite32 (Base + RCG2_CMD_OFFSET, RCG2_CMD_UPDATE | RCG2_CMD_ROOT_EN);
}

STATIC
VOID
BranchEnable (
  IN UINTN  CbcrOffset
  )
{
  UINTN  Address = MSM8996_MMSS_CC_BASE + CbcrOffset;

  MmioOr32 (Address, 0x1);
  DsiPoll (Address, CBCR_CLK_OFF, 0, 200000);
}

STATIC
VOID
GdscEnable (
  IN UINTN  GdscrOffset
  )
{
  UINTN  Address = MSM8996_MMSS_CC_BASE + GdscrOffset;

  if ((MmioRead32 (Address) & GDSC_POWER_ON_STATUS) != 0) {
    return;                                   // already on (hot hand-off)
  }

  MmioAnd32 (Address, ~0x1);                  // clear SW_COLLAPSE
  DsiPoll (Address, GDSC_POWER_ON_STATUS, GDSC_POWER_ON_STATUS, 200000);
}

//==============================================================================
// MMSS power + clocks
//==============================================================================

STATIC
EFI_STATUS
MmssPowerAndClocks (
  VOID
  )
{
  //
  // Two GDSCs: mmagic_mdss (AXI island) then mdss (core). mmcc-msm8996.c
  // gdsc_mdss cxcs {0x2310, 0x231c} are enabled as branches below.
  //
  GdscEnable (MMSS_MMAGIC_MDSS_GDSCR);
  GdscEnable (MMSS_MDSS_GDSCR);

  //
  // Global branches + MDSS branches. Order: island AXI/NOC first, then the
  // AHB that reaches the MDP register space, then the pipeline clocks.
  //
  BranchEnable (MMSS_MMAGIC_MDSS_AXI_CBCR);
  BranchEnable (MMSS_MMAGIC_MDSS_NOC_CFG_AHB_CBCR);
  BranchEnable (MMSS_MDSS_AHB_CBCR);

  //
  // RCG2 sources that do NOT depend on the DSI PLL:
  //   axi  <- GPLL0 /2 = 300 MHz (map value 5, div 1)
  //   mdp  <- GPLL0 /2 = 300 MHz (map value 5, div 1)
  //   esc0 <- XO 19.2 MHz       (map value 0, div 0)
  //
  Rcg2Set (MMSS_AXI_CLK_SRC_CMD_RCGR, (5 << 8) | 0x1);
  Rcg2Set (MMSS_MDP_CLK_SRC_CMD_RCGR, (5 << 8) | 0x1);
  Rcg2Set (MMSS_ESC0_CLK_SRC_CMD_RCGR, 0x00000000);

  BranchEnable (MMSS_MDSS_AXI_CBCR);
  BranchEnable (MMSS_MDSS_MDP_CBCR);
  BranchEnable (MMSS_MDSS_VSYNC_CBCR);
  BranchEnable (MMSS_MDSS_ESC0_CBCR);

  return EFI_SUCCESS;
}

//==============================================================================
// 14nm PLL (port of dsi_pll_14nm.c vco_set_rate + db_commit + enable_seq)
//==============================================================================

typedef struct {
  UINT32  DecStart;
  UINT32  DivFracStart;
  UINT32  SscPeriod;
  UINT32  SscStepSize;
  UINT32  PlllockCmp;
  UINT32  VcoDivRef;
  UINT32  VcoCount;
  UINT32  KvcoDivRef;
  UINT32  KvcoCount;
  UINT32  PllPostdiv;
} DSI_PLL_PARAMS;

STATIC
VOID
PllInputInitFixed (
  OUT DSI_PLL_PARAMS *P
  )
{
  P->PllPostdiv = 1;    // DSI_PLL_DEFAULT_VCO_POSTDIV (tx_band = /1)
}

/**
  pll_14nm_dec_frac_calc: dec_start / div_frac_start / plllock_cmp.
**/
STATIC
VOID
PllDecFracCalc (
  IN  UINT64          VcoRate,
  IN  UINT64          Fref,
  OUT DSI_PLL_PARAMS  *P
  )
{
  UINT64  Multiplier  = 1ULL << 20;
  UINT64  DecStartMul;
  UINT64  DecStart;
  UINT64  DivFrac;
  UINT64  CompVal;

  DecStartMul = (VcoRate * Multiplier) / Fref;
  DivFrac     = DecStartMul % Multiplier;
  DecStart    = DecStartMul / Multiplier;

  P->DecStart    = (UINT32)DecStart;
  P->DivFracStart = (UINT32)DivFrac;

  // plllock_cnt == 1 => duration 256.
  CompVal = 256 * DecStartMul;
  CompVal = CompVal / Multiplier;
  CompVal = CompVal / 10;

  P->PlllockCmp = (UINT32)CompVal;
}

/**
  pll_14nm_ssc_calc: SSC down-spread 0.5 % @ 31.5 kHz (driver defaults).
**/
STATIC
VOID
PllSscCalc (
  IN  UINT64          VcoRate,
  IN  UINT64          Fref,
  OUT DSI_PLL_PARAMS  *P
  )
{
  UINT32  SscFreq     = 31500;
  UINT32  Spread      = 5;         // PPM / 1000
  UINT32  AdjPeriod   = 37;
  UINT32  Period;
  UINT32  SscPeriod;
  UINT64  StepSize;
  UINT32  Ref;

  SscPeriod = SscFreq / 500;
  Period    = (UINT32)Fref / 1000;
  SscPeriod = (Period + SscPeriod - 1) / SscPeriod;
  SscPeriod -= 1;
  P->SscPeriod = SscPeriod;

  StepSize = VcoRate;
  Ref      = (UINT32)Fref / 1000;
  StepSize = StepSize / Ref;
  StepSize <<= 20;
  StepSize = StepSize / 1000;
  StepSize *= Spread;
  StepSize = StepSize / 1000;
  StepSize *= (AdjPeriod + 1);

  StepSize = StepSize / (SscPeriod + 1);
  StepSize &= 0x0ffff;

  P->SscStepSize = (UINT32)StepSize;
}

/**
  pll_14nm_calc_vco_count: measurement windows for the reset state machine.
**/
STATIC
VOID
PllVcoCountCalc (
  IN  UINT64          VcoRate,
  IN  UINT64          Fref,
  OUT DSI_PLL_PARAMS  *P
  )
{
  UINT64  Data;
  UINT32  Cnt;

  Data = Fref * 5;                 // vco_measure_time = 5
  Data = Data / 1000000;
  Data &= 0x03ff;
  Data -= 2;
  P->VcoDivRef = (UINT32)Data;

  Data = VcoRate / 1000000;
  Data *= 5;
  Data = Data / 10;
  P->VcoCount = (UINT32)Data;

  Data = Fref * 5;                 // kvco_measure_time = 5
  Data = Data / 1000000;
  Data &= 0x03ff;
  Data -= 1;
  P->KvcoDivRef = (UINT32)Data;

  // pll_14nm_kvco_slop: 1.3G < rate <= 1.8G => 600.
  Cnt = (VcoRate > 1300000000ULL && VcoRate <= 1800000000ULL) ? 600 : 400;
  Cnt *= 2;
  Cnt /= 100;
  Cnt *= 5;
  P->KvcoCount = Cnt;
}

STATIC
VOID
PllCommit (
  IN UINT64               VcoRate,
  IN UINT64               Fref,
  IN CONST DSI_PLL_PARAMS *P
  )
{
  UINTN  Base     = MSM8996_DSI0_PLL_BASE;
  UINTN  Cmn      = MSM8996_DSI0_PHY_BASE;
  UINT32 SscEnable = 1;
  UINT32 Data;

  //
  // pll_db_commit_14nm -- LDO first, then the static register set, the
  // software reset, then the frequency-dependent registers.
  //
  MmioWrite32 (Cmn + D14_CMN_LDO_CNTRL, 0x3c);         // cmn_ldo_cntrl

  // pll_db_commit_common (non-frequency-dependent).
  MmioWrite32 (Base + D14_PLL_SYSCLK_EN_RESET, 0);
  MmioWrite32 (Base + D14_PLL_TXCLK_EN, 1);            // pll_txclk_en
  MmioWrite32 (Base + D14_PLL_RESETSM_CNTRL, 48);
  MmioWrite32 (Base + D14_PLL_RESETSM_CNTRL2, 4 << 3); // bandgap_timer = 4
  MmioWrite32 (Base + D14_PLL_RESETSM_CNTRL5, 5);      // pll_wakeup_timer
  MmioWrite32 (Base + D14_PLL_VCO_DIV_REF1, P->VcoDivRef & 0xff);
  MmioWrite32 (Base + D14_PLL_VCO_DIV_REF2, (P->VcoDivRef >> 8) & 0x3);
  MmioWrite32 (Base + D14_PLL_KVCO_DIV_REF1, P->KvcoDivRef & 0xff);
  MmioWrite32 (Base + D14_PLL_KVCO_DIV_REF2, (P->KvcoDivRef >> 8) & 0x3);
  MmioWrite32 (Base + D14_PLL_PLL_MISC1, 16);
  MmioWrite32 (Base + D14_PLL_IE_TRIM, 4);
  MmioWrite32 (Base + D14_PLL_IP_TRIM, 4);
  MmioWrite32 (Base + D14_PLL_CP_SET_CUR, (1 << 3) | 1);
  MmioWrite32 (Base + D14_PLL_PLL_ICPCSET, 0);
  MmioWrite32 (Base + D14_PLL_PLL_ICPMSET, 0);
  MmioWrite32 (Base + D14_PLL_PLL_ICP_SET, (4 << 3) | 4);
  MmioWrite32 (Base + D14_PLL_PLL_LPF1, (1 << 4) | 11);  // cap2<<4 | cap1
  MmioWrite32 (Base + D14_PLL_IPTAT_TRIM, 7);
  MmioWrite32 (Base + D14_PLL_PLL_CRCTRL, 2 | (1 << 4));

  // pll_14nm_software_reset.
  MmioWrite32 (Cmn + D14_CMN_PLL_CNTRL, 0);
  MmioWrite32 (Cmn + D14_CMN_CTRL_1, 0x20);
  MicroSecondDelay (10);
  MmioWrite32 (Cmn + D14_CMN_CTRL_1, 0);

  MmioWrite32 (Cmn + D14_CMN_CLK_CFG1, 1);             // dsiclk_sel = 1
  MmioWrite32 (Cmn + D14_CMN_CTRL_0, 0xff);            // data/clk/pll normal

  // Frequency-dependent registers.
  MmioWrite32 (Base + D14_PLL_DEC_START, P->DecStart & 0xff);
  MmioWrite32 (Base + D14_PLL_DIV_FRAC_START1, P->DivFracStart & 0xff);
  MmioWrite32 (Base + D14_PLL_DIV_FRAC_START2, (P->DivFracStart >> 8) & 0xff);
  MmioWrite32 (Base + D14_PLL_DIV_FRAC_START3, (P->DivFracStart >> 16) & 0xf);
  MmioWrite32 (Base + D14_PLL_PLLLOCK_CMP1, P->PlllockCmp & 0xff);
  MmioWrite32 (Base + D14_PLL_PLLLOCK_CMP2, (P->PlllockCmp >> 8) & 0xff);
  MmioWrite32 (Base + D14_PLL_PLLLOCK_CMP3, (P->PlllockCmp >> 16) & 0x3);
  MmioWrite32 (Base + D14_PLL_PLLLOCK_CMP_EN, (1 << 1) | (0 << 3));
  MmioWrite32 (Base + D14_PLL_VCO_COUNT1, P->VcoCount & 0xff);
  MmioWrite32 (Base + D14_PLL_VCO_COUNT2, (P->VcoCount >> 8) & 0xff);
  MmioWrite32 (Base + D14_PLL_KVCO_COUNT1, P->KvcoCount & 0xff);
  MmioWrite32 (Base + D14_PLL_KVCO_COUNT2, (P->KvcoCount >> 8) & 0x3);
  MmioWrite32 (
    Base + D14_PLL_PLL_LPF2_POSTDIV,
    ((P->PllPostdiv - 1) << 4) | 3       // lpf_res1 = 3
    );

  if (SscEnable != 0) {
    // pll_db_commit_ssc.
    MmioWrite32 (Base + D14_PLL_SSC_ADJ_PER1, 37 & 0xff);
    MmioWrite32 (Base + D14_PLL_SSC_ADJ_PER2, (37 >> 8) & 0x3);
    MmioWrite32 (Base + D14_PLL_SSC_PER1, P->SscPeriod & 0xff);
    MmioWrite32 (Base + D14_PLL_SSC_PER2, (P->SscPeriod >> 8) & 0xff);
    MmioWrite32 (Base + D14_PLL_SSC_STEP_SIZE1, P->SscStepSize & 0xff);
    MmioWrite32 (Base + D14_PLL_SSC_STEP_SIZE2, (P->SscStepSize >> 8) & 0xff);
    Data = (0 << 1) | 0x1;               // center = 0 (down), enable
    MmioWrite32 (Base + D14_PLL_SSC_EN_CENTER, Data);
  }
}

/**
  dsi_pll_14nm_enable_seq: start the PLL and wait for lock.
**/
STATIC
EFI_STATUS
PllEnable (
  VOID
  )
{
  UINTN  Base = MSM8996_DSI0_PLL_BASE;
  UINTN  Cmn  = MSM8996_DSI0_PHY_BASE;
  UINT32 Tries;
  UINT32 Val;

  MmioWrite32 (Base + D14_PLL_VREF_CFG1, 0x10);
  MmioWrite32 (Cmn + D14_CMN_PLL_CNTRL, 1);

  // pll_14nm_poll_for_ready: bit5 (reset-sm ready), then bit0 (pll locked).
  for (Tries = 0; Tries < 15; Tries++) {
    Val = MmioRead32 (Base + D14_PLL_RESET_SM_READY_STATUS);
    if ((Val & (1 << 5)) != 0) {
      return EFI_SUCCESS;
    }

    MicroSecondDelay (1000);
  }

  for (Tries = 0; Tries < 15; Tries++) {
    Val = MmioRead32 (Base + D14_PLL_RESET_SM_READY_STATUS);
    if ((Val & (1 << 0)) != 0) {
      return EFI_SUCCESS;
    }

    MicroSecondDelay (1000);
  }

  DEBUG ((DEBUG_ERROR, "%a: DSI PLL lock failed\n", gEfiCallerBaseName));
  return EFI_TIMEOUT;
}

//==============================================================================
// 14nm PHY (dsi_phy_14nm.c dsi_14nm_phy_enable, standalone usecase)
//==============================================================================

STATIC
VOID
PhyEnable (
  IN CONST DSI_PLL_PARAMS *P
  )
{
  UINTN  Base = MSM8996_DSI0_PHY_BASE;
  UINTN  Lane = MSM8996_DSI0_LANE_BASE;
  UINTN  Cmn  = MSM8996_DSI0_PHY_BASE;
  INTN   i;

  //
  // PHY programming needs the DSI AHB clock: mdss_esc0 was enabled in
  // MmssPowerAndClocks (ESC0 is the PHY's register-interface clock source
  // on this generation via the DSI CTRL core clock -- mdss_ahb gates it).
  //

  // LDO_CNTRL: 0x1c | VREG_CTRL(32) (non-standalone load path, as mainline).
  MmioWrite32 (Base + D14_CMN_LDO_CNTRL, 0x1c | 0x20);

  MmioWrite32 (Base + D14_CMN_GLBL_TEST_CTRL, 0x1);

  // 4 data lanes + 1 clock lane.
  for (i = 0; i < 5; i++) {
    BOOLEAN  ClkLane = (i == 4);
    UINTN    Ln      = Lane + 0x80 * i;

    MmioWrite32 (Ln + D14_LN_VREG_CNTRL, 0x1d);
    MmioWrite32 (Ln + D14_LN_STRENGTH_CTRL_0, 0xff);
    MmioWrite32 (Ln + D14_LN_STRENGTH_CTRL_1, ClkLane ? 0x00 : 0x06);
    MmioWrite32 (Ln + D14_LN_CFG3, ClkLane ? 0x8f : 0x0f);
    MmioWrite32 (Ln + D14_LN_CFG2, 0x10);
    MmioWrite32 (Ln + D14_LN_TEST_DATAPATH, 0);
    MmioWrite32 (Ln + D14_LN_TEST_STR, 0x88);

    // dsi_14nm_dphy_set_timing with vendor bytes from the DTB.
    MmioWrite32 (Ln + D14_LN_TIMING_CTRL_4, mLaneTiming[i][0]);
    MmioWrite32 (Ln + D14_LN_TIMING_CTRL_5, mLaneTiming[i][1]);
    MmioWrite32 (Ln + D14_LN_TIMING_CTRL_6, mLaneTiming[i][2]);
    MmioWrite32 (Ln + D14_LN_TIMING_CTRL_7, mLaneTiming[i][3]);
    MmioWrite32 (Ln + D14_LN_TIMING_CTRL_8, mLaneTiming[i][4]);
    MmioWrite32 (Ln + D14_LN_TIMING_CTRL_9, mLaneTiming[i][5]);
    MmioWrite32 (Ln + D14_LN_TIMING_CTRL_10, mLaneTiming[i][6]);
    MmioWrite32 (Ln + D14_LN_TIMING_CTRL_11, mLaneTiming[i][7]);

    // CFG0 prepare-dly: bit_rate > 120 MHz => 0. CFG1 halfbyte: off.
    MmioWrite32 (Ln + D14_LN_CFG0, 0);
    MmioWrite32 (Ln + D14_LN_CFG1, 0);
  }

  // Make sure PLL is not started.
  MmioWrite32 (Base + D14_CMN_PLL_CNTRL, 0x00);

  // Reset digital block.
  MmioWrite32 (Base + D14_CMN_CTRL_1, 0x80);
  MicroSecondDelay (100);
  MmioWrite32 (Base + D14_CMN_CTRL_1, 0x00);

  // set_usecase(STANDALONE): CLKBUFLR_EN = 0x1.
  MmioWrite32 (MSM8996_DSI0_PLL_BASE + D14_PLL_CLKBUFLR_EN, 0x1);

  // Remove power down from PLL and all lanes.
  MmioWrite32 (Base + D14_CMN_CTRL_0, 0xff);

  //
  // Configure the PLL for the panel bit clock and start it.
  //
  PllCommit (PANEL_VCO_HZ, MSM8996_TIMER_CLOCK_HZ, P);

  // Post-dividers: N1 = 2 (byteclk = vco/2/8), N2 = 3 (pixel = vco/2/2/3).
  MmioWrite32 (Cmn + D14_CMN_CLK_CFG0, (UINT32)(PLL_N1 | (PLL_N2 << 4)));
}

//==============================================================================
// DSI host (dsi_host.c dsi_ctrl_config / dsi_timing_setup + command DMA)
//==============================================================================

/**
  Build one MSM-format command packet in the DMA scratch and fire it.

  MSM packet-in-memory format (dsi_host.c dsi_cmd_dma_add):
    [0] = header[1], [1] = header[2], [2] = data_id (dt | vc<<6),
    [3] = 0x80 | long<<6, then payload, 0xff-padded to a 4-byte multiple.
**/
STATIC
EFI_STATUS
DsiSendCommand (
  IN CONST DSI_PANEL_CMD *Cmd
  )
{
  UINTN   Host   = MSM8996_DSI0_CTRL_BASE;
  UINT8   *Buf   = (UINT8 *)(UINTN)DSI_CMD_DMA_SCRATCH;
  UINTN   Len;
  UINT32  Count;
  BOOLEAN Long;

  Long = (Cmd->DataType == 0x39);

  if (Long) {
    Buf[0] = Cmd->Payload[0];
    Buf[1] = Cmd->Payload[1];
    Buf[2] = Cmd->DataType;
    Buf[3] = 0x80 | 0x40;                       // last | long
    CopyMem (Buf + 4, Cmd->Payload + 2, Cmd->Len - 2);
    Count = 4 + (Cmd->Len - 2);
  } else {
    Buf[0] = Cmd->Payload[0];
    Buf[1] = Cmd->Payload[1];
    Buf[2] = Cmd->DataType;
    Buf[3] = 0x80;                              // last, short
    Count = 4;
  }

  Len = (Count + 3) & ~(UINTN)3;
  SetMem (Buf + Count, Len - Count, 0xff);

  //
  // The old (xml.h) offsets made DMA_BASE land on the interface timing
  // register and DMA_LEN on the DSI_CTRL register -- i.e. the panel on-command
  // DMA never received its buffer address and every trigger aborted the
  // transfer.  DMA_LEN is also write-count / read-zero, so the completion poll
  // must use "cleared", not "all ones".
  //
  MmioWrite32 (Host + DSI_REG_DMA_BASE, DSI_CMD_DMA_SCRATCH);
  MmioWrite32 (Host + DSI_REG_DMA_LEN, (UINT32)Len);
  MmioWrite32 (Host + DSI_REG_TRIG_DMA, 0x1);   // SW trigger

  // DMA_LEN self-clears when the transfer completes.
  DsiPoll (Host + DSI_REG_DMA_LEN, 0xffffffff, 0, 200000);

  if (Cmd->WaitMs != 0) {
    MicroSecondDelay (Cmd->WaitMs * 1000);
  }

  return EFI_SUCCESS;
}

//
// Programme the DSI video-mode timing, pixel format and lane enable -- the
// part of dsi_ctrl_config that decides how many pixels the controller puts
// on the wire for every scanline.
//
// This has to run on the hot hand-off path too.  The bootloader paints its
// splash in command mode (straight into the panel's own GRAM), so it leaves
// ACTIVE_H/ACTIVE_V/TOTAL/VID_CFG0 at whatever the early stage programmed
// and never needs them to describe a real video line.  Reusing those values
// after switching INTF1 to video mode made the panel receive roughly a third
// of each scanline and repeat it three times across the width -- the screen
// showed the whole frame squeezed to 1/3 and tiled.
//
// Deliberately does NOT touch the PHY/PLL, the controller reset or the panel
// on-sequence: on the hot path the link is already up and re-resetting the
// controller would risk losing a working display.
//
STATIC
VOID
DsiVideoTimingOnly (
  VOID
  )
{
  UINTN   Host = MSM8996_DSI0_CTRL_BASE;
  UINT32  HaStart;
  UINT32  HaEnd;
  UINT32  VaStart;
  UINT32  VaEnd;
  UINT32  Data;

  //
  // Video mode timing, vendor mdss_dsi_mode_setup() layout (identical field
  // packing to dsi_timing_setup() in the newer driver):
  //
  //   0x0024 = (hspw + hbp + width) << 16 | (hspw + hbp)
  //   0x0028 = (vspw + vbp + height) << 16 | (vspw + vbp)
  //   0x002c = (vsync_period - 1) << 16 | (hsync_period - 1)
  //   0x0030 = hspw << 16
  //   0x0034 = 0
  //   0x0038 = vspw << 16
  //
  // Note 0x0024/0x0028 carry x/y coordinates that are 1-based relative to the
  // sync edge (the panel counts the pulse as line 0).
  //
  HaStart = PANEL_HSW + PANEL_HBP;
  HaEnd   = HaStart + PANEL_WIDTH;
  VaStart = PANEL_VSW + PANEL_VBP;
  VaEnd   = VaStart + PANEL_HEIGHT;

  MmioWrite32 (Host + DSI_REG_ACTIVE_H, (HaEnd << 16) | HaStart);
  MmioWrite32 (Host + DSI_REG_ACTIVE_V, (VaEnd << 16) | VaStart);
  MmioWrite32 (
    Host + DSI_REG_TOTAL,
    ((PANEL_VTOTAL - 1) << 16) | (PANEL_HTOTAL - 1)
    );
  MmioWrite32 (Host + DSI_REG_ACTIVE_HSYNC, PANEL_HSW << 16);
  MmioWrite32 (Host + DSI_REG_ACTIVE_VSYNC_HPOS, 0);
  MmioWrite32 (Host + DSI_REG_ACTIVE_VSYNC_VPOS, PANEL_VSW << 16);

  // Video mode payload config (dsi_ctrl_config, video part).
  //   VID_CFG0: pulse/hsa-he | eof-bllp stop | bllp stop | burst | RGB888 | vc0
  //   VID_CFG1: no RGB swap / no per-component select.
  MmioWrite32 (Host + DSI_REG_VID_CFG0, DSI_VID_CFG0_BUILD);
  MmioWrite32 (Host + DSI_REG_VID_CFG1, 0);

  //
  // HS TX timeout: resolution = 8 esc clocks, timeout = 0x3f08 esc clocks
  // (vendor value 0x3fd08).  Without it the controller aborts a burst as soon
  // as the pixel FIFO underruns, and the panel then resamples whatever is on
  // the wire.
  //
  MmioWrite32 (Host + DSI_REG_HS_TIMER_CTRL, 0x0003FD08);

  //
  // Enable the controller's internal clock gates and request a continuous HS
  // clock on the clock lane (the ft8716 panel is configured
  // qcom,mdss-dsi-clk-lane-hs-request for "always on" burst video).
  //
  MmioWrite32 (Host + DSI_REG_CLK_CTRL, DSI_CLK_CTRL_ENABLE_CLKS);
  Data = MmioRead32 (Host + DSI_REG_LANE_CTRL);
  MmioWrite32 (Host + DSI_REG_LANE_CTRL, Data | 0x10000000);

  //
  // DSI_CTRL: ENABLE | VID_MODE_EN | CLK_EN | 4 data lanes.
  //
  // The panel is dsi_video_mode, so the controller has to actually be in video
  // mode: VID_MODE_EN is what makes it frame the MDP pixel stream with our
  // ACTIVE_H/V + TOTAL timing.  While the old offset was in use this write
  // went to 0x000, i.e. the hardware-version register, and the real DSI_CTRL
  // kept whatever aboot's command-mode splash left in it.
  //
  MmioWrite32 (
    Host + DSI_REG_CTRL,
    DSI_CTRL_ENABLE | DSI_CTRL_VID_MODE_EN | DSI_CTRL_CLK_EN |
    DSI_CTRL_LANE0 | DSI_CTRL_LANE1 | DSI_CTRL_LANE2 | DSI_CTRL_LANE3
    );
}

/**
  Park the controller: disable video mode and the DSI core, wait for both
  engines to go idle, then soft-reset.  Vendor mdss_dsi_controller_cfg(0) +
  mdss_dsi_sw_reset, which is what runs before any mode change upstream.
**/
STATIC
VOID
DsiControllerDisable (
  VOID
  )
{
  UINTN  Host = MSM8996_DSI0_CTRL_BASE;
  UINT32 Ctl;

  //
  // Same trick as vendor mdss_dsi_op_mode_config in reverse: clear ENABLE and
  // VID_MODE_EN, keep CLK_EN and the lane map, so the block stays clocked and
  // can finish the burst in flight instead of being cut mid-packet.
  //
  Ctl  = MmioRead32 (Host + DSI_REG_CTRL);
  Ctl &= ~(UINT32)(DSI_CTRL_ENABLE | DSI_CTRL_VID_MODE_EN);
  MmioWrite32 (Host + DSI_REG_CTRL, Ctl);

  // Wait for VIDEO_MODE_ENGINE_BUSY and CMD_MODE_DMA_BUSY to drop.
  DsiPoll (Host + DSI_REG_STATUS, DSI_STATUS0_VIDEO_MODE_ENGINE_BUSY, 0, 16000);
  DsiPoll (Host + DSI_REG_STATUS, DSI_STATUS0_CMD_MODE_DMA_BUSY, 0, 16000);

  // Soft reset, keeping the link configured (vendor mdss_dsi_sw_reset false).
  MmioWrite32 (Host + DSI_REG_RESET, 0x1);
  MicroSecondDelay (100);
  MmioWrite32 (Host + DSI_REG_RESET, 0x0);
}

STATIC
VOID
DsiHostConfigure (
  VOID
  )
{
  UINTN   Host = MSM8996_DSI0_CTRL_BASE;
  UINT32  Index;

  //
  // Controller soft reset (dsi_hw_init / REG_DSI_RESET).
  //
  MmioWrite32 (Host + DSI_REG_RESET, 0x0);
  MmioWrite32 (Host + DSI_REG_RESET, 0x1);
  MmioWrite32 (Host + DSI_REG_RESET, 0x0);

  // Command path basics (dsi_ctrl_config, non-video part).
  MmioWrite32 (
    Host + DSI_REG_CMD_DMA_CTRL,
    0x10000000 | 0x04000000                  // FROM_FRAME_BUFFER | LOW_POWER
    );

  MmioWrite32 (
    Host + DSI_REG_TRIG_CTRL,
    0x80000000 |                             // TE (always assume)
    (TRIGGER_NONE << 4) |                    // MDP trigger = none
    (TRIGGER_SW << 0) |                      // DMA trigger = SW
    (0 << 8) |                               // stream 0
    0x00001000                               // BLOCK_DMA_WITHIN_FRAME (6G)
    );

  // DTB qcom,mdss-dsi-tx-eot-append = 1: append EOT after every packet so the
  // panel sees a clean end of transmission instead of trailing garbage.
  MmioWrite32 (
    Host + DSI_REG_EOT_PACKET_CTRL,
    DSI_EOT_PACKET_CTRL_TX_EOT_APPEND
    );

  // Shared PHY timing: t-clk-post / t-clk-pre from the DTB (6 bits each).
  MmioWrite32 (
    Host + DSI_REG_CLKOUT_TIMING_CTRL,
    (T_CLK_POST << 8) | T_CLK_PRE
    );

  MmioWrite32 (Host + DSI_REG_LANE_SWAP_CTRL, 0x0);   // lane_map_0123

  //
  // The bootloader might still consider the panel awake (cont_splash left the
  // DSI0 link and the MIPI DSI block alive).  That is risky because calling
  // resume while the panel is already in sleep, or calling sleep twice, breaks
  // the panel boot sequence, so the display driver core only calls resume if
  // the panel isn't awake to begin with.  We do not know for sure, so: take the
  // controller down, reset the panel, re-init it from scratch.  Everything
  // below re-establishes the link deterministically.
  //
  DsiControllerDisable ();

  // Video mode timing + payload format + lane enable (shared with the hot
  // hand-off path, see DsiVideoTimingOnly).
  DsiVideoTimingOnly ();

  //
  // Panel on-commands: reset, LP11 settle, then the 18-packet sequence.
  //
  // Reset sequence [1, 10, 0, 10, 1, 10] (ms, level) from the DTB.
  //
  MmioWrite32 (
    TLMM_GPIO_CFG (MSM8996_DSI_RESET_GPIO),
    TLMM_GPIO_CFG_FUNC_GPIO | TLMM_GPIO_CFG_DRV_8MA | TLMM_GPIO_CFG_OE
    );
  MmioWrite32 (TLMM_GPIO_IN_OUT (MSM8996_DSI_RESET_GPIO), 0x1);
  MicroSecondDelay (10 * 1000);
  MmioWrite32 (TLMM_GPIO_IN_OUT (MSM8996_DSI_RESET_GPIO), 0x0);
  MicroSecondDelay (10 * 1000);
  MmioWrite32 (TLMM_GPIO_IN_OUT (MSM8996_DSI_RESET_GPIO), 0x1);
  MicroSecondDelay (10 * 1000);

  for (Index = 0; Index < ARRAY_SIZE (mPanelOnCommands); Index++) {
    DsiSendCommand (&mPanelOnCommands[Index]);
  }
}

//==============================================================================
// MDP5 pipeline (mdp5_encoder/mixer/ctl, video mode INTF1)
//==============================================================================

STATIC
VOID
Mdp5Configure (
  VOID
  )
{
  UINT32  VaStart = PANEL_VTOTAL - (PANEL_VBP + PANEL_HEIGHT);

  //
  // INTF vertical timing is expressed in PIXELS, not in lines.
  //
  // Upstream mdp5_encoder.c (drivers/gpu/drm/msm/mdp/mdp5/mdp5_encoder.c):
  //
  //   hsync_start_x = htotal - hsync_start;
  //   hsync_end_x   = htotal - (hsync_start - hdisplay) - 1;
  //   vsync_period  = vtotal * htotal;
  //   vsync_len     = (vsync_end - vsync_start) * htotal;
  //   display_v_start = (vtotal - vsync_start) * htotal;
  //   display_v_end   = vsync_period
  //                     - ((vsync_start - vdisplay) * htotal) - 1;
  //
  // v33..v43 wrote the raw line counts (VSYNC_PERIOD = 1954, VSYNC_LEN = 2,
  // DISPLAY_VSTART = 18, DISPLAY_VEND = 1938).  The timing engine then believed
  // that one frame was 1954 *pixels* long, i.e. 1.7 scanlines: with the panel
  // in burst traffic mode the DSI controller framed the burst against that
  // nonsense, only a fraction of every line reached the panel and the rest of
  // the line kept whatever the previous frame had left there.  That is exactly
  // the "the same 360 px window repeated three times, and the old menu text
  // still visible" artefact measured in the v33..v43 photos.
  //
  UINT32  HsyncStartX = PANEL_HTOTAL - (PANEL_HBP + PANEL_WIDTH);
  UINT32  HsyncEndX   = PANEL_HTOTAL - PANEL_HBP - 1;         // inclusive
  UINT32  VsyncPeriod = PANEL_VTOTAL * PANEL_HTOTAL;          // pixels / frame
  UINT32  VsyncLen    = PANEL_VSW * PANEL_HTOTAL;             // pixels
  UINT32  DispVStart  = VaStart * PANEL_HTOTAL;
  UINT32  DispVEnd    = VsyncPeriod - (PANEL_VBP * PANEL_HTOTAL) - 1;

  // Route INTF1 to the DSI video engine.
  MmioWrite32 (MDP_DISP_INTF_SEL, MDP5_INTF_SEL_INTF1_DSI);

  //
  // SSPP_RGB0: source = the framebuffer the pipe is really meant to read,
  // 1:1 scale, no flip.  Probe before overwriting SRC0_ADDR, otherwise the
  // evidence is gone.
  //
  mScanoutBase = Mdp5ProbeScanoutBase ();

  MmioWrite32 (MDP_RGB0_SRC_SIZE, (PANEL_HEIGHT << 16) | PANEL_WIDTH);
  MmioWrite32 (MDP_RGB0_SRC_IMG_SIZE, (PANEL_HEIGHT << 16) | PANEL_WIDTH);
  MmioWrite32 (MDP_RGB0_SRC_XY, 0);
  MmioWrite32 (MDP_RGB0_OUT_SIZE, (PANEL_HEIGHT << 16) | PANEL_WIDTH);
  MmioWrite32 (MDP_RGB0_OUT_XY, 0);
  MmioWrite32 (MDP_RGB0_SRC0_ADDR, (UINT32)mScanoutBase);
  mScanoutProgrammed = TRUE;
  MmioWrite32 (MDP_RGB0_SRC_STRIDE_A, PANEL_WIDTH * 4);
  MmioWrite32 (MDP_RGB0_SRC_FORMAT, PIPE_FMT_RGB888);
  MmioWrite32 (MDP_RGB0_SRC_UNPACK, PIPE_UNPACK_RGB888);
  MmioWrite32 (MDP_RGB0_SRC_OP_MODE, 0);
  MmioWrite32 (MDP_RGB0_OP_MODE, 0);
  MmioWrite32 (MDP_RGB0_SCALE_CONFIG, 0);

  //
  // LM0: single opaque stage fed by RGB0.
  //
  MmioWrite32 (MDP_LM0_OUT_SIZE, (PANEL_HEIGHT << 16) | PANEL_WIDTH);
  MmioWrite32 (MDP_LM0_BLEND_OP_MODE, 0);
  MmioWrite32 (MDP_LM0_COLOR_OUT, 0x1);        // STAGE0_FG
  // Layer ext register (mdp5_ctl_blend always writes it): no EXT/CSC bits.
  MmioWrite32 (MDP_CTL0_LAYER_EXT, 0);

  //
  // CTL0: RGB0 on STAGE_BASE of LM0, driven out via INTF1 in video mode.
  //
  MmioWrite32 (MDP_CTL0_LAYER, (UINT32)MDP_STAGE_BASE << 9);
  MmioWrite32 (MDP_CTL0_OP_MODE, 0x2 << 4);    // INTF_NUM = INTF1 (2), video

  //
  // INTF1 video timing (mdp5_encoder mode_set).
  //
  //
  // HSYNC_CTL packs the pulse width in [15:0] and the LINE PERIOD in [31:16]
  // (mdp5.xml.h, MDP5_INTF_HSYNC_CTL_PERIOD / _PULSEW).  v33..v39 wrote
  // period = 0, which does not describe a 1148-pixel line at all: the
  // interface then paces the pixel stream with a broken line length and the
  // panel samples whatever thirds it can find.  Upstream mdp5_encoder writes
  // the full htotal here.
  //
  MmioWrite32 (
    MDP_INTF1_HSYNC_CTL,
    (PANEL_HSW & 0xffff) | ((PANEL_HTOTAL & 0xffff) << 16)
    );
  MmioWrite32 (MDP_INTF1_VSYNC_PERIOD, VsyncPeriod);
  MmioWrite32 (MDP_INTF1_VSYNC_LEN, VsyncLen);
  MmioWrite32 (
    MDP_INTF1_DISPLAY_HCTL,
    (HsyncStartX & 0xffff) | ((HsyncEndX & 0xffff) << 16)
    );
  MmioWrite32 (MDP_INTF1_DISPLAY_VSTART, DispVStart);
  MmioWrite32 (MDP_INTF1_DISPLAY_VEND, DispVEnd);
  MmioWrite32 (MDP_INTF1_UNDERFLOW, 0xff);
  MmioWrite32 (MDP_INTF1_HSYNC_SKEW, 0);
  MmioWrite32 (MDP_INTF1_POLARITY, 0);
  //
  // PANEL_FORMAT: upstream mdp5_encoder.c uses 0x2100 | bpc; for 8bpc that is
  // 0x213F.  v33..v43 wrote 0x3, which leaves the upper bits (data type /
  // bpc) at whatever the bootloader left behind.
  //
  MmioWrite32 (MDP_INTF1_PANEL_FORMAT, 0x213F);
  //
  // ACTIVE_* must be zero for a normal full-screen mode (upstream writes
  // START(0) | END(0)).  Explicitly zero them so a stale bootloader value
  // cannot carve a sub-window out of every line.
  //
  MmioWrite32 (MDP_INTF1_ACTIVE_HCTL, 0);
  MmioWrite32 (MDP_INTF1_ACTIVE_VSTART, 0);
  MmioWrite32 (MDP_INTF1_ACTIVE_VEND, 0);
  MmioWrite32 (MDP_INTF1_BORDER_COLOR, 0);
  MmioWrite32 (MDP_INTF1_FRAME_LINE_COUNT_EN, 0x3);

  //
  // Commit in the order mdp5_ctl_commit() uses (blend, then source pipes, then
  // the interface), and only then start the timing engine: a flush that names
  // a block which is not yet programmed latches stale attributes into the
  // double-buffered CTL blob.
  //
  MmioWrite32 (MDP_CTL0_FLUSH, MDP_FLUSH_LM0);
  MmioWrite32 (MDP_CTL0_FLUSH, MDP_FLUSH_RGB0);
  MmioWrite32 (MDP_CTL0_FLUSH, MDP_FLUSH_CTL);
  MmioWrite32 (MDP_INTF1_TIMING_EN, 0x1);
}

//==============================================================================
// Scanout source probe
//==============================================================================
//
// The scanout address is a guess (the carve-out this device's DTB declares),
// and a wrong guess looks exactly like "the pipeline does not work": black
// panel, no error anywhere.  aboot (LK) ran the vendor display stack before
// handing over, so SSPP_RGB0 may still hold the address of the buffer LK
// itself was scanning.  If that address is a plausible DRAM buffer, reuse it
// instead of our guess: the hardware is then guaranteed to be pointed at a
// region that already contains a valid, correctly-strided image.
//
// Plausibility check: MSM8996 DRAM starts at 0x80000000; the value must be
// 4 KiB aligned, must not land inside our own command-DMA scratch area, and
// must leave room for a full 1080x1920x4 frame below the 4 GiB mark.
//
#define MSM8996_DRAM_BASE        0x80000000ULL
#define MSM8996_FRAME_BYTES      ((UINT64)PANEL_WIDTH * PANEL_HEIGHT * 4)

STATIC
UINT64
Mdp5ProbeScanoutBase (
  VOID
  )
{
  UINT64  Live;
  UINT32  SrcSize;

  //
  // Only the first call carries information: afterwards the register holds the
  // address we ourselves wrote.
  //
  if (mScanoutProgrammed) {
    return mScanoutBase;
  }

  Live    = (UINT64)MmioRead32 (MDP_RGB0_SRC0_ADDR);
  SrcSize = MmioRead32 (MDP_RGB0_SRC_SIZE);

  //
  // Bit 31 of SRC0_ADDR is the "address is valid / pipe enabled" indicator on
  // this generation; strip it before using the address.
  //
  Live &= ~0x80000000ULL;

  if ((Live < MSM8996_DRAM_BASE) ||
      ((Live & 0xFFF) != 0) ||
      (Live >= (0x100000000ULL - MSM8996_FRAME_BYTES)) ||
      ((Live >= DSI_CMD_DMA_SCRATCH) &&
       (Live <  DSI_CMD_DMA_SCRATCH + DSI_CMD_DMA_SCRATCH_SZ)))
  {
    return MSM8996_CONT_SPLASH_BASE;
  }

  DEBUG ((
    DEBUG_INFO,
    "Msm8996Display: reusing live scanout 0x%lx (SRC_SIZE=0x%08x)\n",
    Live,
    SrcSize
    ));

  return Live;
}

UINT64
EFIAPI
Msm8996DisplayGetFrameBuffer (
  VOID
  )
{
  return mScanoutBase;
}

//==============================================================================
// Debug bring-up snapshot
//==============================================================================
//
// The panel is our only output device on this board, and it is currently the
// thing under investigation.  Capture the live register file right after the
// pipeline is configured; DisplayDxe hands the buffer to the UI, which renders
// it as hex digits so a photo of the screen gives us the values the hardware
// actually holds (as opposed to the values we believe we wrote).
//
// Group 0: MDP5 pipeline                 (12 words)
// Group 1: DSI controller 0x000-0x05c   (24 words)
// Group 2: DSI controller 0x060-0x0bc   (24 words)
// Group 3: DSI controller 0x0c0-0x11c   (24 words)
// Group 4: 14nm PLL + CMN               (20 words)
// Group 5: DSI0 lane 0 + clock lane     (16 words)
//
// The snapshot layout (24 words per group, 6 groups) is declared in
// Msm8996DisplayLib.h so callers can size their buffer.
//
STATIC CONST UINT32  mMdpSnapshotOffsets[12] = {
  0x00000004,   // DISP_INTF_SEL
  0x00002000,   // CTL0_LAYER
  0x00002014,   // CTL0_OP_MODE
  0x00015000,   // RGB0_SRC_SIZE
  0x00015008,   // RGB0_SRC_XY
  0x00015014,   // RGB0_SRC0_ADDR
  0x00015024,   // RGB0_SRC_STRIDE_A
  0x00015030,   // RGB0_SRC_FORMAT
  0x0006B800,   // INTF1_TIMING_EN
  0x0006B808,   // INTF1_HSYNC_CTL
  0x0006B80C,   // INTF1_VSYNC_PERIOD
  0x0006B83C    // INTF1_DISPLAY_HCTL
};

VOID
EFIAPI
Msm8996DisplayGetDebugSnapshot (
  OUT UINT32 *Buffer,
  IN  UINTN  Groups
  )
{
  UINTN   Host = MSM8996_DSI0_CTRL_BASE;
  UINTN   G;
  UINTN   Index;

  if ((Buffer == NULL) || (Groups == 0)) {
    return;
  }

  ZeroMem (Buffer, Groups * MSM8996_DISPLAY_DEBUG_GROUP_WORDS * sizeof (UINT32));

  // Group 0: MDP5.
  for (Index = 0; Index < 12; Index++) {
    Buffer[Index] = MmioRead32 (MDP_REG (mMdpSnapshotOffsets[Index]));
  }

  if (Groups < 2) {
    return;
  }

  // Group 1..3: DSI controller, four 32-bit words per row.
  for (Index = 0; Index < 24; Index++) {
    Buffer[24 + Index] = MmioRead32 (Host + 0x000 + Index * 4);
  }

  if (Groups < 3) {
    return;
  }

  for (Index = 0; Index < 24; Index++) {
    Buffer[48 + Index] = MmioRead32 (Host + 0x060 + Index * 4);
  }

  if (Groups < 4) {
    return;
  }

  for (Index = 0; Index < 24; Index++) {
    Buffer[72 + Index] = MmioRead32 (Host + 0x0C0 + Index * 4);
  }

  if (Groups < 5) {
    return;
  }

  // Group 4: 14nm PLL, first 16 words; then CMN_CLK_CFG0/1 + LDO.
  for (Index = 0; Index < 16; Index++) {
    Buffer[96 + Index] = MmioRead32 (MSM8996_DSI0_PLL_BASE + Index * 4);
  }

  Buffer[112] = MmioRead32 (MSM8996_DSI0_PHY_BASE + D14_CMN_CLK_CFG0);
  Buffer[113] = MmioRead32 (MSM8996_DSI0_PHY_BASE + D14_CMN_CLK_CFG1);
  Buffer[114] = MmioRead32 (MSM8996_DSI0_PHY_BASE + D14_CMN_CTRL_0);
  Buffer[115] = MmioRead32 (MSM8996_DSI0_PHY_BASE + D14_CMN_LDO_CNTRL);

  if (Groups < 6) {
    return;
  }

  // Group 5: lane 0 timing (0x18 .. 0x34 reversed = TIMING_4..11 order kept
  // ascending), then clock lane (index 4) timing.
  for (G = 0; G < 2; G++) {
    UINTN  Lane = MSM8996_DSI0_LANE_BASE + 0x80 * (G == 0 ? 0 : 4);

    for (Index = 0; Index < 8; Index++) {
      Buffer[120 + G * 8 + Index] =
        MmioRead32 (Lane + D14_LN_TIMING_CTRL_4 + Index * 4);
    }
  }
}

/**
  Bring the display chain up from cold.
**/
EFI_STATUS
EFIAPI
Msm8996DisplayColdInit (
  VOID
  )
{
  EFI_STATUS      Status;
  DSI_PLL_PARAMS  Pll;
  UINT32          Gdscr;

  ZeroMem (&Pll, sizeof (Pll));

  //
  // Hot hand-off: MDSS GDSC already powered -> the bootloader left the PMIC
  // rails, MMSS clocks, DSI PHY/PLL and the DSI link running.  None of that
  // is touched here.
  //
  // What must be re-programmed is everything that describes *our* frame:
  //   - the DSI video timing (the splash was pushed in command mode, so the
  //     controller's ACTIVE_H/V and VID_CFG0 do not describe a video line at
  //     all -- the panel ended up sampling about a third of each scanline and
  //     repeating it three times across the screen), and
  //   - the MDP5 layer/timing engine that scans the 1080x1920 buffer we hand
  //     out through GOP.
  //
  Gdscr = MmioRead32 (MSM8996_MMSS_CC_BASE + MMSS_MDSS_GDSCR);
  if ((Gdscr & GDSC_POWER_ON_STATUS) != 0) {
    DEBUG ((DEBUG_INFO, "%a: MDSS already powered (hot path), re-programming DSI timing + MDP5 scanout\n", gEfiCallerBaseName));
    DsiVideoTimingOnly ();
    Mdp5Configure ();
    return EFI_SUCCESS;
  }

  //
  // 1. DSI supply rails (PM8994): vdda 1.25 V (L2), vddio 1.8 V (L14).
  //
  Status = Msm8996PmicEnableRail (PmicRailDsiVdda, 1250000);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "%a: dsi vdda failed: %r\n", gEfiCallerBaseName, Status));
    return Status;
  }

  Status = Msm8996PmicEnableRail (PmicRailDsiVddio, 1800000);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "%a: dsi vddio failed: %r\n", gEfiCallerBaseName, Status));
    return Status;
  }

  //
  // 2. Backlight boost first, before any DSI work.
  //
  //    g0245d uses bl_ctrl_dcs: the panel has no backlight-enable GPIO and
  //    takes lab/ibb from the PM8994 WLED block.  Lighting the boost here
  //    (rather than at the end) turns the panel into a progress indicator:
  //    if the screen glows but stays black, everything up to and including
  //    SPMI/WLED is proven good and the fault is downstream in DSI/MDP5.
  //
  Status = Msm8996PmicWledInit (WLED_MAX_LEVEL);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "%a: wled init failed: %r\n", gEfiCallerBaseName, Status));
    //
    // Keep going: a dark panel is worth reporting, but the rest of the
    // pipeline is still worth bringing up for the serial log.
    //
  }

  //
  // 3. MMSS power domains + clocks.
  //
  MmssPowerAndClocks ();

  //
  // 4. PHY + PLL (locks 808 MHz lane clock).
  //
  PllInputInitFixed (&Pll);
  PllDecFracCalc (PANEL_VCO_HZ, MSM8996_TIMER_CLOCK_HZ, &Pll);
  PllSscCalc (PANEL_VCO_HZ, MSM8996_TIMER_CLOCK_HZ, &Pll);
  PllVcoCountCalc (PANEL_VCO_HZ, MSM8996_TIMER_CLOCK_HZ, &Pll);

  PhyEnable (&Pll);

  Status = PllEnable ();
  if (EFI_ERROR (Status)) {
    return Status;
  }

  //
  // 4. Route the locked PLL into MMSS: byte0 <- dsi0pllbyte, pclk0 <- dsi0pll.
  //
  Rcg2Set (MMSS_BYTE0_CLK_SRC_CMD_RCGR, 0x00000100);  // src=1, div=0
  Rcg2Set (MMSS_PCLK0_CLK_SRC_CMD_RCGR, 0x00000100);  // src=1, div=0
  BranchEnable (MMSS_MDSS_BYTE0_CBCR);
  BranchEnable (MMSS_MDSS_PCLK0_CBCR);

  //
  // 5. DSI host + panel on-commands.
  //
  DsiHostConfigure ();

  //
  // 6. MDP5 pipeline and timing engine.
  //
  Mdp5Configure ();

  //
  // 7. Backlight already up (step 2) -- nothing left to do here.
  //

  DEBUG ((
    DEBUG_INFO,
    "%a: cold display up (pclk %lu Hz, vco %lu Hz)\n",
    gEfiCallerBaseName, PANEL_PCLK_HZ, PANEL_VCO_HZ
    ));
  return EFI_SUCCESS;
}
