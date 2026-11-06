# g0245d APRIORI DXE list.
# NOTE: the upstream starqltechn/qrd8550 equivalents reference device-specific
# prebuilt DXE drivers in Binaries/<device>/QcomPkg/Drivers/. Since v79 a small
# subset (SmemDxe/DALSys/SPMI/PlatformInfo/DALTLMM/ButtonsDxe, for button
# input) IS included from Binaries/g0245d/ -- extracted from the G0245D's own
# QFIL xbl.elf (the vendor XBL IS UEFI-based; the old "LK aboot" note below was
# wrong). The remaining full bring-up set (ClockDxe, UFSDxe, PmicDxe, ...) can
# be extracted the same way when needed.
APRIORI DXE {
  INF MdeModulePkg/Core/Dxe/DxeMain.inf
  INF MdeModulePkg/Universal/PCD/Dxe/Pcd.inf

  INF MdeModulePkg/Universal/ReportStatusCodeRouter/RuntimeDxe/ReportStatusCodeRouterRuntimeDxe.inf
  INF MdeModulePkg/Universal/StatusCodeHandler/RuntimeDxe/StatusCodeHandlerRuntimeDxe.inf
  INF MdeModulePkg/Core/RuntimeDxe/RuntimeDxe.inf
  INF ArmPkg/Drivers/CpuDxe/CpuDxe.inf
  INF ArmPkg/Drivers/ArmGicDxe/ArmGicDxe.inf
  INF ArmPkg/Drivers/TimerDxe/TimerDxe.inf
  INF MdeModulePkg/Universal/WatchdogTimerDxe/WatchdogTimer.inf
  INF MdeModulePkg/Universal/SecurityStubDxe/SecurityStubDxe.inf
}

# NOTE (v53c regression): hoisting DisplayDxe into this APRIORI list DID fix the
# ConOut ordering problem -- the screen finally showed DEBUG() text -- but it
# re-broke the panel: with DisplayDxe running that early the framebuffer came up
# horizontally squeezed to 1/3 and repeated three times again (the exact symptom
# v52 fixed). DisplayDxe therefore stays at its normal place in g0245d.fdf and
# GraphicsConsoleDxe is moved AFTER it instead; see DXE.inc / g0245d.fdf.
