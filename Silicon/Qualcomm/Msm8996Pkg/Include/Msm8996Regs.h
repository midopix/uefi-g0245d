/** @file
 *
 *  MSM8996 (Snapdragon 820/821) silicon register definitions.
 *
 *  PROVENANCE / CONFIDENCE
 *  -----------------------
 *  [VERIFIED]   Cross-checked against this device's own artefacts:
 *               - Resources/DTBs/g0245d.dtb (decompiled to g0245d.dts with dtc)
 *               - UniXNU-g0245d/board.toml
 *               - emmc_appsboot.mbn (Qualcomm aboot/LK)
 *  [UNVERIFIED] Taken from public MSM UARTDM documentation. MUST be
 *               re-confirmed by disassembling aboot before trusting it on
 *               silicon. A wrong offset here silently kills early serial
 *               output, which is exactly the failure mode we most want to
 *               avoid during bring-up.
 *
 *  Copyright (c) Microsoft Corporation.
 *
 *  SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 **/

#ifndef __MSM8996_REGS_H__
#define __MSM8996_REGS_H__

//
// [VERIFIED] GIC-500 -- DTB node interrupt-controller@09bc0000
//   reg = <0x9bc0000 0x10000 0x9c00000 0x100000>
// NOTE: these are NOT the SDM845/NapaliPkg values (0x17A00000/0x17A60000).
//
#define MSM8996_GICD_BASE                0x09BC0000
#define MSM8996_GICD_SIZE                 0x00010000
#define MSM8996_GICR_BASE                 0x09C00000
#define MSM8996_GICR_SIZE                 0x00100000

//
// [VERIFIED] Debug UART -- DTB node serial@075b0000
//   compatible = "qcom,msm-lsuart-v14", status = "ok"
//   BLSP2_UART2. The two other BLSP serials:
//     serial@07570000 -> status = "disabled"
//     serial@07574000 -> status = "ok"
//   Stock cmdline confirms the console: console=ttyHSL0,115200,n8
//
#define MSM8996_UART_BASE                 0x075B0000
#define MSM8996_UART_SIZE                 0x00001000
#define MSM8996_UART_ALT_BASE              0x07574000

//
// [VERIFIED] Architected timer -- DTB timer node
//   clock-frequency = <0x124f800>  == 19,200,000 Hz (19.2 MHz)
//   interrupts = <1 13 f08  1 14 f08  1 11 f08  1 10 f08>
//     => secure=13, non-secure=14, virtual=11, hypervisor=10
//
#define MSM8996_TIMER_CLOCK_HZ            19200000ULL
#define MSM8996_TIMER_SEC_PPI             13
#define MSM8996_TIMER_NS_PPI              14
#define MSM8996_TIMER_VIRT_PPI            11
#define MSM8996_TIMER_HYP_PPI             10

//
// [VERIFIED] CPU topology -- DTB cpus node: 4x "qcom,kryo" in 2 clusters.
//
#define MSM8996_CPU_CORE_COUNT            4
#define MSM8996_CPU_CLUSTER_COUNT         2

//
// [VERIFIED] Contiguous splash / GOP framebuffer carve-out -- DTB reserved-memory:
//   cont_splash_mem@83401000 { reg = <0x0 0x83401000 0x0 0x23ff000>; }
//   => base 0x83401000, length 0x23ff000 (36 MiB).
//   Panel is 1080x1920; 1080*1920*4 = 8,294,400 B (~8 MiB) of that window.
//
#define MSM8996_CONT_SPLASH_BASE          0x83401000
#define MSM8996_CONT_SPLASH_SIZE          0x023FF000

//
// [VERIFIED] UEFI firmware volume window -- board.toml [uefi_fd].
//   Chosen to sit above the aboot image (0x91600000-0x916c4478) and below
//   cont_splash_mem_hdmi (0xb1c00000), so nothing overlaps.
//
#define MSM8996_UEFI_FD_BASE              0x92000000
#define MSM8996_UEFI_FD_SIZE              0x00300000

