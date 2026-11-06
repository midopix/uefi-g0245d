/** @file
 *
 *  MSM8996 UFS PHY (Qualcomm 14nm QMP) calibration tables.
 *
 *  PROVENANCE
 *  ----------
 *  Register offsets and calibration values are the hardware constants from
 *  Linux v4.9 drivers/phy/phy-qcom-ufs-qmp-14nm.h (phy_cal_table_rate_A /
 *  phy_cal_table_rate_B), the reference programming sequence for this PHY
 *  generation. They are independent of any software licensing: they describe
 *  what the silicon expects.
 *
 *  Table semantics (from the kernel driver):
 *    - rate_A : programming for PWM gear 1..4 (60/150 Mbps lines) AND the
 *               first bring-up sequence; used unconditionally at init.
 *    - rate_B : only the delta entries for HS gear 1..3 (300+ Mbps);
 *               written after rate_A when the device negotiates rate B.
 *               UfsPassThruDxe runs at PWM gear 1, so rate_A alone is
 *               sufficient for UEFI bring-up.
 *
 *  Copyright (c) 2026, UniXNU contributors.
 *
 *  SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 **/

#ifndef __MSM8996_UFS_PHY_H__
#define __MSM8996_UFS_PHY_H__

#include <Msm8996Regs.h>

typedef struct {
  UINT32    Offset;
  UINT32    Value;
} MSM8996_UFS_PHY_CAL_ENTRY;

//
// QSERDES COM register offsets (base + 0x000).
//
#define QSERDES_COM_BG_TIMER                     QSERDES_COM (0x0C)
#define QSERDES_COM_BIAS_EN_CLKBUFLR_EN          QSERDES_COM (0x34)
#define QSERDES_COM_SYS_CLK_CTRL                 QSERDES_COM (0x3C)
#define QSERDES_COM_LOCK_CMP1_MODE0              QSERDES_COM (0x4C)
#define QSERDES_COM_LOCK_CMP2_MODE0              QSERDES_COM (0x50)
#define QSERDES_COM_LOCK_CMP3_MODE0              QSERDES_COM (0x54)
#define QSERDES_COM_LOCK_CMP1_MODE1              QSERDES_COM (0x58)
#define QSERDES_COM_LOCK_CMP2_MODE1              QSERDES_COM (0x5C)
#define QSERDES_COM_LOCK_CMP3_MODE1              QSERDES_COM (0x60)
#define QSERDES_COM_CP_CTRL_MODE0                QSERDES_COM (0x78)
#define QSERDES_COM_CP_CTRL_MODE1                QSERDES_COM (0x7C)
#define QSERDES_COM_PLL_RCTRL_MODE0              QSERDES_COM (0x84)
#define QSERDES_COM_PLL_RCTRL_MODE1              QSERDES_COM (0x88)
#define QSERDES_COM_PLL_CCTRL_MODE0              QSERDES_COM (0x90)
#define QSERDES_COM_PLL_CCTRL_MODE1              QSERDES_COM (0x94)
#define QSERDES_COM_SYSCLK_EN_SEL                QSERDES_COM (0xAC)
#define QSERDES_COM_RESETSM_CNTRL                QSERDES_COM (0xB4)
#define QSERDES_COM_LOCK_CMP_EN                  QSERDES_COM (0xC8)
#define QSERDES_COM_LOCK_CMP_CFG                 QSERDES_COM (0xCC)
#define QSERDES_COM_DEC_START_MODE0              QSERDES_COM (0xD0)
#define QSERDES_COM_DEC_START_MODE1              QSERDES_COM (0xD4)
#define QSERDES_COM_DIV_FRAC_START1_MODE0        QSERDES_COM (0xDC)
#define QSERDES_COM_DIV_FRAC_START2_MODE0        QSERDES_COM (0xE0)
#define QSERDES_COM_DIV_FRAC_START3_MODE0        QSERDES_COM (0xE4)
#define QSERDES_COM_DIV_FRAC_START1_MODE1        QSERDES_COM (0xE8)
#define QSERDES_COM_DIV_FRAC_START2_MODE1        QSERDES_COM (0xEC)
#define QSERDES_COM_DIV_FRAC_START3_MODE1        QSERDES_COM (0xF0)
#define QSERDES_COM_INTEGLOOP_GAIN0_MODE0        QSERDES_COM (0x108)
#define QSERDES_COM_INTEGLOOP_GAIN1_MODE0        QSERDES_COM (0x10C)
#define QSERDES_COM_INTEGLOOP_GAIN0_MODE1        QSERDES_COM (0x110)
#define QSERDES_COM_INTEGLOOP_GAIN1_MODE1        QSERDES_COM (0x114)
#define QSERDES_COM_VCO_TUNE_CTRL                QSERDES_COM (0x124)
#define QSERDES_COM_VCO_TUNE_MAP                 QSERDES_COM (0x128)
#define QSERDES_COM_VCO_TUNE1_MODE0              QSERDES_COM (0x12C)
#define QSERDES_COM_VCO_TUNE2_MODE0              QSERDES_COM (0x130)
#define QSERDES_COM_VCO_TUNE1_MODE1              QSERDES_COM (0x134)
#define QSERDES_COM_VCO_TUNE2_MODE1              QSERDES_COM (0x138)
#define QSERDES_COM_VCO_TUNE_TIMER1              QSERDES_COM (0x144)
#define QSERDES_COM_VCO_TUNE_TIMER2              QSERDES_COM (0x148)
#define QSERDES_COM_CLK_SELECT                   QSERDES_COM (0x174)
#define QSERDES_COM_HSCLK_SEL                    QSERDES_COM (0x178)
#define QSERDES_COM_CORECLK_DIV                  QSERDES_COM (0x184)
#define QSERDES_COM_CORE_CLK_EN                  QSERDES_COM (0x18C)
#define QSERDES_COM_CMN_CONFIG                   QSERDES_COM (0x194)
#define QSERDES_COM_SVS_MODE_CLK_SEL             QSERDES_COM (0x19C)
#define QSERDES_COM_CORECLK_DIV_MODE1            QSERDES_COM (0x1BC)

