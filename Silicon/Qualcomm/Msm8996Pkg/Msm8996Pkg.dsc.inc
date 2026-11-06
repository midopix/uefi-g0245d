##
#  Copyright (c) 2011 - 2022, ARM Limited. All rights reserved.
#  Copyright (c) 2018, Bingxing Wang. All rights reserved.
#  Copyright (c) Microsoft Corporation.
#
#  SPDX-License-Identifier: BSD-2-Clause-Patent
#
#  MSM8996 (Snapdragon 820/821) SoC-level platform description.
#
#  IMPORTANT
#  ---------
#  The GIC addresses below were taken from the device's own device tree
#  (`interrupt-controller@09bc0000`, reg = <0x9bc0000 0x10000
#  0x9c00000 0x100000>) and deliberately differ from the SDM845
#  (NapaliPkg) values, which were 0x17A00000 / 0x17A60000. Copying the
#  845 constants here would produce a firmware that hangs at GIC
#  initialisation, so they must not be "harmonised" back.
##
[Defines]
  #
  # Timer
  #
  USE_PHYSICAL_TIMER                         = 1

  #
  # KEP
  #
  HAS_ACTLR_EL1_UNIMPLEMENTED_ERRATA         = 0
  HAS_AMCNTENSET0_EL0_UNIMPLEMENTED_ERRATA   = 0
  HAS_GIC_V3_WITHOUT_IRM_FLAG_SUPPORT_ERRATA = 0
  HAS_PSCI_MEMPROTECT_FAILING_ERRATA         = 0

!include QcomPkg/QcomPkg.dsc.inc

[BuildOptions]
  # Kryo implements ARMv8-A (not 8.2), with the Cryptographic Extensions.
  *_*_AARCH64_CC_FLAGS = -march=armv8-a+crypto

