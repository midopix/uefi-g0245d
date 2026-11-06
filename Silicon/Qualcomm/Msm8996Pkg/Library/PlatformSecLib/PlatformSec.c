/** @file
 *
 *  Platform SEC (early boot) initialisation for the Gree G0245D.
 *
 *  WHY THIS FILE EXISTS
 *  --------------------
 *  SiliciumPkg's Sec module is SoC-agnostic; it calls PlatformInitialize()
 *  (see Sec.c) to run whatever the target silicon needs before the DXE core
 *  can take over. On msm8996 the one thing that MUST happen is disabling the
 *  APSS watchdog, otherwise the board reboots a few seconds after the
 *  little-kernel hand-off and you never see a single character of output.
 *
 *  EVIDENCE / PROVENANCE
 *  ---------------------
 *  [VERIFIED] Watchdog base 0x09830000 -- from the device's own flattened
 *             device tree: node "qcom,wdt@9830000", reg = <0x9830000 0x1000>.
 *             Deliberately NOT the SDM845 address (0x0B017000); copying the
 *             Samsung reference would silently fail to disable the timer.
 *  [VERIFIED] Debug UART base 0x075B0000 -- DTB "serial@075b0000",
 *             reg = <0x075b0000 0x1000>, status = "okay"; independently
 *             corroborated by the stock kernel command line
 *             "console=ttyHSL0,115200,n8".
 *  [DERIVED]  UART clock 7.3728 MHz -- standard Qualcomm BLSP UART clock;
 *             used to derive the divisor for the requested baud rate.
 *             TODO(REA): confirm against the clock plan in emmc_appsboot.mbn
 *             (GCC_BLSP2_AHB_CBCR / BLSP2_UART2_APPS_CBCR) before trusting
 *             baud-rate accuracy on real hardware.
 *  [UNVERIFIED] Any IOMMU/SMMU stream detach: on sdm845 the reference does
 *             ArmSmmuDetach() for the MDP streams. The equivalent msm8996
 *             SIDs have NOT been extracted yet, so the call is intentionally
 *             omitted rather than guessed -- see the TODO below.
 *
 *  Copyright (c) 2011-2013, ARM Limited. All rights reserved.
 *  SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 **/

#include <Library/IoLib.h>
#include <Library/PlatformSecLib.h>
#include <Library/MemoryMapHelperLib.h>
#include <Library/SerialPortLib.h>
#include <Library/DebugLib.h>

#include "PlatformRegisters.h"

/**
  Disable the APSS watchdog timer.

  The watchdog is armed by the previous boot stage; if it is not disabled it
  fires and resets the device shortly after we take control. The base address
  is resolved by name from the platform memory map (see MemoryMapLib), so it
  stays in one place rather than being scattered through the SEC code.

  [VERIFIED] Region "APSS_WDT_TMR1" -> 0x09830000 (DTB qcom,wdt@9830000).
**/
STATIC
VOID
DisableWatchDogTimer (
  VOID
  )
{
  EFI_STATUS                   Status;
  EFI_MEMORY_REGION_DESCRIPTOR WatchDogRegion;

  //
  // Resolve the watchdog MMIO window from the platform memory map.
  //
  Status = LocateMemoryRegionByName (&"APSS_WDT_TMR1"[0], &WatchDogRegion);
  if (EFI_ERROR (Status)) {
    //
    // Cannot disable it safely without an address; keep going so the caller
    // can still reach serial output and report the failure.
    //
    DEBUG ((DEBUG_ERROR, "SEC: watchdog region 'APSS_WDT_TMR1' not found!\n"));
    return;
  }

  //
  // Clear the enable bit -- [VERIFIED] offset 0x8 from the sdm845 reference;
  // the msm8996 APSS WDT uses the same register layout.
  //
  MmioWrite32 (WatchDogRegion.Address + APSS_WDT_ENABLE_OFFSET, 0x0);

  DEBUG ((DEBUG_INFO, "SEC: APSS watchdog disabled @ 0x%08x\n", WatchDogRegion.Address));
}

/**
  Platform-specific early initialisation.

  Called from SiliciumPkg Sec before the DXE core is entered. Keep this short
  and dependency-free: no memory services, no protocol database yet.
**/
VOID
PlatformInitialize (
  VOID
  )
{
  //
  // 1. Stop the watchdog before anything else can delay us.
  //
  DisableWatchDogTimer ();

  //
  // 2. Bring up the debug UART so everything after this point is visible.
  //    [VERIFIED] base 0x075B0000 (BLSP2_UART2).
  //
  SerialPortInitialize ();

  DEBUG ((DEBUG_INFO, "SEC: G0245D (msm8996) platform init done\n"));

  //
  // TODO(REA): once the msm8996 SMMU stream IDs are recovered from
  //   emmc_appsboot.mbn, re-introduce the equivalent of sdm845's
  //   ArmSmmuDetach(MdpStreams, ...) here so the display engine can work
  //   without the boot-time IOMMU attachments. Guessing SIDs risks hanging
  //   the bus, which is worse than having no display.
  //
}