//
// [VERIFIED] Stock boot image geometry -- board.toml [boot_image] + extracted
//   boot.img header (Android v0). aboot gzip-decompresses the kernel payload to
//   kernel_addr and enters with x0 = appended DTB.
//
#define MSM8996_KERNEL_LOAD_ADDR          0x80008000
#define MSM8996_RAMDISK_LOAD_ADDR         0x81000000
#define MSM8996_TAGS_LOAD_ADDR            0x80000100
#define MSM8996_BOOTIMG_PAGE_SIZE         4096

//
// Qualcomm UARTDM (low-speed UART, "qcom,msm-lsuart-v14") register offsets.
//
// [VERIFIED 2026-10-27] These offsets now follow Linux v5.10
// drivers/tty/serial/msm_serial.c (the uartdm 1.4 map this silicon uses),
// replacing the earlier UNVERIFIED guesses. IMPORTANT quirks of uartdm:
//   * 0x0008 reads as SR (status) and is WRITTEN as CSR (baud select).
//   * TX/RX FIFO both live at 0x0070 (not 0x0C/0x20 as guessed before --
//     the old guess would have hung the polling TX loop on real hardware).
//
#define UARTDM_MR1                        0x0000
#define UARTDM_MR2                        0x0004
#define UARTDM_SR                         0x0008   // read = status
#define UARTDM_CSR                        0x0008   // write = baud select
#define UARTDM_CR                         0x0010
#define UARTDM_IMR                        0x0014
#define UARTDM_IPR                        0x0018
#define UARTDM_TFWR                       0x001C
#define UARTDM_RFWR                       0x0020
#define UARTDM_DMRX                       0x0034
#define UARTDM_DMEN                       0x003C
#define UARTDM_NCF_TX                     0x0040
#define UARTDM_RXFS                       0x0050
#define UARTDM_TF                         0x0070
#define UARTDM_RF                         0x0070

// UARTDM_SR bits (msm_serial.c).
#define UARTDM_SR_RX_READY                 0x01
#define UARTDM_SR_TX_READY                 0x04
#define UARTDM_SR_TX_EMPTY                 0x08
#define UARTDM_SR_OVERRUN                  0x10

// UARTDM_CR command field (bits 7:4), enable bits (bits 3:0).
#define UARTDM_CR_CMD_RESET_RX             (1 << 4)
#define UARTDM_CR_CMD_RESET_TX             (2 << 4)
#define UARTDM_CR_CMD_RESET_ERR            (3 << 4)
#define UARTDM_CR_CMD_RESET_BREAK_INT      (4 << 4)
#define UARTDM_CR_CMD_RESET_CTS            (7 << 4)
#define UARTDM_CR_CMD_RESET_STALE_INT      (8 << 4)
#define UARTDM_CR_CMD_RESET_RFR            (14 << 4)
#define UARTDM_CR_CMD_PROTECTION_EN        (16 << 4)
#define UARTDM_CR_CMD_STALE_EVENT_ENABLE   (80 << 4)
#define UARTDM_CR_TX_ENABLE                0x04
#define UARTDM_CR_RX_ENABLE                0x01

// UARTDM_MR2: 8 data bits, 1 stop bit, no parity.
#define UARTDM_MR2_8N1                     0x3C

// UARTDM_CSR codes: baud = clk / 16 / divisor (msm_serial.c baud table).
// For 115200 @ 7.3728 MHz the divisor is 4 => CSR 0xCC, rxstale 6.
#define UARTDM_CSR_115200                  0xCC
#define UARTDM_IPR_RXSTALE_6               0x18   // (6 << 2) | mask msb

