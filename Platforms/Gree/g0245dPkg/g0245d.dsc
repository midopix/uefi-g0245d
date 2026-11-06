## @file
#  g0245dPkg - Gree G0245D (msm8996 / Snapdragon 820) Mu-Silicium platform package.
#
#  Provenance of the constants below
#  ---------------------------------
#  Upstream Mu-Silicium / UniXNU has NO msm8996 (Snapdragon 820) silicon
#  package: Silicon/Qualcomm/ contains sdm845 (NapaliPkg) and newer SoCs only.
#  Earlier drafts therefore !included NapaliPkg (sdm845) as a stand-in.
#  This version !includes the Msm8996Pkg written for this port; its GIC and
#  UART constants are taken from the device's own artefacts (DTB + board.toml),
#  NOT from the SDM845 template. In particular:
#      GICD = 0x09BC0000, GICR = 0x09C00000   (SDM845 is 0x17A00000/0x17A60000)
#      UART = 0x075B0000 (BLSP2_UART2, status="ok")
#  Values that could not be confirmed from device artefacts are marked
#  TODO(REA) rather than invented -- see Msm8996Pkg/Include/Msm8996Regs.h.
#  A bootable FD still requires the unwritten NativeUfs8996 / Adreno530 /
#  Mdp5 / BlspI2c silicon components plus validation on real hardware.
#
#  The genuine UEFI payload is unixnu-src/package/UniXNUPkg
#  (Application/UniXNU + Driver/UniXNUDxe). ArmCpuPkg / ArmDTPlatformPkg /
#  Drivers/DarwinBoot referenced in earlier drafts are XNU *kernel* drivers
#  (built as .a static libs by the ./unixnu CLI), NOT UEFI packages, and are not
#  linked here.
#
#  Build entry:
#    tools/build-g0245d.sh            (RELEASE, via build_uefi.py -> DeviceBuild.py)
#  Output: Build/g0245dPkg/{TARGET}_CLANGPDB/FV/SILICIUM_UEFI.fd
#  Packed by UniXNU-g0245d/scripts/package-g0245d-boot.py.
#
#  Copyright (c) 2026, UniXNU contributors. All rights reserved.
#  SPDX-License-Identifier: BSD-2-Clause-Patent
##

[Defines]
  PLATFORM_NAME                  = g0245d
  PLATFORM_GUID                  = 6F2A4E1B-9C38-4B7A-8D12-3F5E9C7A1B20
  PLATFORM_VERSION               = 0.1
  DSC_SPECIFICATION              = 0x00010005
  OUTPUT_DIRECTORY               = Build/g0245dPkg
  SUPPORTED_ARCHITECTURES        = AARCH64
  BUILD_TARGETS                  = RELEASE|DEBUG
  SKUID_IDENTIFIER               = DEFAULT
  FLASH_DEFINITION               = g0245dPkg/g0245d.fdf
  USE_CUSTOM_DISPLAY_DRIVER      = 0

  # Msm8996Pkg is the MSM8996 (Snapdragon 820/821) silicon package written for
  # this port. Its GIC constants were verified against this device's own DTB and
  # deliberately differ from the SDM845/NapaliPkg values -- do not "harmonise"
  # them back to 0x17A00000/0x17A60000.
!include Msm8996Pkg/Msm8996Pkg.dsc.inc

