# g0245d DXE module list.
# v91 clean baseline: NO vendor prebuilts are dispatched here. See
# g0245d.fdf and g0245d/PITFALLS.md D3/D4 for why (SmemDxe hangs).
  INF MdeModulePkg/Core/Dxe/DxeMain.inf
  INF MdeModulePkg/Universal/PCD/Dxe/Pcd.inf
  INF ArmPkg/Drivers/CpuDxe/CpuDxe.inf
  INF MdeModulePkg/Core/RuntimeDxe/RuntimeDxe.inf
  INF MdeModulePkg/Universal/SecurityStubDxe/SecurityStubDxe.inf

  INF MdeModulePkg/Universal/WatchdogTimerDxe/WatchdogTimer.inf
  INF MdeModulePkg/Universal/CapsuleRuntimeDxe/CapsuleRuntimeDxe.inf
  INF MdeModulePkg/Universal/ReportStatusCodeRouter/RuntimeDxe/ReportStatusCodeRouterRuntimeDxe.inf
  INF MdeModulePkg/Universal/StatusCodeHandler/RuntimeDxe/StatusCodeHandlerRuntimeDxe.inf
  INF MdeModulePkg/Universal/Variable/RuntimeDxe/VariableRuntimeDxe.inf
  INF EmbeddedPkg/EmbeddedMonotonicCounter/EmbeddedMonotonicCounter.inf
  INF EmbeddedPkg/SimpleTextInOutSerial/SimpleTextInOutSerial.inf
  INF MdeModulePkg/Universal/ResetSystemRuntimeDxe/ResetSystemRuntimeDxe.inf
  INF EmbeddedPkg/RealTimeClockRuntimeDxe/RealTimeClockRuntimeDxe.inf
  INF EmbeddedPkg/MetronomeDxe/MetronomeDxe.inf
  INF MdeModulePkg/Universal/PrintDxe/PrintDxe.inf
  INF MdeModulePkg/Universal/DevicePathDxe/DevicePathDxe.inf
  INF MdeModulePkg/Universal/Console/ConPlatformDxe/ConPlatformDxe.inf
  INF MdeModulePkg/Universal/Console/ConSplitterDxe/ConSplitterDxe.inf

  # Vendor XBL drivers are NOT dispatched here. v79..v90 proved that lifting
  # XBL DXEs wholesale into Mu-Silicium does not work: SmemDxe's entry point
  # hangs waiting for XBL/PBL/TZ state and blocks the single-threaded DXE
  # dispatcher (see g0245d/PITFALLS.md D3/D4). The only survivor, SPMI, is
  # dispatched from g0245d.fdf. A future button driver must be written
  # against SPMI/PMIC directly, not pulled out of XBL.
  #
  # GraphicsConsoleDxe is NOT dispatched here. It must come after DisplayDxe
  # (which produces the GOP), because its INF has no [Depex] and its entry point
  # LocateProtocol()s the GOP exactly once -- on failure it returns an error and
  # is never retried, leaving gST->ConOut NULL for the whole boot (v53a bug), or
  # worse, hoisting DisplayDxe earlier instead re-broke the panel (v53c bug).
  # The INF now lives in g0245d.fdf, right after DisplayDxe.
  INF MdeModulePkg/Universal/HiiDatabaseDxe/HiiDatabaseDxe.inf
  INF MdeModulePkg/Universal/BdsDxe/BdsDxe.inf

  INF ArmPkg/Drivers/ArmGicDxe/ArmGicDxe.inf
  INF ArmPkg/Drivers/TimerDxe/TimerDxe.inf