//
// ---------------------------------------------------------------------------
// Global Clock Controller (GCC)
// ---------------------------------------------------------------------------
// [VERIFIED] GCC block base -- DTB "clock-controller@300000",
//   compatible = "qcom,gcc-msm8996", reg = <0x300000 0x90000>.
// All offsets below are the enable/halt (CBCR), RCG2 command and GDSC
// registers extracted from Linux v4.9 drivers/clk/qcom/gcc-msm8996.c
// (the clock plan this silicon was taped out against), cross-checked
// against the GCC reset map. These are silicon constants, not board
// constants, so kernel provenance is sufficient.
//
// [FIXED 2026-10-28] This constant used to read 0x03000000 -- one extra hex
// digit (16x too high).  With that value the *first* MMIO access in SEC
// (UartColdInit -> CBCR read at 0x03000000 + 0x29004 = 0x03290004) hit an
// unmapped physical address; with the MMU off that is an external abort with
// no vector to catch it, so the board dropped straight into EDL (05c6:900e)
// before a single line of serial output.  The DTB of THIS device says
// clock-controller@300000 / reg = <0x0 0x300000 0x0 0x90000>.
#define MSM8996_GCC_BASE                  0x00300000
#define MSM8996_GCC_SIZE                  0x00090000

// CBCR / branch enable registers (BIT0 = clock enable, poll BIT31 halt).
// [VERIFIED 2026-10-27] BLSP2 UART offsets cross-checked against Linux v4.9
// gcc-msm8996.c: gcc_blsp2_ahb_clk  (halt 0x25004, voted, enable 0x52004
// BIT15), gcc_blsp2_sleep_clk (enable 0x52004 BIT14),
// gcc_blsp2_uart2_apps_clk (0x29004, BIT0), blsp2_uart2_apps_clk_src
// (cmd_rcgr 0x2900C, mnd_width 16, parent map gcc_xo_gpll0).
#define GCC_APCS_CLOCK_BRANCH_ENA_VOTE     0x00052004
#define GCC_VOTE_BLSP2_AHB                  (1 << 15)
#define GCC_VOTE_BLSP2_SLEEP                (1 << 14)
#define GCC_BLSP2_AHB_CBCR_HALT            0x00025004
#define GCC_BLSP2_UART2_APPS_CBCR          0x00029004
#define GCC_BLSP2_UART2_APPS_CMD_RCGR      0x0002900C
#define GCC_UFS_GDSCR                     0x00075004   // GDSC power domain
#define GCC_UFS_AXI_CBCR                  0x00075008
#define GCC_UFS_AHB_CBCR                  0x0007500C
#define GCC_UFS_TX_CFG_CBCR               0x00075010
#define GCC_UFS_RX_CFG_CBCR               0x00075014
#define GCC_UFS_TX_SYMBOL_0_CBCR          0x00075018
#define GCC_UFS_RX_SYMBOL_0_CBCR          0x0007501C
#define GCC_UFS_RX_SYMBOL_1_CBCR          0x00075020
#define GCC_UFS_AXI_CMD_RCGR              0x00075024
#define GCC_UFS_BCR                       0x00075000   // block reset
#define GCC_SYS_NOC_UFS_AXI_CBCR          0x00075038
#define GCC_UFS_ICE_CORE_CBCR             0x00076010
#define GCC_UFS_UNIPRO_CORE_CBCR          0x0007600C
#define GCC_UFS_ICE_CORE_CMD_RCGR         0x00076014

// RCG2 registers are addressed relative to CMD_RCGR (Linux clk-rcg2.c):
//   CMD 0x0, CFG 0x4, M 0x8, N 0xC, D 0x10.
// NOTE: the previous 0x14 CFG offset was wrong -- CFG lives at +0x4.
#define RCG2_CMD_OFFSET                    0x00
#define RCG2_CFG_OFFSET                    0x04   // CFG_RCGR
#define RCG2_M_OFFSET                      0x08
#define RCG2_N_OFFSET                      0x0C
#define RCG2_D_OFFSET                      0x10
#define RCG2_CMD_UPDATE                    0x01   // CMD.UPDATE: trigger re-config
#define RCG2_CMD_ROOT_EN                   0x02

// RCG2 CFG fields: SRC select is bits [10:8]. gcc_xo_gpll0 parent map:
//   {P_XO, 0}, {P_GPLL0, 2}  (Linux gcc-msm8996.c gcc_xo_gpll0_map).
#define RCG2_SRC_XO                        0x0
#define RCG2_SRC_GPLL0                     0x2