//
// TX / RX lane 0 register offsets.
// The 14nm UFS PHY DTB reg window covers the whole PHY; lane 1 does not
// exist on this device (lanes-per-direction = 1).
//
#define QSERDES_TX_HIGHZ_TRANSCEIVER_BIAS_DRVR_EN  QSERDES_TX (0x68)
#define QSERDES_TX_LANE_MODE                       QSERDES_TX (0x94)

#define QSERDES_RX_UCDR_FASTLOCK_FO_GAIN           QSERDES_RX (0x40)
#define QSERDES_RX_RX_TERM_BW                      QSERDES_RX (0x90)
#define QSERDES_RX_RX_EQ_GAIN1_LSB                 QSERDES_RX (0xC4)
#define QSERDES_RX_RX_EQ_GAIN1_MSB                 QSERDES_RX (0xC8)
#define QSERDES_RX_RX_EQ_GAIN2_LSB                 QSERDES_RX (0xCC)
#define QSERDES_RX_RX_EQ_GAIN2_MSB                 QSERDES_RX (0xD0)
#define QSERDES_RX_RX_EQU_ADAPTOR_CNTRL2           QSERDES_RX (0xD8)
#define QSERDES_RX_SIGDET_CNTRL                    QSERDES_RX (0x114)
#define QSERDES_RX_SIGDET_LVL                      QSERDES_RX (0x118)
#define QSERDES_RX_SIGDET_DEGLITCH_CNTRL           QSERDES_RX (0x11C)
#define QSERDES_RX_RX_INTERFACE_MODE               QSERDES_RX (0x12C)

