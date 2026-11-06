/** @file
 *
 *  Memory map for the Gree G0245D (Qualcomm MSM8996 / Snapdragon 820).
 *
 *  HOW THIS MAP WAS DERIVED
 *  ------------------------
 *  Fixed / verified anchors (from the device's own artefacts -- do not move
 *  them without re-verifying against the same sources):
 *
 *    [board.toml  uefi_fd.base/size]  UEFI FD window  0x92000000, 0x00300000
 *    [DTB reserved-memory]             cont_splash_mem
 *                                        base 0x83401000, len 0x023FF000
 *    [DTB interrupt-controller@09bc0000]
 *                                       GICD 0x09BC0000, GICR 0x09C00000
 *    [DTB serial@075b0000]            debug UART 0x075B0000, size 0x1000
 *    [board.toml  boot_image]         kernel 0x80008000, ramdisk 0x81000000
 *
 *  Everything else (heap / stack / page-table placement) is chosen to pack
 *  around those anchors without overlapping them. The ordering below matters:
 *  the SEC heap must sit below the FV region, and the stack immediately after
 *  the page tables, or the MMU bring-up will scribble over live data.
 *
 *  Copyright (c) Microsoft Corporation.
 *
 *  SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 **/

#include <Library/MemoryMapLib.h>