// GDSC bits.
//
// [FIXED 2026-10-29 v31] SW_COLLAPSE 原来是 0x0，于是
//   MmioWrite32 (GDSCR, Gdscr & ~GDSC_SW_COLLAPSE) 变成"原样写回"，
//   掉电的电源域永远扶不起来，后面的等待只能超时。
//   Linux drivers/clk/qcom/gdsc.c：SW_COLLAPSE_MASK = BIT(0)，写 0 = 上电。
//   POWER_ON 状态位 BIT(15) 是反证过的：显示驱动靠它判断 MDSS 已被
//   bootloader 上电并走热路径，而屏幕确实是亮的。
#define GDSC_SW_COLLAPSE                   (1 << 0)
#define GDSC_POWER_ON_STATUS               (1 << 15)

// CBCR halt status bit (poll until 0 = clock running).
#define CBCR_CLK_OFF                       (1 << 31)

//
// ---------------------------------------------------------------------------
// UFS host controller (UFSHCI) -- standard JEDEC register space
// ---------------------------------------------------------------------------
// [VERIFIED] DTB node ufshc@624000:
//   compatible = "jedec,ufs-1.1"  <-- STANDARD UFSHCI 1.1 register layout,
//   so the EDK2 MdeModulePkg UfsPassThruDxe can drive it directly.
//   reg = <0x624000 0x2500>, status = "ok", lanes-per-direction = <1>.
//   freq-table-hz: core_clk_src 100/200 MHz, core_clk_unipro 150/300 MHz.
//
// [FIXED 2026-10-28] Was 0x06240000 (0x624000 x16) -- same extra-digit typo
// as the old GCC base.  DTB: ufshc@624000, reg = <0x0 0x624000 0x0 0x2500>.
#define MSM8996_UFS_HC_BASE               0x00624000
#define MSM8996_UFS_HC_SIZE               0x00002500

//
// UFS PHY -- Qualcomm 14nm QMP
//
// [VERIFIED] DTB node ufsphy@627000:
//   compatible = "qcom,ufs-phy-qmp-14nm", reg = <0x627000 0xda8>, status = "ok"
// Register offsets follow phy-qcom-ufs-qmp-14nm.h (kernel v4.9); the
// calibration values live in Msm8996UfsPhy.h.
//
// [FIXED 2026-10-28] Was 0x06270000 (0x627000 x16).  DTB: phy@627000.
#define MSM8996_UFS_PHY_BASE              0x00627000
#define MSM8996_UFS_PHY_SIZE              0x00000DA8

#define QSERDES_COM(x)                    (0x000 + (x))
#define QSERDES_TX(x)                     (0x400 + (x))
#define QSERDES_RX(x)                     (0x600 + (x))
#define QMP_PHY_TX_LANE0_BASE             0x400
#define QMP_PHY_RX_LANE0_BASE             0x600

// PHY control registers (PHY_OFF in kernel terms, base 0xC00).
#define UFS_PHY_PHY_START                 0x0C00
#define UFS_PHY_POWER_DOWN_CONTROL        0x0C04
#define UFS_PHY_PCS_READY_STATUS          0x0D68   // PHY_OFF(0x168)
#define QMP_SERDES_START_BIT              0x1
#define QMP_PCS_READY_MASK                0x1

//
// ---------------------------------------------------------------------------
// MMSS clock controller (mmcc-msm8996.c) + MDSS GDSCs
// ---------------------------------------------------------------------------
// [VERIFIED 2026-10-27] Offsets from Linux v4.9 mmcc-msm8996.c. MMSS CC base
// 0x8C0000 comes from the DTB mmss clock node; all offsets below are the
// halt/enable registers of the MDSS branches and the two MDSS GDSCs
// (gdsc_mdss 0x2304 with cxcs {0x2310, 0x231c}, gdsc_mmagic_mdss 0x247C).
// Block bases (DTB): MDP 0x900000, DSI ctrl0 0x994000, DSI phy 0x994400,
// DSI lane 0x994700 (phy + 0x300), DSI PLL 0x994a00 (phy + 0x600),
// mmss_misc 0x828000.
//
// [FIXED 2026-10-28] Was 0x08C00000 (0x8C0000 x16) -- MMSS CC GDSC and CBCR
// writes went 16x out of range, so the display power domain never came up.
// DTB: clock-controller@8c0000, reg = <0x0 0x8c0000 0x0 0xb000>.
#define MSM8996_MMSS_CC_BASE              0x008C0000

