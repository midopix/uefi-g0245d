/** @file
 *
 *  Configuration map for the Gree G0245D (Qualcomm MSM8996 / Snapdragon 820).
 *
 *  These entries are the device-specific knobs the common Silicium/QCOM code
 *  reads (core counts, IMEM cookie base, log sizing, security policy flags).
 *
 *  PROVENANCE
 *  ----------
 *  Verified against this device's own device tree:
 *    NumCpus / NumActiveCores = 4   (DTB cpus node: 4x "qcom,kryo")
 *    PwrBtnShutdownFlag        = 0  (MTP reference board, no power-button tree)
 *
 *  NOTE(2026-10-27): SharedIMEMBaseAddr = 0x146BF000 was previously claimed
 *    to come from a DTB "IMEM Cookie Base" reserved-memory node. A full
 *    parse of all 90 DTBs appended to this device's stock boot.img found NO
 *    such node (no cookie/imem node in any reserved-memory). The value is
 *    therefore downgraded to OEM-DEFAULT: it matches the generic UEFI
 *    convention (polaris and others use the same 0x146BF000) and is kept,
 *    but it must be cross-checked against serial logs on real hardware.
 *
 *  Recovered from emmc_appsboot.mbn
 *  --------------------------------
 *  DloadCookieAddr, DloadCookieValue, TzAppsRegnAddr and TzAppsRegnSize were
 *  re-extracted by Ghidra 12.1.4 driven through REA (see the notes on each
 *  entry below). All four differed from the SDM845 reference: two were wrong
 *  (DloadCookieAddr, TzAppsRegnAddr) and two happened to match.
 *
 *  Cross-verified constants (NumCpusFuseAddr, Sdc1/Sdc2GpioConfig{On,Off},
 *    SecurityFlag): these are Qualcomm UEFI-core constants, not SoC-specific
 *    values. Evidence: the original OEM uefiplat.cfg files shipped in
 *    Mu-Silicium/Binaries/«device»/RawFiles (73 devices, msm8998 through
 *    sm8550)
 *    give NumCpusFuseAddr=0x5C04C, Sdc{1,2}GpioConfigOn=0x1E92,
 *    Sdc{1,2}GpioConfigOff=0xA00 on every device that carries the key, and
 *    SecurityFlag=0xC4 on 71 of 73 (only caprip/cebu add EnableQseeLogsFlag
 *    for 0x1C4). This device is Samsung, like starqltechn, which uses 0xC4.
 *    A direct sweep of emmc_appsboot.mbn (u32 scan, MOVW/MOVT immediate
 *    encoding scan) found none of these constants -- which is expected:
 *    the LK aboot on this device reads fuses through SCM_SVC_FUSE
 *    (FUN_9161c2bc: owner=6, svc=4, cmd=0; SCM 2.0 command 0x02000604),
 *    never by direct MMIO, so UEFI-core MMIO constants simply do not occur
 *    in that binary.
 *
 *  Copyright (c) Microsoft Corporation.
 *
 *  SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 **/

#include <Library/ConfigurationMapLib.h>

STATIC
EFI_CONFIGURATION_ENTRY_DESCRIPTOR
gConfigurationDescriptor[] = {
  // Configuration Map
  //
  // [VERIFIED / MSM8996]
  {"NumCpus",                4},
  {"NumActiveCores",         4},
  // [OEM-DEFAULT] generic UEFI convention value (0x146BF000); NOT
  //   confirmed by this device's own DTBs (see NOTE(2026-10-27) above).
  //   Cross-check against serial logs on real hardware.
  {"SharedIMEMBaseAddr",     0x146BF000},
  {"PwrBtnShutdownFlag",     0x0},
  {"EnableShell",            0x1},
  {"MaxLogFileSize",         0x400000},
  {"USBHS1_Config",          0x0},
  {"UsbFnIoRevNum",          0x10001},
  {"EnableSDHCSwitch",       0x1},

  // [VERIFIED / MSM8996] Recovered from this device's own emmc_appsboot.mbn
  //   (QFIL package, 2018-11-16) by Ghidra 12.1.4 through REA.
  //
  //   DloadCookieAddr / DloadCookieValue -- FUN_9161c8e4, the routine that also
  //     emits "SCM call to check dload mode failed":
  //         mov  r0, #0x3000
  //         movt r0, #0x7b        -> 0x7B3000
  //         ...
  //         cmp  r3, #0x10         -> cookie value 0x10
  //     Note the previous SDM845-derived placeholder was 0x1FD3000; the
  //     on-silicon value is 0x7B3000.
  //
  //   TzAppsRegnAddr / TzAppsRegnSize -- FUN_9161dd08, the routine that emits
  //     "secure app region addr=0x%x size=0x%x":
  //         mov  r3, #0x0   ; movt r3, #0x8660  -> 0x86600000
  //         mov  r12, #0x2200000                  -> 0x2200000
  //     Note the previous SDM845-derived placeholder was 0x86D00000; the
  //     on-silicon value is 0x86600000.
  {"DloadCookieAddr",        0x7B3000},
  {"DloadCookieValue",       0x10},
  {"TzAppsRegnAddr",         0x86600000},
  {"TzAppsRegnSize",         0x2200000},

  // [CROSS-VERIFIED / Qualcomm UEFI-core constants] Values identical to the
  //   original OEM uefiplat.cfg of 73 devices (msm8998..sm8550), see the
  //   PROVENANCE block above. Not present in emmc_appsboot.mbn by design:
  //   LK aboot reads fuses via SCM_SVC_FUSE (FUN_9161c2bc), not MMIO.
  //
  //   SecurityFlag bit decode (from the OEM uefiplat.cfg comments):
  //     0x1   SecBootEnableFlag        0x10  VariableServicesFlag
  //     0x2   TreeTpmEnableFlag        0x20  WinsecappFlag
  //     0x4   CommonMbnLoadFlag        0x40  LoadSecAppFlag
  //     0x8   DxHdcp2LoadFlag          0x80  LoadKeymasterFlag
  //     0x100 EnableQseeLogsFlag
  //   0xC4 = CommonMbnLoad | LoadSecApp | LoadKeymaster (matches Samsung
  //   starqltechn; caprip/cebu use 0x1C4 which additionally enables Qsee logs).
  {"NumCpusFuseAddr",        0x5C04C},
  {"Sdc1GpioConfigOn",       0x1E92},
  {"Sdc2GpioConfigOn",       0x1E92},
  {"Sdc1GpioConfigOff",      0xA00},
  {"Sdc2GpioConfigOff",      0xA00},
  {"SecurityFlag",           0xC4},
  // [OEM-DEFAULT] No device-specific source exists for these two on a
  //   non-UEFI msm8996 device; zero matches every OEM uefiplat.cfg checked.
  {"ImageFVFlashed",         0x0},
  {"EnableLogFsSyncInRetail", 0x0}
};

/**
  Returns the platform configuration map.
**/
VOID
GetConfigurationMap (
  OUT EFI_CONFIGURATION_ENTRY_DESCRIPTOR **ConfigurationDescriptor,
  OUT UINT8                               *ConfigurationDescriptorCount
  )
{
  *ConfigurationDescriptor      = gConfigurationDescriptor;
  *ConfigurationDescriptorCount = ARRAY_SIZE (gConfigurationDescriptor);
}
