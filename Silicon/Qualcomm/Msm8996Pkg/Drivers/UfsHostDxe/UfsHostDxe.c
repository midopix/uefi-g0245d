/** @file
 *
 *  MSM8996 UFS host controller platform driver.
 *
 *  RESPONSIBILITY
 *  --------------
 *  1. Bring the UFS subsystem out of reset: GCC GDSC power domain, GCC
 *     clock branches, RCG2 sources and the 14nm QMP UFS PHY.
 *  2. Publish the EDKII UFS HOST CONTROLLER protocol so that the generic
 *     MdeModulePkg UfsPassThruDxe can drive the standard UFSHCI register
 *     space (the DTB declares this controller "jedec,ufs-1.1").
 *
 *  BRING-UP STRATEGY (why everything is probe-then-init)
 *  -----------------------------------------------------
 *  On this device the boot chain is SBL1 -> [UEFI]. SBL1 must have used
 *  UFS itself to load this firmware (the aboot partition lives in UFS),
 *  so at SEC entry the UFS power domain, clocks and PHY are likely
 *  already programmed. Every step below therefore first PROBES the
 *  current state and only reprograms hardware that is demonstrably off.
 *  This is idempotent when handed an already-initialised controller and
 *  still correct when handed a cold one (e.g. after a warm reset that
 *  collapsed the GDSC).
 *
 *  HARDWARE FACTS (all from this device's own DTB, see g0245d.dts)
 *  ----------------------------------------------------------------
 *    ufshc@624000   compatible="jedec,ufs-1.1"  reg=<0x624000 0x2500>
 *                   lanes-per-direction=<1>, status="ok"
 *    ufsphy@627000  compatible="qcom,ufs-phy-qmp-14nm" reg=<0x627000 0xda8>
 *    ufsice@630000  (inline crypto engine -- NOT used; no encrypted
 *                   volumes in the UEFI boot path)
 *    freq-table-hz  core_clk_src 100/200 MHz, core_clk_unipro 150/300 MHz
 *
 *  Copyright (c) 2026, UniXNU contributors.
 *
 *  SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 **/

#include <Uefi.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/IoLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>
#include <Library/CacheMaintenanceLib.h>
#include <Library/Msm8996PmicLib.h>

#include <Protocol/UfsHostController.h>

#include <Msm8996Regs.h>
#include <Msm8996UfsPhy.h>

#define UFS_GCC_BASE                  MSM8996_GCC_BASE
#define UFS_HC_BASE                   MSM8996_UFS_HC_BASE
#define UFS_PHY_BASE                  MSM8996_UFS_PHY_BASE

//
// ---------------------------------------------------------------------------
// [2026-10-29 v30] 驱动自带显存打点（无串口取证用）
// ---------------------------------------------------------------------------
// 屏上读数 = 32 + 步号，正好落在 32..56 区间（DXE 的驱动序号用 64..127，
// PEI 的 ArmMmu 失败码 32..39 早已过去，所以这个区间在 DXE 里是空的，
// 读数落在 32..63 就一定来自本驱动）。
//
// 打点靠直接写 cont_splash_mem（0x83401000）：进 DXE 后该区域由 bootloader
// 留给开机 logo，MDSS 一直在扫描它，所以写进去立刻可见；不依赖 gBS/协议。
// 几何必须与 Sec.c / DxeMain.c 完全一致，否则读数会错位。
//
#define UFS_BEACON_FB        0x83401000ULL
#define UFS_BEACON_STRIDE    4320ULL
#define UFS_BEACON_FIELD     960
#define UFS_BEACON_REF_BOT   90
#define UFS_BEACON_REF_TOP   855
#define UFS_BEACON_REF_ROWS  70
#define UFS_BEACON_BIT_BASE  200
#define UFS_BEACON_BIT_STEP  90
#define UFS_BEACON_BIT_ROWS  38
#define UFS_BEACON_BITS      7