#define MMSS_MDSS_GDSCR                   0x002304
#define MMSS_MMAGIC_MDSS_GDSCR            0x00247C
#define MMSS_MMAGIC_MDSS_AXI_CBCR         0x002474
#define MMSS_MMAGIC_MDSS_NOC_CFG_AHB_CBCR 0x002478
#define MMSS_MDSS_AHB_CBCR                0x002308
#define MMSS_MDSS_AXI_CBCR                0x002310
#define MMSS_MDSS_PCLK0_CBCR              0x002314
#define MMSS_MDSS_MDP_CBCR                0x00231C
#define MMSS_MDSS_VSYNC_CBCR              0x002328
#define MMSS_MDSS_BYTE0_CBCR              0x00233C
#define MMSS_MDSS_ESC0_CBCR               0x002344

// RCG2 command registers inside MMSS CC.
#define MMSS_AXI_CLK_SRC_CMD_RCGR         0x005040   // parent mmpll0, /2
#define MMSS_MDP_CLK_SRC_CMD_RCGR         0x002040   // parent mmpll0
#define MMSS_PCLK0_CLK_SRC_CMD_RCGR       0x002000   // parent dsi0pll
#define MMSS_BYTE0_CLK_SRC_CMD_RCGR       0x002120   // parent dsibyte
#define MMSS_ESC0_CLK_SRC_CMD_RCGR        0x002160   // parent XO

// GDSC bits (same layout as GCC GDSCs).
// MDSS block bases (from the DTB qcom,mdss_mdp@900000 node).
#define MSM8996_MDP_BASE                  0x00900000
#define MSM8996_MDP_REG_OFFSET            0x00001000  // qcom,mdss-mdp-reg-offset
#define MSM8996_MDP_CORE_BASE             (MSM8996_MDP_BASE + MSM8996_MDP_REG_OFFSET)
#define MSM8996_DSI0_CTRL_BASE            0x00994000
#define MSM8996_DSI0_PHY_BASE             0x00994400
#define MSM8996_DSI0_LANE_BASE            0x00994700  // REG_DSI_14nm_PHY_LN(n) = 0x80*n
#define MSM8996_DSI0_PLL_BASE             0x00994A00  // 14nm PLL regs, phy + 0x600
#define MSM8996_MMSS_MISC_BASE            0x00828000
#define MSM8996_VBIF_BASE                 0x009B0000

//
// TLMM (pinctrl@01010000, qcom,msm8996-pinctrl, reg 0x1010000 0x300000).
// GPIO n register block = base + n * 0x1000.
// [VERIFIED] DSI reset = TLMM GPIO 8 (qcom,platform-reset-gpio in the
// mdss_dsi_ctrl0 node, pinctrl mdss_dsi_active drives gpio8).
//
#define MSM8996_TLMM_BASE                 0x01010000
#define MSM8996_TLMM_GPIO_SIZE            0x1000
#define MSM8996_DSI_RESET_GPIO            8

// TLMM GPIO register offsets within a pin block.
#define TLMM_GPIO_CFG(n)                  (MSM8996_TLMM_BASE + (n) * MSM8996_TLMM_GPIO_SIZE + 0x0000)
#define TLMM_GPIO_IN_OUT(n)               (MSM8996_TLMM_BASE + (n) * MSM8996_TLMM_GPIO_SIZE + 0x0004)
// GPIO_CFG bits: value<1:0> function, 5:3 drive strength, 6 pull, 9 output enable? For msm8996:
//   bit 9 = OE (output enable), bits 2:0 func, 5:3 drv, 7:6 pull.
#define TLMM_GPIO_CFG_FUNC_GPIO           0x0
#define TLMM_GPIO_CFG_OE                  (1 << 9)
#define TLMM_GPIO_CFG_DRV_8MA             (2 << 3)