//
// rate_A table: the full calibration sequence written before SERDES start.
// Order preserved from the kernel driver.
//
STATIC CONST MSM8996_UFS_PHY_CAL_ENTRY mMsm8996UfsPhyCalTableRateA[] = {
  { UFS_PHY_POWER_DOWN_CONTROL,               0x01 },
  { QSERDES_COM_CMN_CONFIG,                   0x0e },
  { QSERDES_COM_SYSCLK_EN_SEL,                0xd7 },
  { QSERDES_COM_CLK_SELECT,                   0x30 },
  { QSERDES_COM_SYS_CLK_CTRL,                 0x06 },
  { QSERDES_COM_BIAS_EN_CLKBUFLR_EN,          0x08 },
  { QSERDES_COM_BG_TIMER,                     0x0a },
  { QSERDES_COM_HSCLK_SEL,                    0x05 },
  { QSERDES_COM_CORECLK_DIV,                  0x0a },
  { QSERDES_COM_CORECLK_DIV_MODE1,            0x0a },
  { QSERDES_COM_LOCK_CMP_EN,                  0x01 },
  { QSERDES_COM_VCO_TUNE_CTRL,                0x10 },
  { QSERDES_COM_RESETSM_CNTRL,                0x20 },
  { QSERDES_COM_CORE_CLK_EN,                  0x00 },
  { QSERDES_COM_LOCK_CMP_CFG,                 0x00 },
  { QSERDES_COM_VCO_TUNE_TIMER1,              0xff },
  { QSERDES_COM_VCO_TUNE_TIMER2,              0x3f },
  { QSERDES_COM_VCO_TUNE_MAP,                 0x14 },
  { QSERDES_COM_SVS_MODE_CLK_SEL,             0x05 },
  { QSERDES_COM_DEC_START_MODE0,              0x82 },
  { QSERDES_COM_DIV_FRAC_START1_MODE0,        0x00 },
  { QSERDES_COM_DIV_FRAC_START2_MODE0,        0x00 },
  { QSERDES_COM_DIV_FRAC_START3_MODE0,        0x00 },
  { QSERDES_COM_CP_CTRL_MODE0,                0x0b },
  { QSERDES_COM_PLL_RCTRL_MODE0,              0x16 },
  { QSERDES_COM_PLL_CCTRL_MODE0,              0x28 },
  { QSERDES_COM_INTEGLOOP_GAIN0_MODE0,        0x80 },
  { QSERDES_COM_INTEGLOOP_GAIN1_MODE0,        0x00 },
  { QSERDES_COM_VCO_TUNE1_MODE0,              0x28 },
  { QSERDES_COM_VCO_TUNE2_MODE0,              0x02 },
  { QSERDES_COM_LOCK_CMP1_MODE0,              0xff },
  { QSERDES_COM_LOCK_CMP2_MODE0,              0x0c },
  { QSERDES_COM_LOCK_CMP3_MODE0,              0x00 },
  { QSERDES_COM_DEC_START_MODE1,              0x98 },
  { QSERDES_COM_DIV_FRAC_START1_MODE1,        0x00 },
  { QSERDES_COM_DIV_FRAC_START2_MODE1,        0x00 },
  { QSERDES_COM_DIV_FRAC_START3_MODE1,        0x00 },
  { QSERDES_COM_CP_CTRL_MODE1,                0x0b },
  { QSERDES_COM_PLL_RCTRL_MODE1,              0x16 },
  { QSERDES_COM_PLL_CCTRL_MODE1,              0x28 },
  { QSERDES_COM_INTEGLOOP_GAIN0_MODE1,        0x80 },
  { QSERDES_COM_INTEGLOOP_GAIN1_MODE1,        0x00 },
  { QSERDES_COM_VCO_TUNE1_MODE1,              0xd6 },
  { QSERDES_COM_VCO_TUNE2_MODE1,              0x00 },
  { QSERDES_COM_LOCK_CMP1_MODE1,              0x32 },
  { QSERDES_COM_LOCK_CMP2_MODE1,              0x0f },
  { QSERDES_COM_LOCK_CMP3_MODE1,              0x00 },
  { QSERDES_TX_HIGHZ_TRANSCEIVER_BIAS_DRVR_EN, 0x45 },
  { QSERDES_TX_LANE_MODE,                     0x02 },
  { QSERDES_RX_SIGDET_LVL,                    0x24 },
  { QSERDES_RX_SIGDET_CNTRL,                  0x02 },
  { QSERDES_RX_RX_INTERFACE_MODE,             0x00 },
  { QSERDES_RX_SIGDET_DEGLITCH_CNTRL,         0x18 },
  { QSERDES_RX_UCDR_FASTLOCK_FO_GAIN,         0x0B },
  { QSERDES_RX_RX_TERM_BW,                    0x5B },
  { QSERDES_RX_RX_EQ_GAIN1_LSB,               0xFF },
  { QSERDES_RX_RX_EQ_GAIN1_MSB,               0x3F },
  { QSERDES_RX_RX_EQ_GAIN2_LSB,               0xFF },
  { QSERDES_RX_RX_EQ_GAIN2_MSB,               0x0F },
  { QSERDES_RX_RX_EQU_ADAPTOR_CNTRL2,         0x0E },
};

//
// rate_B delta: only written when the link negotiates high-speed gears.
// UfsPassThruDxe bring-up runs PWM gear 1, so this table is not used in
// the UEFI bring-up path; kept for completeness.
//
STATIC CONST MSM8996_UFS_PHY_CAL_ENTRY mMsm8996UfsPhyCalTableRateB[] = {
  { QSERDES_COM_VCO_TUNE_MAP,                 0x54 },
};

#endif /* __MSM8996_UFS_PHY_H__ */