STATIC
VOID
UfsBeaconFillRows (
  IN UINT64  FirstRow,
  IN UINT64  Rows,
  IN UINT64  Value
  )
{
  UINT64           Row;
  UINT64           Index;
  volatile UINT64  *Cursor;

  for (Row = 0; Row < Rows; Row++) {
    Cursor = (volatile UINT64 *)(UINTN)(UFS_BEACON_FB + (FirstRow + Row) * UFS_BEACON_STRIDE);
    for (Index = 0; Index < UFS_BEACON_STRIDE / sizeof (UINT64); Index++) {
      Cursor[Index] = Value;
    }
  }
}

STATIC
VOID
UfsTrace (
  IN UINTN  Stage
  )
{
  UINTN  Bit;

  if (Stage >= (1u << UFS_BEACON_BITS)) {
    return;
  }

  UfsBeaconFillRows (0, UFS_BEACON_FIELD, 0);
  UfsBeaconFillRows (UFS_BEACON_REF_BOT, UFS_BEACON_REF_ROWS, MAX_UINT64);
  UfsBeaconFillRows (UFS_BEACON_REF_TOP, UFS_BEACON_REF_ROWS, MAX_UINT64);

  for (Bit = 0; Bit < UFS_BEACON_BITS; Bit++) {
    if (((Stage >> Bit) & 1u) != 0) {
      UfsBeaconFillRows (
        UFS_BEACON_BIT_BASE + (UINT64)Bit * UFS_BEACON_BIT_STEP,
        UFS_BEACON_BIT_ROWS,
        MAX_UINT64
        );
    }
  }
}

#define PHY_INIT_POLL_MAX_US          1000000   // 1 s, matches kernel
#define PHY_INIT_POLL_STEP_US         10
//
// GDSC power-up and RCG2 update acknowledgements are fast (tens of
// microseconds) when the block is actually clocked.  100 ms is generous
// while still keeping a dead bus from turning into a watchdog reset.
//
#define GDSC_POWER_ON_TIMEOUT_US      100000    // 100 ms

//
// EDKII_UFS_HOST_CONTROLLER_PROTOCOL instance data.
//
typedef struct {
  UINTN                                 Signature;
  EDKII_UFS_HOST_CONTROLLER_PROTOCOL    UfsHc;
} MSM8996_UFS_HC_PRIVATE;

#define MSM8996_UFS_HC_PRIVATE_SIGNATURE  SIGNATURE_32 ('M', '9', 'U', 'H')

//
// Map token: keeps Map()/Unmap() symmetric and records the operation type
// so Unmap() knows which direction to flush.
//
typedef struct {
  UINTN                                  Signature;
  EDKII_UFS_HOST_CONTROLLER_OPERATION    Operation;
  UINTN                                  NumberOfBytes;
  VOID                                   *HostAddress;
  EFI_PHYSICAL_ADDRESS                   DeviceAddress;
} MAP_INFO;

#define MAP_INFO_SIGNATURE  SIGNATURE_32 ('M', '9', 'M', 'P')

//
// ---------------------------------------------------------------------------
// Clock helpers
// ---------------------------------------------------------------------------

/**
  Poll a GCC CBCR until the branch reports running (CLK_OFF bit clears).

  @param[in] CbcrOffset   Offset of the branch control register in GCC.
  @param[in] TimeoutUs    Poll timeout in microseconds.

  @retval EFI_SUCCESS     Clock is running.
  @retval EFI_TIMEOUT     The branch stayed halted.
**/
STATIC
EFI_STATUS
GccWaitBranchOn (
  IN UINT32    CbcrOffset,
  IN UINT32    TimeoutUs
  )
{
  UINT32  Elapsed;

  Elapsed = 0;
  while ((MmioRead32 (UFS_GCC_BASE + CbcrOffset) & CBCR_CLK_OFF) != 0) {
    if (Elapsed >= TimeoutUs) {
      return EFI_TIMEOUT;
    }

    gBS->Stall (PHY_INIT_POLL_STEP_US);
    Elapsed += PHY_INIT_POLL_STEP_US;
  }

  return EFI_SUCCESS;
}