//
// ---------------------------------------------------------------------------
// SPMI PMIC arbiter v2 (DTB qcom,spmi@400f000)
// ---------------------------------------------------------------------------
// [VERIFIED] reg = <0x400f000 0x1000 0x4400000 0x800000 0x4c00000 0x800000
// 0x5800000 0x200000 0x400a000 0x2100>, ee = 0, channel 0.
// Protocol per Linux v4.9 drivers/spmi/spmi-pmic-arb.c (v2 layout).
//
#define MSM8996_SPMI_CORE_BASE            0x00400F000
#define MSM8996_SPMI_CHNLS_BASE           0x04400000
#define MSM8996_SPMI_OBSRVR_BASE          0x04C00000
#define MSM8996_SPMI_CNFG_BASE            0x00400A000

// Channel register offsets (relative to the channel window).
#define SPMI_CH_CMD                       0x00
#define SPMI_CH_CONFIG                    0x04
#define SPMI_CH_STATUS                    0x08
#define SPMI_CH_WDATA0                    0x10
#define SPMI_CH_WDATA1                    0x14
#define SPMI_CH_RDATA0                    0x18
#define SPMI_CH_RDATA1                    0x1C

// v2 channel window: 0x1000 * ee + 0x8000 * chan (ee = 0 here).
#define SPMI_V2_CHAN_STRIDE               0x8000
#define SPMI_V2_EE_STRIDE                 0x1000

// PPID -> channel mapping table lives in CORE space at 0x800 + 4*chan.
#define SPMI_REG_CHNL(chan)               (0x800 + 4 * (chan))
#define SPMI_PPID_MASK                    0xFFF
//
// [FIXED 2026-10-29 v31] 原来是 512，与 DTB 不符。
//   DTB spmi@400f000 声明 qcom,pmic-arb-max-peripherals = <0x100> = 256，
//   且通道窗口大小 0x800000 / 步长 0x8000 = 256 —— 三个数字互相印证。
//   取 512 时 SpmiFindChannel 之后 Off = 511 * 0x8000 = 0x3F80000 会跑出
//   8 MB 窗口，读到的就是未映射地址（又一次外部中止）。
#define SPMI_MAX_PERIPHS                  256

// Status bits.
#define SPMI_STATUS_DONE                  (1 << 0)
#define SPMI_STATUS_FAILURE               (1 << 1)
#define SPMI_STATUS_DENIED                (1 << 2)
#define SPMI_STATUS_DROPPED               (1 << 3)

// Operation codes (v2 fmt: (opc << 27) | ((addr & 0xff) << 4) | (bc & 7)).
#define SPMI_OP_EXT_WRITE                 2
#define SPMI_OP_EXT_READ                  13
#define SPMI_OP_WRITE                     14
#define SPMI_OP_READ                      15

//
// ---------------------------------------------------------------------------
// PM8994 (slave id 0) regulator peripherals -- qcom_spmi-regulator.c
// ---------------------------------------------------------------------------
// [VERIFIED] Peripheral bases from the mainline pm8994_regulators[] table.
// Common register layout (offsets within a 0x100 periph):
//   0x40 VOLTAGE_RANGE, 0x41 VOLTAGE_SET, 0x45 MODE, 0x46 ENABLE (bit7).
//
#define PM8994_SID                        0