[PcdsFixedAtBuild]
  #
  # DDR Memory
  #
  gArmTokenSpaceGuid.PcdSystemMemoryBase|0x80000000

  #
  # UEFI Stack
  #
  # MUST match the "UEFI Stack" entry in g0245dPkg/Library/MemoryMapLib: SEC
  # starts on this stack with the MMU off, so an address outside the declared
  # map works right up until MemoryPeim() installs the page tables -- after
  # which the very next push faults and the board resets into EDL.  The table
  # places the stack at 0x9F090000 (right after MMU PageTables), so use it.
  #
  gArmPlatformTokenSpaceGuid.PcdCPUCoresStackBase|0x9F090000
  gArmPlatformTokenSpaceGuid.PcdCPUCorePrimaryStackSize|0x40000

  #
  # SMBIOS
  #
  gSiliciumPkgTokenSpaceGuid.PcdSmbiosSystemManufacturer|"Gree"
  gSiliciumPkgTokenSpaceGuid.PcdSmbiosSystemModel|"G0245D"
  gSiliciumPkgTokenSpaceGuid.PcdSmbiosSystemRetailModel|"g0245d"
  gSiliciumPkgTokenSpaceGuid.PcdSmbiosSystemRetailSku|"G0245D"
  gSiliciumPkgTokenSpaceGuid.PcdSmbiosSystemBoardModel|"G0245D"

  #
  # Simple Frame Buffer (G0245D panel: 1080x1920, 32bpp)
  #
  gSiliciumPkgTokenSpaceGuid.PcdFrameBufferWidth|1080
  gSiliciumPkgTokenSpaceGuid.PcdFrameBufferHeight|1920
  # LK hands over a 24bpp scanout: photo forensics (v64 "3 compressed copies"
  # = gcd(4320,3240)=1080B period; v65 speckle noise = gcd(4352,3240)=8B
  # period) pin the LK scanout at stride 1080*3 = 3240 bytes, RGB888.
  gSiliciumPkgTokenSpaceGuid.PcdFrameBufferColorDepth|24
  gSiliciumPkgTokenSpaceGuid.PcdFrameBufferScanlinePixels|1080
  # Earlier beacon debugging measured: framebuffer row 0 lands on the BOTTOM
  # screen line (panel scans the buffer bottom-up) -> GOP must flip rows.
  gSiliciumPkgTokenSpaceGuid.PcdFrameBufferVerticalFlip|1
  # v66 photo: single clean copy, colors right, but horizontally mirrored ->
  # the scanout is a full 180-degree rotation (row 0 = bottom AND col 0 = right).
  gSiliciumPkgTokenSpaceGuid.PcdFrameBufferHorizontalFlip|1
  # v73 ruler-probe photo: the six 180-px colour blocks land at screen
  # columns {78,257,435,614,795,973} with the yellow band split across BOTH
  # screen edges -> the line fetch window starts at buffer column 796,
  # playing right-to-left.  ColStart = Width-1-ColShift -> ColShift = 283.
  # (v70's 267 was 16 px off; the 17-px strip around the true seam x~813 is
  # exactly where the menu-card rectangles have been corrupting.)
  gSiliciumPkgTokenSpaceGuid.PcdFrameBufferColumnOffset|283

  #
  # Platform PEI
  #
  gQcomPkgTokenSpaceGuid.PcdPlatformType|"LA"

  #
  # DEBUG property/error-level PCDs (v53, kept for a future serial DebugLib).
  # They are inert with BaseDebugLibNull.  PropertyMask 0x0A = PRINT enabled |
  # CLEAR_MEMORY enabled, ASSERT deliberately cleared: an ASSERT() in an
  # unvalidated bring-up port must not halt the boot.
  #
  gEfiMdePkgTokenSpaceGuid.PcdDebugPropertyMask|0x0A
  gEfiMdePkgTokenSpaceGuid.PcdDebugPrintErrorLevel|0x8000004F

  #
  # v95 INPUT NOTE: SiliciumPkg.dsc.inc:102 sets
  # gEfiMdeModulePkgTokenSpaceGuid.PcdConInConnectOnDemand|TRUE.  We do NOT
  # redefine that PCD here -- a second [PcdsFixedAtBuild] entry for the same
  # PCD pulled in through !include is a build hazard.  Instead BootManager.c
  # explicitly calls EfiBootManagerConnectConsoleVariable (ConIn) in
  # PlatformBootManagerBeforeConsole().  See PITFALLS.md D10: lazy ConIn makes
  # BdsDxe skip ConIn entirely and every key appears dead.
  #