/**
  Enable a GCC clock branch, unless it is already enabled.

  @param[in] CbcrOffset   Offset of the branch control register in GCC.
  @param[in] Name         Register name for debug output.

  @retval EFI_SUCCESS     The branch is enabled and running.
  @retval EFI_TIMEOUT     The branch did not un-halt.
**/
STATIC
EFI_STATUS
GccEnableBranch (
  IN UINT32     CbcrOffset,
  IN CONST CHAR8  *Name
  )
{
  UINT32      Val;
  EFI_STATUS  Status;

  Val = MmioRead32 (UFS_GCC_BASE + CbcrOffset);
  if ((Val & 0x1) != 0) {
    //
    // Already enabled -- leave it alone (SBL1 hand-off path).
    //
    return EFI_SUCCESS;
  }

  MmioWrite32 (UFS_GCC_BASE + CbcrOffset, Val | 0x1);
  Status = GccWaitBranchOn (CbcrOffset, PHY_INIT_POLL_MAX_US);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "%a: %a (0x%x) did not un-halt\n", gEfiCallerBaseName, Name, CbcrOffset));
    return Status;
  }

  DEBUG ((DEBUG_INFO, "%a: enabled %a (0x%x)\n", gEfiCallerBaseName, Name, CbcrOffset));
  return EFI_SUCCESS;
}

/**
  Program the UFS AXI RCG2 for 100 MHz (GPLL0 / 6) and trigger an update.

  Only called on the cold path; if SBL1 already configured the root clock
  we must not disturb it.

  @retval EFI_SUCCESS     RCG2 reconfigured and update acknowledged.
**/
STATIC
EFI_STATUS
GccConfigUfsAxiRcg2 (
  VOID
  )
{
  UINT32  Cmd;
  UINT32  Cfg;
  UINT32  Elapsed;

  Cmd = UFS_GCC_BASE + GCC_UFS_AXI_CMD_RCGR;
  Cfg = MmioRead32 (Cmd + RCG2_CFG_OFFSET);

  //
  // CFG: SRC = GPLL0 (index 2, bits 10:8), pre-div = 6 -> field value 5 at
  // CFG bits [4:0] (CFG_SRC_DIV_SHIFT = 0 per clk-rcg2.c), no MND.
  // 100 MHz is the rate the DTB freq-table declares for gear 1 PWM.
  //
  Cfg = (RCG2_SRC_GPLL0 << 8) | 0x5;
  MmioWrite32 (Cmd + RCG2_CFG_OFFSET, Cfg);
  MmioWrite32 (Cmd, RCG2_CMD_UPDATE);

  //
  // Poll the update bit back to zero (root clock reconfiguration done).
  // Bounded: an unclocked GCC block would otherwise spin here forever.
  //
  Elapsed = 0;
  while ((MmioRead32 (Cmd) & RCG2_CMD_UPDATE) != 0) {
    if (Elapsed >= GDSC_POWER_ON_TIMEOUT_US) {
      DEBUG ((
        DEBUG_ERROR,
        "%a: UFS AXI RCG2 update not acknowledged (CMD=0x%08x)\n",
        gEfiCallerBaseName,
        MmioRead32 (Cmd)
        ));
      return EFI_TIMEOUT;
    }

    gBS->Stall (PHY_INIT_POLL_STEP_US);
    Elapsed += PHY_INIT_POLL_STEP_US;
  }

  return EFI_SUCCESS;
}

//
// ---------------------------------------------------------------------------
// UFS PHY bring-up (cold path only)
// ---------------------------------------------------------------------------

/**
  Apply the 14nm QMP rate-A calibration table.
**/
STATIC
VOID
UfsPhyApplyCalibration (
  VOID
  )
{
  UINTN  Index;

  for (Index = 0; Index < ARRAY_SIZE (mMsm8996UfsPhyCalTableRateA); Index++) {
    MmioWrite32 (
      UFS_PHY_BASE + mMsm8996UfsPhyCalTableRateA[Index].Offset,
      mMsm8996UfsPhyCalTableRateA[Index].Value
      );
  }

  MemoryFence ();
}