//
// PM8994 PON (qcom,power-on@800 in Resources/DTBs/g0245d.dtb).
//   pon_1: qcom,pon-type = 0 (KPDPWR), linux,code 116 KEY_POWER
//   pon_2: qcom,pon-type = 1 (RESIN),  linux,code 114 KEY_VOLUMEDOWN
// INT_RT_STS @ 0x10 is the REAL-TIME (non-latched) interrupt status.  Its
// active polarity is deliberately NOT hard-coded: KeypadDeviceLib snapshots
// this register at boot and treats any bit that differs from the snapshot as
// "pressed" (see KeypadDeviceLib.c).  Absolute address = 0x810.
//
#define PM8994_PON_BASE                   0x0800
#define PM8994_PON_REG_INT_RT_STS         0x10
#define PON_KPDPWR_N_BIT                  (1 << 0)
#define PON_RESIN_N_BIT                   (1 << 1)

#define PM8994_L2_PERIPH                  0x4100   // dsi vdda 1.25 V   (N600)
#define PM8994_L12_PERIPH                 0x4B00   // ufs vdda-pll 1.8 V (P600)
#define PM8994_L14_PERIPH                 0x4D00   // dsi vddio 1.8 V   (P600)
#define PM8994_L25_PERIPH                 0x5800   // ufs vccq 1.2 V    (N600)
#define PM8994_L28_PERIPH                 0x5B00   // ufs vdda-phy 0.925 V (N300)
#define PM8994_S4_PERIPH                  0x1D00   // ufs vccq2 1.8 V   (SMPS)
#define PM8994_GPIO_BASE                  0xC000   // gpio n: +0x100*(n-1)

// Regulator common registers.
#define PMIC_REG_VOLTAGE_RANGE            0x40
#define PMIC_REG_VOLTAGE_SET              0x41
#define PMIC_REG_MODE                     0x45
#define PMIC_REG_ENABLE                   0x46
#define PMIC_REG_TYPE                     0x04
#define PMIC_REG_SUBTYPE                  0x05
#define PMIC_REG_DIG_MAJOR_REV            0x01

#define PMIC_ENABLE_BIT                   0x80
#define PMIC_MODE_HPM                     0x80

// Subtypes (spmi_regulator_subtype in qcom_spmi-regulator.c).
#define PMIC_SUBTYPE_N300                 0x03
#define PMIC_SUBTYPE_N600                 0x04
#define PMIC_SUBTYPE_N1200                0x05
#define PMIC_SUBTYPE_N600_ST              0x06
#define PMIC_SUBTYPE_N1200_ST             0x07
#define PMIC_SUBTYPE_P50                  0x08
#define PMIC_SUBTYPE_P150                 0x09
#define PMIC_SUBTYPE_P300                 0x0A
#define PMIC_SUBTYPE_P600                 0x0B
#define PMIC_SUBTYPE_P1200                0x0C
#define PMIC_SUBTYPE_LN                   0x10
#define PMIC_SUBTYPE_LV_P50               0x28
#define PMIC_SUBTYPE_LV_P150              0x29
#define PMIC_SUBTYPE_LV_P300              0x2A
#define PMIC_SUBTYPE_LV_P600              0x2B
#define PMIC_SUBTYPE_LV_P1200             0x2C
#define PMIC_SUBTYPE_GP_CTL               0x08   // BUCK/SMPS

// PMIC GPIO (pinctrl-spmi-gpio.c) registers within a 0x100 periph.
// RT_STS @ 0x10 bit0 = CURRENT pad level (what gpio-keys polls).  The board's
// gpio_keys entries all carry flags 0x1 = GPIO_ACTIVE_LOW, so a PRESSED key
// reads 0 here.  Confirmed from Resources/DTBs/g0245d.dtb via
// tools/dump_gpio_keys.py: gpios phandle 0xf9 -> .../pm8994@0/gpios.
//
// v106: cross-checked against Linux drivers/pinctrl/qcom/pinctrl-spmi-gpio.c.
// Reading 0x10 IS correct (PMIC_MPP_REG_RT_STS, used by pmic_gpio_get), but a
// usable input pad ALSO needs DIG_IN_CTL (0x43) = 0: it selects the physical
// pin instead of a DTEST buffer.  A bootloader leftover DTEST mux makes
// RT_STS report a constant internal value -- the "no key edge" symptom seen
// on the 2026-11-09 device boot.  DIG_VIN_CTL (0x41) is deliberately left
// at the bootloader value; this board's gpio_keys DT has no vin/pinctrl data.
#define PMIC_GPIO_REG_RT_STS              0x10
#define PMIC_GPIO_REG_MODE_CTL            0x40
#define PMIC_GPIO_REG_DIG_VIN_CTL         0x41
#define PMIC_GPIO_REG_DIG_PULL_CTL        0x42
#define PMIC_GPIO_REG_DIG_IN_CTL          0x43
#define PMIC_GPIO_REG_DIG_OUT_CTL         0x45
#define PMIC_GPIO_REG_EN_CTL              0x46
#define PMIC_GPIO_MODE_DIGITAL_OUTPUT     (1 << 4)
#define PMIC_GPIO_PULL_DOWN               4
#define PMIC_GPIO_PULL_DISABLE            5
#define PMIC_GPIO_MASTER_EN               0x80