STATIC
EFI_MEMORY_REGION_DESCRIPTOR
gMemoryDescriptor[] = {
  // Name,                Address,     Length,     HobOption, ResourceType, ResourceAttribute, MemoryType, ArmAttributes

  // ---- DDR regions (anchors verified above) ----
  {"Kernel",             0x80000000, 0x03400000, AddMem, SYS_MEM, SYS_MEM_CAP,      Reserv, WRITE_BACK_XN},
  // Extra page right before the scanout carveout: the flat-byte-stream
  // fetch begins (ColumnOffset * bpp) = 801 bytes before the framebuffer
  // base, so the blitter writes the bottom-row spillover here.  The
  // "Display Reserved" region itself must keep its original 0x83401000
  // base -- SimpleFbDxe takes its framebuffer address from that region's
  // Address field (v69 moved it and the image shifted a whole 4K page).
  // v75: write-back (was WRITE_THROUGH_XN).  The scanout fetch starved under
  // the v74 uncached per-pixel menu redraws (persistent zone distortion in the
  // photo); write-back makes blits short bursts, coherency is kept by the
  // per-blit WriteBackInvalidate in SimpleFb / FrameBufferSerialPortLib.
  {"Display Reserved Pre", 0x83400000, 0x00001000, AddMem, MEM_RES, SYS_MEM_CAP, Reserv, WRITE_BACK_XN},
  {"Display Reserved",   0x83401000, 0x023FF000, AddMem, MEM_RES, SYS_MEM_CAP,      Reserv, WRITE_BACK_XN},
  {"SMEM",               0x86000000, 0x00200000, AddMem, MEM_RES, UNCACHEABLE,      Reserv, UNCACHED_UNBUFFERED_XN},
  {"FV Region",          0x8A000000, 0x02000000, AddMem, SYS_MEM, SYS_MEM_CAP,      BsData, WRITE_BACK_XN},
  {"ABOOT FV",           0x8C000000, 0x00200000, AddMem, SYS_MEM, SYS_MEM_CAP,      Reserv, WRITE_BACK_XN},

  // ---- UEFI firmware volume (board.toml [uefi_fd]) ----
  {"UEFI FD",            0x92000000, 0x00300000, AddMem, SYS_MEM, SYS_MEM_CAP,      BsData, WRITE_BACK},

  // ---- Runtime scratch regions (placed above the FD to stay clear of it) ----
  // [FIXED 2026-10-28] DXE Heap used to start at 0x93000000, i.e. INSIDE the
  // "UEFI FD" window (0x92000000 + 0x300000 = 0x95000000).  The heap was
  // therefore aliased onto two megabytes of the firmware volume that is
  // still being read (SEC decompresses FVMAIN from it, DXE dispatches the
  // drivers out of it).  The first big allocation would overwrite live
  // firmware code.  Start it at the first byte after the FD instead.
  {"DXE Heap",           0x95000000, 0x06000000, AddMem, SYS_MEM, SYS_MEM_CAP,      Conv,   WRITE_BACK_XN},
  {"SEC Heap",           0x9F000000, 0x0008C000, AddMem, SYS_MEM, SYS_MEM_CAP,      BsData, WRITE_BACK_XN},
  {"MMU PageTables",     0x9F08D000, 0x00003000, AddMem, SYS_MEM, SYS_MEM_CAP,      BsData, WRITE_BACK_XN},
  {"UEFI Stack",         0x9F090000, 0x00040000, AddMem, SYS_MEM, SYS_MEM_CAP,      BsData, WRITE_BACK_XN},
  {"Log Buffer",         0x9F0F7000, 0x00008000, AddMem, SYS_MEM, SYS_MEM_CAP,      RtData, WRITE_BACK_XN},
  {"Info Blk",           0x9F0FF000, 0x00001000, AddMem, SYS_MEM, SYS_MEM_CAP,      RtData, WRITE_BACK_XN},

  // ---- Register regions ----
  // [VERIFIED] GIC: DTB reg = <0x9bc0000 0x10000 0x9c00000 0x100000>
  //   These are deliberately NOT the SDM845 values (0x17A00000/0x17A60000).
  {"APSS_GIC500_GICD",   0x09BC0000, 0x00010000, AddDev, MMAP_IO, UNCACHEABLE,      MmIO,   NS_DEVICE},
  {"APSS_GIC500_GICR",   0x09C00000, 0x00100000, AddDev, MMAP_IO, UNCACHEABLE,      MmIO,   NS_DEVICE},

  // [VERIFIED] Debug UART: DTB serial@075b0000, reg = <0x075b0000 0x1000>
  {"BLSP2_UART2",        0x075B0000, 0x00001000, AddDev, MMAP_IO, UNCACHEABLE,      MmIO,   NS_DEVICE},

  // [VERIFIED] APSS WatchDog Timer: DTB qcom,wdt@9830000, reg = <0x9830000 0x1000>.
  //   PlatformInitialize() disables this via LocateMemoryRegionByName(); if the
  //   region is missing (or the address wrong) the board resets a few seconds
  //   after hand-off. Note this is NOT the SDM845 address (0x0B017000).
  {"APSS_WDT_TMR1",      0x09830000, 0x00001000, AddDev, MMAP_IO, UNCACHEABLE,      MmIO,   NS_DEVICE},

  // [VERIFIED] Global Clock Controller: DTB clock-controller@300000,
  //   reg = <0x0 0x300000 0x0 0x90000>.
  //   Needed from SEC onwards: SerialPortLib votes BLSP2_UART2 through GCC,
  //   and UfsHostDxe powers the UFS GDSC / branches here.  It MUST be in the
  //   map, otherwise the first DEBUG() after MemoryPeim() installs the page
  //   tables faults on an unmapped device window.
  {"APSS_GCC",           0x00300000, 0x00090000, AddDev, MMAP_IO, UNCACHEABLE,      MmIO,   NS_DEVICE},

  // [VERIFIED] UFS host controller + QMP PHY: DTB ufshc@624000 (0x2500) and
  //   phy@627000 (0x1000).  One window covers both (0x624000-0x628000).
  //   UfsHostDxe drives these from DXE, so they must survive MMU turn-on.
  {"UFS_HC_PHY",         0x00624000, 0x00004000, AddDev, MMAP_IO, UNCACHEABLE,      MmIO,   NS_DEVICE},

  // [VERIFIED] MMSS clock controller: DTB clock-controller@8c0000 (0xb000).
  //   DisplayDxe powers the MDSS GDSCs here.
  {"APSS_MMSS_CC",       0x008C0000, 0x00010000, AddDev, MMAP_IO, UNCACHEABLE,      MmIO,   NS_DEVICE},

  // [VERIFIED] MDSS/MDP5 + DSI0: DTB mdss_mdp@900000, dsi ctrl@994000.
  //   Window 0x900000-0xA00000 covers MDP, VBIF, DSI ctrl/PHY/PLL.
  {"MDSS_MDP_DSI",       0x00900000, 0x00100000, AddDev, MMAP_IO, UNCACHEABLE,      MmIO,   NS_DEVICE},

  // [VERIFIED] TLMM/pinctrl: DTB pinctrl@01010000, reg = <0x1010000 0x300000>.
  {"APSS_TLMM",          0x01010000, 0x00300000, AddDev, MMAP_IO, UNCACHEABLE,      MmIO,   NS_DEVICE},

  // [VERIFIED] SPMI PMIC arbiter v2: DTB spmi@400f000 and its CNFG window.
  {"APSS_SPMI_CORE",     0x0400F000, 0x00003000, AddDev, MMAP_IO, UNCACHEABLE,      MmIO,   NS_DEVICE},
  // [FIXED 2026-10-29] Length used to be 0x2100, which is NOT 4 KB aligned.
  //   ArmMmuLib's UpdateRegionMapping() rejects the whole map with
  //   EFI_INVALID_PARAMETER if any single region is not page aligned, so
  //   ArmConfigureMmu() returned an error and the MMU was never turned on at
  //   all (everything then ran uncached, and SEC died right after stage 12).
  //   Round the window up to 0x3000: still inside the SPMI CNFG block, and
  //   it does not reach APSS_SPMI_CORE at 0x0400F000.
  {"APSS_SPMI_CNFG",     0x0400A000, 0x00003000, AddDev, MMAP_IO, UNCACHEABLE,      MmIO,   NS_DEVICE},

  // [FIXED 2026-10-29 v31] SPMI arbiter v2 的另外两段窗口。
  //   DTB spmi@400f000 的 reg 是 5 段：
  //     <0x400f000 0x1000>    core   （PPID -> 通道号映射表）
  //     <0x4400000 0x800000>  chnls  （写事务通道窗口）
  //     <0x4c00000 0x800000>  obsrvr （读事务观察窗口）
  //     <0x5800000 0x200000>  intr
  //     <0x400a000 0x2100>    cnfg
  //   之前只登记了 core 和 cnfg，所以 Msm8996SpmiLib 第一次发命令
  //   （写 0x04C00000 + 通道偏移）就落在未映射页上 -> 同步外部中止 ->
  //   DxeCore 默认异常处理 -> CpuDeadLoop。屏上表现为 UfsHostDxe 的
  //   step 34（冷路径，即将开 PMIC 供电轨）之后彻底静止。
  //   0x800000 = 256 通道 * 0x8000 步长，正好等于 DTB 的窗口大小，
  //   也印证了 qcom,pmic-arb-max-peripherals = <0x100>。
  {"APSS_SPMI_CHNLS",    0x04400000, 0x00800000, AddDev, MMAP_IO, UNCACHEABLE,      MmIO,   NS_DEVICE},
  {"APSS_SPMI_OBSRVR",   0x04C00000, 0x00800000, AddDev, MMAP_IO, UNCACHEABLE,      MmIO,   NS_DEVICE},

  // TODO(REA): remaining MMIO windows (TLMM/GPIO, UFS HC, SMMU, MDSS/MDP5,
  //   CPR, SPMI/PMIC arbiter, QTimer, ...) must be extracted from
  //   emmc_appsboot.mbn / the DTB before they are relied on.  They are
  //   intentionally omitted rather than guessed.
};

/**
  Returns the platform memory map.
**/
VOID
GetMemoryMap (
  OUT EFI_MEMORY_REGION_DESCRIPTOR **MemoryDescriptor,
  OUT UINT8                         *MemoryDescriptorCount
  )
{
  *MemoryDescriptor      = gMemoryDescriptor;
  *MemoryDescriptorCount = ARRAY_SIZE (gMemoryDescriptor);
}