[PcdsFixedAtBuild]
  #
  # ACPI OEM identity -- keep the QCOM OEM ID used across the Silicium tree.
  #
  gEfiMdeModulePkgTokenSpaceGuid.PcdAcpiDefaultOemTableId|0x36393938534D5143   # "CQMS8996"
  gEfiMdeModulePkgTokenSpaceGuid.PcdAcpiDefaultOemRevision|0x996

  #
  # CPU topology (verified against the DTB: 4 Kryo cores in 2 clusters).
  # NOTE: in a .dsc / .dsc.inc, PCDs are set with the two-field form
  #   <TokenSpaceGuidCName>.<PcdCName>|<Value>
  # The four-field <Value>|<DatumType>|<Token> form belongs to the .dec that
  # OWNS the PCD. Writing four fields here makes the build tools treat the
  # trailing token as the PCD's maximum size, which then trips:
  #   "error F000: The maximum size of VOID* type PCD '<x>' is less than
  #    its actual size occupied."
  #
  gArmPlatformTokenSpaceGuid.PcdCoreCount|4
  gArmPlatformTokenSpaceGuid.PcdClusterCount|2

  #
  # GIC -- verified from the DTB, NOT copied from SDM845.
  #
  gArmTokenSpaceGuid.PcdGicDistributorBase|0x09BC0000
  gArmTokenSpaceGuid.PcdGicRedistributorsBase|0x09C00000

  #
  # Architected timers -- **GIC INTID, not device-tree PPI index**.
  #
  # The DTB timer node says (msm8996.dtsi / device DTB):
  #     interrupts = <1 13 0x8>, <1 14 0x8>, <1 11 0x8>, <1 10 0x8>;
  # i.e. *PPI* 13 / 14 / 11 / 10.  Edk2's PcdArmArchTimer*IntrNum hold the raw
  # GIC **INTID** that ArmPkg's TimerDxe passes to
  # HardwareInterrupt->RegisterInterruptSource(), and for PPIs
  #     INTID = PPI + 16
  # ArmPkg.dec ships exactly those values as defaults:
  #     Sec 29, Intr 30, Hyp 26, Virt 27.
  #
  # v90..v98 wrote the bare PPI numbers (13/14/11/10) here.  Those are in the
  # SGI range (0..15), so TimerDxe registered its handler on SGIs while the real
  # EL1 physical timer asserts INTID 30 -- the handler never ran.  Symptom:
  # the boot menu's 10 s countdown freezes and every timer-driven input path
  # (keypad debounce / repeat) looks dead, while the firmware otherwise boots.
  # Proof in the built module: AARCH64/ArmPkg/Drivers/TimerDxe/.../ArmTimerDxe.dll
  # contains `mov w20, #0xe; mov w21, #0xd; mov w1, #0xb; mov w22, #0xa`.
  #
  # v100: PPI + 16.  (Upstream Qualcomm .dsc.inc files use 17/18 -- also not
  # this device's mapping; do not copy them either.)
  #
  gArmTokenSpaceGuid.PcdArmArchTimerSecIntrNum|29
  gArmTokenSpaceGuid.PcdArmArchTimerIntrNum|30
  gArmTokenSpaceGuid.PcdArmArchTimerVirtIntrNum|27
  gArmTokenSpaceGuid.PcdArmArchTimerHypIntrNum|26

  #
  # Arch timer frequency (v97 / PITFALLS D15).
  # The stock Qualcomm LK/aboot does NOT program CNTFRQ_EL0 before handing
  # over to UEFI, so ArmReadCntFrq() may read 0.  ArmGenericTimerPhyCounterLib
  # then falls back to FixedPcdGet64 (PcdTimerFreqOverwrite), whose DEC
  # default is ALSO 0 (SiliciumPkg.dec:32).  With Freq == 0:
  #   GetTimeInNanoSecond() -> 0  (keypad 20 ms debounce never fires)
  #   TimerDxe TimerTicks   -> 0  (EVT_TIMER events never dispatch)
  #   NanoSecondDelay()     -> 0  (gBS->Stall() silently becomes a no-op)
  # 19200000 is NOT a guess: parsed from this device's own DTB, node
  # /soc/timer (compatible "arm,armv8-timer"), clock-frequency = <19200000>
  # (bytes 01 24 f8 00).  This mirrors the Samsung S5E* dsc.inc files, which
  # set the same PCD to 26000000 for their own XO.  Other Qualcomm platforms
  # that boot fine all set it; Msm8996Pkg was the only one that never did.
  #
  gSiliciumPkgTokenSpaceGuid.PcdTimerFreqOverwrite|19200000

  #
  # Debug serial -- BLSP2_UART2, `status = "ok"` in the DTB, and confirmed
  # by the stock cmdline `console=ttyHSL0,115200,n8`.
  #
  gEfiMdePkgTokenSpaceGuid.PcdUartDefaultBaudRate|115200
  gMsm8996PkgTokenSpaceGuid.PcdUartBase|0x075B0000

  #
  # Framebuffer -- the stock ABL leaves the panel framebuffer at
  # cont_splash_mem (0x83401000, 36 MB); panel is 1080x1920.
  #
  gMsm8996PkgTokenSpaceGuid.PcdFrameBufferWidth|1080
  gMsm8996PkgTokenSpaceGuid.PcdFrameBufferHeight|1920
  gMsm8996PkgTokenSpaceGuid.PcdFrameBufferPixelBpp|32

  #
  # SMBIOS identity
  # NOTE: VOID* PCDs are size-bounded by the default value declared in the DEC
  # that OWNS them. SiliciumPkg.dec declares "Not Specified" (13 chars), so a
  # longer value is rejected with:
  #   "error F000: The maximum size of VOID* type PCD '<x>' is less than
  #    its actual size occupied."
  # These are only SMBIOS display strings, so keep them <= 13 characters.
  #
  gSiliciumPkgTokenSpaceGuid.PcdSmbiosProcessorModel|"MSM8996"
  gSiliciumPkgTokenSpaceGuid.PcdSmbiosProcessorPartNumber|"MSM8996"

[LibraryClasses]
  PlatformSecLib|Msm8996Pkg/Library/PlatformSecLib/PlatformSecLib.inf
  SerialPortLib|Msm8996Pkg/Library/SerialPortLib/Msm8996SerialPortLib.inf

  #
  # SPMI / PMIC / display silicon libraries (written for this port; all
  # offsets verified against Linux mainline drivers and the device DTB).
  #
  Msm8996SpmiLib|Msm8996Pkg/Library/Msm8996SpmiLib/Msm8996SpmiLib.inf
  Msm8996PmicLib|Msm8996Pkg/Library/Msm8996PmicLib/Msm8996PmicLib.inf
  Msm8996DisplayLib|Msm8996Pkg/Library/Msm8996DisplayLib/Msm8996DisplayLib.inf