// Backlight enable GPIO on the PMIC (qcom,platform-bklight-en-gpio).
#define PM8994_BKLIGHT_GPIO               14

//
// PM8994 WLED (white-LED boost for panel backlight).
//
// g0245d's ft8716 node says qcom,mdss-dsi-bl-pmic-control-type = "bl_ctrl_dcs",
// and its panel-supply-entries list "lab"/"ibb" (4.6-6.0 V) from this block.
// There is NO discrete backlight-enable GPIO on this board: the only way to
// light the panel is to bring up this boost. Layout follows
// drivers/video/backlight/qpnp-wled.c (ctrl @ 0xD800, sink @ 0xD900).
//
#define PM8994_WLED_CTRL_BASE             0xD800
#define PM8994_WLED_SINK_BASE             0xD900
#define PM8994_WLED_IBB_BASE              0xDC00
#define PM8994_WLED_LAB_BASE              0xDE00

// ctrl-block registers (offset from PM8994_WLED_CTRL_BASE)
#define WLED_CTRL_DISP_SEL                0x44   // bit7: 1 = AMOLED, 0 = LCD
#define WLED_CTRL_MODULE_EN               0x46   // bit7: boost enable
#define WLED_CTRL_FDBK_OP                 0x48
#define WLED_CTRL_VREF                    0x49
#define WLED_CTRL_BOOST_DUTY              0x4B
#define WLED_CTRL_SWITCH_FREQ             0x4C
#define WLED_CTRL_OVP                     0x4D
#define WLED_CTRL_ILIM                    0x4E
#define WLED_CTRL_SOFTSTART_RAMP          0x53
#define WLED_CTRL_SC_PRO                  0x5E
#define WLED_SEC_ACCESS                   0xD0   // write 0xA5 to unlock

// sink-block registers (offset from PM8994_WLED_SINK_BASE)
#define WLED_SINK_CURR_SINK               0x46   // bit(4 + string)
#define WLED_SINK_SYNC                    0x47   // 0x0F latch, 0x00 reset
#define WLED_SINK_MOD                     0x4A
#define WLED_SINK_HYB_THRES               0x4B
#define WLED_SINK_TEST5                   0xE6
// per-string block: PM8994_WLED_SINK_BASE + 0x50 + n * 0x10  (n = 1..4)
#define WLED_SINK_STRIDE                  0x10
#define WLED_SINK_MOD_EN                  0x00   // bit7: modulator enable
#define WLED_SINK_SYNC_DLY                0x01
#define WLED_SINK_FS_CURR                 0x02
#define WLED_SINK_CABC                    0x06
#define WLED_SINK_BRIGHT_LSB              0x07
#define WLED_SINK_BRIGHT_MSB              0x08

#define WLED_SEC_UNLOCK_KEY               0xA5
#define WLED_SYNC_LATCH                   0x0F
#define WLED_SYNC_RESET                   0x00
#define WLED_TEST5_HYBRID                 0x14
#define WLED_SWITCH_FREQ_800KHZ_CODE      0x0B
#define WLED_MAX_LEVEL                    4095

#endif /* __MSM8996_REGS_H__ */