/**
  Kick the SERDES and wait for the physical coding sublayer to report ready.

  @retval EFI_SUCCESS     PCS is ready.
  @retval EFI_TIMEOUT     PCS did not report ready within 1 s.
**/
STATIC
EFI_STATUS
UfsPhyStartSerdes (
  VOID
  )
{
  UINT32  Val;
  UINT32  Elapsed;

  Val = MmioRead32 (UFS_PHY_BASE + UFS_PHY_PHY_START);
  MmioWrite32 (UFS_PHY_BASE + UFS_PHY_PHY_START, Val | QMP_SERDES_START_BIT);
  MemoryFence ();

  Elapsed = 0;
  while ((MmioRead32 (UFS_PHY_BASE + UFS_PHY_PCS_READY_STATUS) & QMP_PCS_READY_MASK) == 0) {
    if (Elapsed >= PHY_INIT_POLL_MAX_US) {
      return EFI_TIMEOUT;
    }

    gBS->Stall (PHY_INIT_POLL_STEP_US);
    Elapsed += PHY_INIT_POLL_STEP_US;
  }

  return EFI_SUCCESS;
}

//
// ---------------------------------------------------------------------------
// EDKII_UFS_HOST_CONTROLLER_PROTOCOL
// ---------------------------------------------------------------------------

/**
  Return the MMIO base of the UFS host controller.

  @param[in]  This       Protocol instance.
  @param[out] MmioBar    MMIO base address.

  @retval EFI_SUCCESS            Operation succeeds.
  @retval EFI_INVALID_PARAMETER  A parameter is NULL.
**/
STATIC
EFI_STATUS
EFIAPI
UfsHostDxeGetMmioBar (
  IN     EDKII_UFS_HOST_CONTROLLER_PROTOCOL  *This,
  OUT UINTN                                 *MmioBar
  )
{
  if ((This == NULL) || (MmioBar == NULL)) {
    return EFI_INVALID_PARAMETER;
  }

  *MmioBar = UFS_HC_BASE;
  return EFI_SUCCESS;
}