[LibraryClasses]
  #
  # DEBUG routing (v78).  v53 routed DEBUG() to ConOut because the panel was
  # the only console -- but GraphicsConsoleDxe prints onto the SAME
  # framebuffer the menu draws into, and every newline scrolls the whole
  # screen up one glyph row (19 px) through the blitter.  That is exactly the
  # menu corruption seen since v68: title fragments clipped at the selected
  # card's top edge, stepped card edges whose height differences are
  # multiples of 19 px, and the v76 probe (which emits no DEBUG) rendering
  # pixel-perfect through the identical blitter.  UniXNU's Ui.c states this
  # assumption explicitly: "Graphics mode never prints through ConOut".
  # Silence DEBUG until a real UART exists; crash diagnosis still works via
  # CrashLogDxe's framebuffer hex renderer, which does not depend on DEBUG.
  #
  DebugLib|MdePkg/Library/BaseDebugLibNull/BaseDebugLibNull.inf

  #
  # UniXNU loader support library
  #
  UniXNULib|UniXNUPkg/Library/UniXNULib/UniXNULib.inf

  #
  # SCSI helper library for ScsiBusDxe/ScsiDiskDxe (UFS storage stack).
  #
  UefiScsiLib|MdePkg/Library/UefiScsiLib/UefiScsiLib.inf

  #
  # Memory Libraries (device-specific: MSM8996 layout)
  #
  MemoryMapLib|g0245dPkg/Library/MemoryMapLib/MemoryMapLib.inf

  #
  # Framebuffer hex renderer, shared by DisplayDiagDxe and CrashLogDxe so a
  # caught CPU fault survives a cold reset as pixels on the panel.
  #
  DiagDrawLib|g0245dPkg/Library/DiagDrawLib/DiagDrawLib.inf

  #
  # v92 physical keypad.  Self-written, pure-MMIO over Msm8996SpmiLib; NO
  # XBL/SMEM/DAL dependency.  KeypadDeviceDxe links this library; KeypadDxe
  # does not need it (it only consumes gKeypadDeviceProtocolGuid).
  #
  KeypadDeviceLib|g0245dPkg/Library/KeypadDeviceLib/KeypadDeviceLib.inf

  #
  # QCOM Libraries (device-specific)
  #
  ConfigurationMapLib|g0245dPkg/Library/ConfigurationMapLib/ConfigurationMapLib.inf

#
# UefiDebugLibConOut's constructor dereferences SystemTable->BootServices without
# a NULL check (DebugLibConstructor.c:67), and it is a DXE-only library instance.
# Anything that runs before DxeCore has a usable SystemTable must stay silent or
# the board resets into EDL before the first line is ever printed.
#
[LibraryClasses.common.SEC]
  DebugLib|MdePkg/Library/BaseDebugLibNull/BaseDebugLibNull.inf

[LibraryClasses.common.PEI_CORE]
  DebugLib|MdePkg/Library/BaseDebugLibNull/BaseDebugLibNull.inf

[LibraryClasses.common.PEIM]
  DebugLib|MdePkg/Library/BaseDebugLibNull/BaseDebugLibNull.inf

[LibraryClasses.common.DXE_CORE]
  DebugLib|MdePkg/Library/BaseDebugLibNull/BaseDebugLibNull.inf

#
# DEBUG is compiled out of RELEASE by default: the toolchain flags carry
# "-D MDEPKG_NDEBUG" (verified in the actual clang command line), which turns
#
#     DEBUG ((DEBUG_INFO, "..."));
#
# into "if (FALSE) { ... }" and then lets clang delete the format string. That is
# why the v53 build produced a firmware with zero debug text in it even though
# DebugLib had been switched to UefiDebugLibConOut.
#
# BuildOptions are appended AFTER the toolchain flags (proof: the -march flag
# from Msm8996Pkg.dsc.inc appears at the very end of the clang command line), so
# a trailing -U is honoured and the macro is undefined again.
#
[BuildOptions]
  *_*_AARCH64_CC_FLAGS = -UMDEPKG_NDEBUG