/**
  Map a host buffer for the controller. The UFSHCI on this SoC is a
  coherent bus master on the system NOC with no boot-time IOMMU stage,
  so device and host addresses are identical.

  @retval EFI_SUCCESS            Operation succeeds.
  @retval EFI_INVALID_PARAMETER  A parameter is NULL.
**/
STATIC
EFI_STATUS
EFIAPI
UfsHostDxeMap (
  IN     EDKII_UFS_HOST_CONTROLLER_PROTOCOL   *This,
  IN     EDKII_UFS_HOST_CONTROLLER_OPERATION  Operation,
  IN     VOID                                 *HostAddress,
  IN OUT UINTN                                *NumberOfBytes,
  OUT EFI_PHYSICAL_ADDRESS                    *DeviceAddress,
  OUT VOID                                    **Mapping
  )
{
  MAP_INFO  *MapInfo;

  if ((This == NULL) || (HostAddress == NULL) ||
      (NumberOfBytes == NULL) || (DeviceAddress == NULL) || (Mapping == NULL))
  {
    return EFI_INVALID_PARAMETER;
  }

  //
  // The EDK2 UfsPassThruDxe only requires that Map() return a non-NULL
  // token and a device address; it never relies on a bounce buffer on
  // architectures with coherent DMA. Keep a small allocation as the token
  // so Unmap() can be symmetric, and invalidate the range so that any
  // prior dirty cache lines cannot corrupt controller reads.
  //
  MapInfo = AllocatePool (sizeof (MAP_INFO));
  if (MapInfo == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  MapInfo->Signature         = MAP_INFO_SIGNATURE;
  MapInfo->Operation         = Operation;
  MapInfo->NumberOfBytes     = *NumberOfBytes;
  MapInfo->HostAddress       = HostAddress;
  MapInfo->DeviceAddress     = (EFI_PHYSICAL_ADDRESS) (UINTN) HostAddress;

  if (Operation == EdkiiUfsHcOperationBusMasterRead) {
    WriteBackInvalidateDataCacheRange (HostAddress, *NumberOfBytes);
  }

  *DeviceAddress = MapInfo->DeviceAddress;
  *Mapping       = (VOID *) MapInfo;
  return EFI_SUCCESS;
}

/**
  Undo a Map() operation.

  @retval EFI_SUCCESS            Operation succeeds.
  @retval EFI_INVALID_PARAMETER  Mapping is NULL.
**/
STATIC
EFI_STATUS
EFIAPI
UfsHostDxeUnmap (
  IN  EDKII_UFS_HOST_CONTROLLER_PROTOCOL  *This,
  IN  VOID                                *Mapping
  )
{
  MAP_INFO  *MapInfo;

  if (Mapping == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  MapInfo = (MAP_INFO *) Mapping;
  if (MapInfo->Signature != MAP_INFO_SIGNATURE) {
    return EFI_INVALID_PARAMETER;
  }

  if (MapInfo->Operation == EdkiiUfsHcOperationBusMasterWrite) {
    WriteBackInvalidateDataCacheRange (MapInfo->HostAddress, MapInfo->NumberOfBytes);
  }

  FreePool (MapInfo);
  return EFI_SUCCESS;
}

/**
  Allocate pages suitable for a common-buffer mapping.

  @retval EFI_SUCCESS            Operation succeeds.
  @retval EFI_INVALID_PARAMETER  A parameter is NULL.
  @retval EFI_OUT_OF_RESOURCES   Pages could not be allocated.
**/
STATIC
EFI_STATUS
EFIAPI
UfsHostDxeAllocateBuffer (
  IN     EDKII_UFS_HOST_CONTROLLER_PROTOCOL  *This,
  IN     EFI_ALLOCATE_TYPE                   Type,
  IN     EFI_MEMORY_TYPE                     MemoryType,
  IN     UINTN                               Pages,
  OUT VOID                                 **HostAddress,
  IN     UINT64                              Attributes
  )
{
  VOID  *Buffer;

  if ((HostAddress == NULL) || (Pages == 0)) {
    return EFI_INVALID_PARAMETER;
  }

  //
  // Allocate coherent memory for descriptor rings. The EDK2 UfsPassThruDxe
  // programs 64-bit address registers, but keeping the allocation inside
  // the first 4 GiB matches what this hardware block was validated for
  // in the vendor stack.
  //
  Buffer = AllocateAlignedPages (MemoryType, Pages);
  if (Buffer == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  ZeroMem (Buffer, EFI_PAGES_TO_SIZE (Pages));
  *HostAddress = Buffer;
  return EFI_SUCCESS;
}

/**
  Free pages allocated by AllocateBuffer().

  @retval EFI_SUCCESS            Operation succeeds.
  @retval EFI_INVALID_PARAMETER  HostAddress is NULL.
**/
STATIC
EFI_STATUS
EFIAPI
UfsHostDxeFreeBuffer (
  IN  EDKII_UFS_HOST_CONTROLLER_PROTOCOL  *This,
  IN  UINTN                               Pages,
  IN  VOID                                *HostAddress
  )
{
  if (HostAddress == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  FreeAlignedPages (HostAddress, Pages);
  return EFI_SUCCESS;
}

/**
  Flush controller-visible data. This SoC has no boot-time IOMMU for UFS;
  the only requirement is to push dirty cache lines to DRAM.

  @retval EFI_SUCCESS            Operation succeeds.
**/
STATIC
EFI_STATUS
EFIAPI
UfsHostDxeFlush (
  IN  EDKII_UFS_HOST_CONTROLLER_PROTOCOL  *This
  )
{
  //
  // Push any posted writes on the system NOC; a DMB suffices for the
  // MMIO window on ARMv8-A.
  //
  MemoryFence ();
  return EFI_SUCCESS;
}

/**
  Read UFS host controller MMIO registers.

  @retval EFI_SUCCESS            Operation succeeds.
  @retval EFI_UNSUPPORTED        Unsupported width.
  @retval EFI_INVALID_PARAMETER  A parameter is NULL.
**/
STATIC
EFI_STATUS
EFIAPI
UfsHostDxeRead (
  IN     EDKII_UFS_HOST_CONTROLLER_PROTOCOL        *This,
  IN     EDKII_UFS_HOST_CONTROLLER_PROTOCOL_WIDTH  Width,
  IN     UINT64                                    Offset,
  IN     UINTN                                     Count,
  IN OUT VOID                                      *Buffer
  )
{
  UINTN   Address;
  UINTN   Index;
  UINT32  *Buffer32;

  if (Buffer == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  if (Width != EfiUfsHcWidthUint32) {
    return EFI_UNSUPPORTED;
  }

  Address  = UFS_HC_BASE + (UINTN)Offset;
  Buffer32 = (UINT32 *)Buffer;
  for (Index = 0; Index < Count; Index++) {
    Buffer32[Index] = MmioRead32 (Address + Index * 4);
  }

  return EFI_SUCCESS;
}

/**
  Write UFS host controller MMIO registers.

  @retval EFI_SUCCESS            Operation succeeds.
  @retval EFI_UNSUPPORTED        Unsupported width.
  @retval EFI_INVALID_PARAMETER  A parameter is NULL.
**/
STATIC
EFI_STATUS
EFIAPI
UfsHostDxeWrite (
  IN     EDKII_UFS_HOST_CONTROLLER_PROTOCOL        *This,
  IN     EDKII_UFS_HOST_CONTROLLER_PROTOCOL_WIDTH  Width,
  IN     UINT64                                    Offset,
  IN     UINTN                                     Count,
  IN OUT VOID                                      *Buffer
  )
{
  UINTN   Address;
  UINTN   Index;
  UINT32  *Buffer32;

  if (Buffer == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  if (Width != EfiUfsHcWidthUint32) {
    return EFI_UNSUPPORTED;
  }

  Address  = UFS_HC_BASE + (UINTN)Offset;
  Buffer32 = (UINT32 *)Buffer;
  for (Index = 0; Index < Count; Index++) {
    MmioWrite32 (Address + Index * 4, Buffer32[Index]);
  }

  return EFI_SUCCESS;
}

//
// Field order MUST match struct _EDKII_UFS_HOST_CONTROLLER_PROTOCOL:
//   GetUfsHcMmioBar, AllocateBuffer, FreeBuffer, Map, Unmap, Flush, Read, Write
//
STATIC EDKII_UFS_HOST_CONTROLLER_PROTOCOL  mUfsHcProtocol = {
  UfsHostDxeGetMmioBar,
  UfsHostDxeAllocateBuffer,
  UfsHostDxeFreeBuffer,
  UfsHostDxeMap,
  UfsHostDxeUnmap,
  UfsHostDxeFlush,
  UfsHostDxeRead,
  UfsHostDxeWrite
};

//
// ---------------------------------------------------------------------------
// Cold-path subsystem bring-up
// ---------------------------------------------------------------------------

/**
  Bring the UFS power domain / clocks / PHY up if they are not already.

  @retval EFI_SUCCESS            Subsystem is up.
  @retval others                 A required step failed.
**/
STATIC
EFI_STATUS
UfsSubsystemInit (
  VOID
  )
{
  UINT32      Gdscr;
  UINT32      PcsReady;
  UINT32      Elapsed;
  BOOLEAN     ColdPath;
  EFI_STATUS  Status;

  //
  // 1. GDSC power domain.
  //
  UfsTrace (32);                       // 入口：UfsSubsystemInit 第一句
  Gdscr = MmioRead32 (UFS_GCC_BASE + GCC_UFS_GDSCR);
  UfsTrace (33);                       // GDSCR 已读出（挂在这里说明 GCC 读都读不了）
  if ((Gdscr & GDSC_POWER_ON_STATUS) == 0) {
    UfsTrace (34);                     // 判为冷路径：GDSC 显示掉电
    DEBUG ((DEBUG_INFO, "%a: UFS GDSC off -- powering on (cold path)\n", gEfiCallerBaseName));

    //
    // Cold path: the UFS supply rails are down too (the RPM keeps them in
    // LPM until a client votes). Bring them up BEFORE the GDSC so the PHY
    // sees stable power, exactly like the kernel regulator_bulk_enable()
    // ordering in ufs_qos_setup_clocks/regulator code.
    //
    Status = Msm8996PmicEnableRail (PmicRailUfsVddaPhy, 925000);   // L28
    if (EFI_ERROR (Status)) {
      DEBUG ((DEBUG_ERROR, "%a: ufs vdda-phy rail failed: %r\n", gEfiCallerBaseName, Status));
      return Status;
    }

    UfsTrace (36);                     // L28 vdda-phy 好了
    Status = Msm8996PmicEnableRail (PmicRailUfsVddaPll, 1800000);  // L12
    if (EFI_ERROR (Status)) {
      DEBUG ((DEBUG_ERROR, "%a: ufs vdda-pll rail failed: %r\n", gEfiCallerBaseName, Status));
      return Status;
    }

    UfsTrace (37);                     // L12 vdda-pll 好了
    Status = Msm8996PmicEnableRail (PmicRailUfsVccq, 1200000);     // L25
    if (EFI_ERROR (Status)) {
      DEBUG ((DEBUG_ERROR, "%a: ufs vccq rail failed: %r\n", gEfiCallerBaseName, Status));
      return Status;
    }

    UfsTrace (38);                     // L25 vccq 好了
    Status = Msm8996PmicEnableRail (PmicRailUfsVccq2, 1800000);    // S4
    if (EFI_ERROR (Status)) {
      DEBUG ((DEBUG_ERROR, "%a: ufs vccq2 rail failed: %r\n", gEfiCallerBaseName, Status));
      return Status;
    }

    UfsTrace (39);                     // S4 vccq2 好了
    ColdPath = TRUE;
    MmioWrite32 (UFS_GCC_BASE + GCC_UFS_GDSCR, Gdscr & ~GDSC_SW_COLLAPSE);

    //
    // Bounded wait.  A GDSC that never reports power-on means the GCC block
    // is not clocked (or the offset is wrong); spinning forever here would
    // trip the secure watchdog and drop the board into EDL, wiping out every
    // chance of diagnosing anything.  Fail the driver instead -- UEFI keeps
    // booting without UFS.
    //
    Elapsed = 0;
    while ((MmioRead32 (UFS_GCC_BASE + GCC_UFS_GDSCR) & GDSC_POWER_ON_STATUS) == 0) {
      if (Elapsed >= GDSC_POWER_ON_TIMEOUT_US) {
        DEBUG ((
          DEBUG_ERROR,
          "%a: UFS GDSC never powered on (GDSCR=0x%08x after %d us)\n",
          gEfiCallerBaseName,
          MmioRead32 (UFS_GCC_BASE + GCC_UFS_GDSCR),
          GDSC_POWER_ON_TIMEOUT_US
          ));
        return EFI_TIMEOUT;
      }

      gBS->Stall (PHY_INIT_POLL_STEP_US);
      Elapsed += PHY_INIT_POLL_STEP_US;
    }

    UfsTrace (40);                     // GDSC 通电位已确认
  } else {
    ColdPath = FALSE;
  }

  //
  // 2. PHY: on the SBL1 hand-off path PCS is already ready and we must
  //    not reprogramme it. On the cold path apply the rate-A table and
  //    start the SERDES.
  //
  UfsTrace (35);                       // 暖路径（或冷路径走完）→ 即将读 PHY
  PcsReady = MmioRead32 (UFS_PHY_BASE + UFS_PHY_PCS_READY_STATUS) & QMP_PCS_READY_MASK;
  UfsTrace (41);                       // PHY PCS 状态已读出（挂在这说明 PHY 窗口访问异常）
  if (PcsReady != 0) {
    DEBUG ((DEBUG_INFO, "%a: UFS PHY PCS already ready (warm path)\n", gEfiCallerBaseName));
    UfsTrace (42);
  } else {
    DEBUG ((DEBUG_INFO, "%a: UFS PHY not ready -- running calibration\n", gEfiCallerBaseName));
    UfsPhyApplyCalibration ();
    UfsTrace (43);                     // 校准表已写完
    Status = UfsPhyStartSerdes ();
    if (EFI_ERROR (Status)) {
      DEBUG ((DEBUG_ERROR, "%a: UFS PHY PCS never became ready\n", gEfiCallerBaseName));
      return EFI_DEVICE_ERROR;
    }

    UfsTrace (44);                     // SERDES 已起，PCS ready
  }

  //
  // 3. Clock branches. Enabling them first (before reconfiguring the
  //    root clock) keeps the warm path a strict no-op.
  //
  Status = GccEnableBranch (GCC_SYS_NOC_UFS_AXI_CBCR, "sys_noc_ufs_axi");
  if (EFI_ERROR (Status)) {
    return Status;
  }

  UfsTrace (45);
  Status = GccEnableBranch (GCC_UFS_AXI_CBCR, "ufs_axi");
  if (EFI_ERROR (Status)) {
    return Status;
  }

  UfsTrace (46);
  Status = GccEnableBranch (GCC_UFS_AHB_CBCR, "ufs_ahb");
  if (EFI_ERROR (Status)) {
    return Status;
  }

  UfsTrace (47);
  Status = GccEnableBranch (GCC_UFS_TX_CFG_CBCR, "ufs_tx_cfg");
  if (EFI_ERROR (Status)) {
    return Status;
  }

  UfsTrace (48);
  Status = GccEnableBranch (GCC_UFS_RX_CFG_CBCR, "ufs_rx_cfg");
  if (EFI_ERROR (Status)) {
    return Status;
  }

  UfsTrace (49);
  Status = GccEnableBranch (GCC_UFS_UNIPRO_CORE_CBCR, "ufs_unipro_core");
  if (EFI_ERROR (Status)) {
    return Status;
  }

  UfsTrace (50);
  if (ColdPath) {
    //
    // Root clock exists only on the cold path; on the warm path the
    // RCG2 was configured by SBL1 and must not be disturbed.
    //
    GccConfigUfsAxiRcg2 ();
    UfsTrace (51);
  }

  //
  // 4. PHY symbol clocks -- only exist once the link is up; the EDK2
  //    UfsPassThruDxe link-startup sequence gates them via the HCI
  //    registers, and on msm8996 the CBCRs tolerate being enabled early.
  //
  Status = GccEnableBranch (GCC_UFS_TX_SYMBOL_0_CBCR, "ufs_tx_symbol_0");
  if (EFI_ERROR (Status)) {
    return Status;
  }

  UfsTrace (52);
  Status = GccEnableBranch (GCC_UFS_RX_SYMBOL_0_CBCR, "ufs_rx_symbol_0");
  if (EFI_ERROR (Status)) {
    return Status;
  }

  UfsTrace (53);                       // 全部时钟使能完成
  return EFI_SUCCESS;
}

/**
  Entry point: initialise the UFS subsystem and publish the host
  controller protocol.

  @param[in] ImageHandle    Image handle.
  @param[in] SystemTable    System table.

  @retval EFI_SUCCESS       Protocol installed.
  @retval others            Subsystem init failed.
**/
EFI_STATUS
EFIAPI
UfsHostDxeEntryPoint (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_STATUS  Status;

  Status = UfsSubsystemInit ();
  if (EFI_ERROR (Status)) {
    //
    // Storage bring-up failure must not brick the firmware: the UniXNU
    // payload may still be able to boot from memory. Log loudly and fail
    // the protocol install so the DXE dispatcher moves on.
    //
    DEBUG ((DEBUG_ERROR, "%a: UFS subsystem init failed: %r\n", gEfiCallerBaseName, Status));
    return Status;
  }

  UfsTrace (54);                       // UfsSubsystemInit 成功返回
  Status = gBS->InstallMultipleProtocolInterfaces (
                  &ImageHandle,
                  &gEdkiiUfsHostControllerProtocolGuid,
                  &mUfsHcProtocol,
                  NULL
                  );
  ASSERT_EFI_ERROR (Status);

  UfsTrace (55);                       // 协议已装好
  DEBUG ((DEBUG_INFO, "%a: UFS host controller protocol installed\n", gEfiCallerBaseName));
  return EFI_SUCCESS;
}