[Components]
  #
  # The genuine UniXNU UEFI payload. All SEC/DXE/platform modules are pulled in
  # by g0245d.fdf (which !includes the source-buildable generic includes).
  #
  UniXNUPkg/Application/UniXNU/UniXNU.inf
  UniXNUPkg/Driver/UniXNUDxe/UniXNUDxe.inf

  #
  # msm8996 silicon drivers written for this port (Silicon/Qualcomm/Msm8996Pkg).
  #
  #   SimpleFbDxe  -- THE UPSTREAM WAY (USE_CUSTOM_DISPLAY_DRIVER = 0).
  #                   It does NOT program MDSS at all: it maps the framebuffer
  #                   the bootloader already set up ("Display Reserved",
  #                   0x83401000, 1080x1920@32) and publishes it as GOP.
  #
  #                   Why the hand-written DisplayDxe is gone: LK/aboot leaves
  #                   MDP5 + DSI + panel in a perfectly working state (that is
  #                   what shows the fastboot screen). Every time this port
  #                   reprogrammed those blocks from scratch the panel came back
  #                   squeezed to 1/3 and tiled three times, or as bare black
  #                   bands -- because the DSI/MDP state the code reproduced was
  #                   never bit-identical to the vendor one. Upstream solves the
  #                   same problem the same way: touch nothing, reuse the
  #                   bootloader's framebuffer.
  #
  #   UfsHostDxe   -- GDSC + GCC clock + 14nm QMP PHY bring-up, publishes
  #                   EDKII UFS HOST CONTROLLER protocol. The controller
  #                   itself is "jedec,ufs-1.1" per the DTB, so the generic
  #                   MdeModulePkg UfsPassThruDxe drives the register space.
  #
  Silicon/Silicium/SiliciumPkg/Drivers/SimpleFbDxe/SimpleFbDxe.inf
  Silicon/Qualcomm/Msm8996Pkg/Drivers/UfsHostDxe/UfsHostDxe.inf

  #
  # Storage stack on top of UfsPassThru:
  #   UfsPassThruDxe -> ExtScsiPassThru
  #   ScsiBusDxe     -> EFI_SCSI_IO / child handles
  #   ScsiDiskDxe    -> BlockIo / DiskIo on the UFS LUNs
  #   DiskIoDxe / PartitionDxe / EnhancedFatDxe -> filesystems (esp partition)
  #
  MdeModulePkg/Bus/Ufs/UfsPassThruDxe/UfsPassThruDxe.inf
  MdeModulePkg/Bus/Scsi/ScsiBusDxe/ScsiBusDxe.inf
  MdeModulePkg/Bus/Scsi/ScsiDiskDxe/ScsiDiskDxe.inf
  MdeModulePkg/Universal/Disk/DiskIoDxe/DiskIoDxe.inf
  MdeModulePkg/Universal/Disk/PartitionDxe/PartitionDxe.inf
  FatPkg/EnhancedFatDxe/Fat.inf

  #
  # Crash forensics: persist a CPU dump to the tail of userdata on any fatal
  # exception (no serial port required). See G0245D_FORENSICS_REFERENCE.md.
  #
  g0245dPkg/Drivers/CrashLogDxe/CrashLogDxe.inf

  #
  # v92 keypad input.  KeypadDeviceDxe installs gKeypadDeviceProtocolGuid on a
  # new handle (depex TRUE); KeypadDxe installs a DriverBinding that opens it
  # and publishes SimpleTextIn / SimpleTextInEx; ConSplitterDxe's protocol
  # notify then folds that into gST->ConIn so UniXNU's menu can read keys.
  #
  # KeyProbeDxe is TEMPORARY diagnostic scaffolding (PITFALLS.md A5): it draws
  # a raw-register strip at the bottom of the panel so one boot photo confirms
  # the PON polarity.  Remove it from here AND from g0245d.fdf afterwards.
  #
  SiliciumPkg/Drivers/KeypadDeviceDxe/KeypadDeviceDxe.inf
  SiliciumPkg/Drivers/KeypadDxe/KeypadDxe.inf
  g0245dPkg/Drivers/KeyProbeDxe/KeyProbeDxe.inf

  # BootMarkDxe sources stay in the tree (see PITFALLS.md C/E) but are NOT part
  # of the clean baseline. Re-add to FDF + DSC only when a new bisect is needed.

  #
  # Deliberately NOT built any more:
  #
  #   Silicon/Qualcomm/Msm8996Pkg/Drivers/DisplayDxe/DisplayDxe.inf
  #
  # It exists only to program MDSS from scratch. With SimpleFbDxe the firmware
  # no longer owns the display controller, so there is nothing to program.
  #
  # DisplayDiagDxe v10 (v76) ANSWERED the calibration question: the GOP blit
  # path (physical GOP via LocateProtocol) renders the ruler, per-second tick
  # redraws AND the exact menu card geometry pixel-perfect.  The blitter is
  # therefore clean; the menu corruption must come from the menu's own GOP
  # acquisition -- it takes the ConSplitter proxy off ConsoleOutHandle, which
  # the probe never exercised.  v77 swaps the menu to the physical GOP and
  # the probe is removed again (its 240 s stall would delay every boot).
  #
  # g0245dPkg/Drivers/DisplayDiagDxe/DisplayDiagDxe.inf

[PcdsFeatureFlag]
  #
  # [VERIFIED 2026-10-27] The msm8996 UFS host controller is implemented:
  #   Msm8996Pkg/Drivers/UfsHostDxe publishes the EDKII UFS host controller
  #   protocol and MdeModulePkg UfsPassThruDxe drives the "jedec,ufs-1.1"
  #   register space (DTB ufshc@624000).
  # NOTE: this section MUST stay at the end of the file -- a [PcdsFeatureFlag]
  # header placed inside the [PcdsFixedAtBuild] block silently re-typed
  # PcdPlatformType and broke the build with "Mismatched PCD type".
  #
  gMsm8996PkgTokenSpaceGuid.PcdUfs8996Supported|TRUE
