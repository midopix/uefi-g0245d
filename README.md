# uefi-g0245d

UEFI platform for Gree G0245D (格力手机·色界, Qualcomm MSM8996 / Snapdragon 820),
consumed by the [UniXNU-g0245d][device] device repository.

Mu-Silicium has no msm8996 silicon package, so this repository carries the board
package and the SoC support it needs as an overlay on a pinned Mu-Silicium
checkout, instead of forking a 3.9 GB tree.

| Field | Value |
| --- | --- |
| Mu-Silicium base | `6b3d90b7` ("Version 4.1", 2026-11-01) |
| Platform | `Platforms/Gree/g0245dPkg` |
| SoC support | `Silicon/Qualcomm/Msm8996Pkg` |
| Board config | `Resources/Configs/g0245d.toml` |
| Arch / target | `AARCH64` `RELEASE_CLANGPDB` |
| FD base / size | `0x92000000` / `0x300000` |

## Layout

| Path | Contents |
| --- | --- |
| `Platforms/Gree/g0245dPkg/g0245d.dsc` | Packages, libraries, components |
| `Platforms/Gree/g0245dPkg/g0245d.fdf` | Firmware volume layout |
| `Platforms/Gree/g0245dPkg/DeviceBuild.py` | EDK II platform build wrapper (pins `TOOL_CHAIN_TAG`) |
| `Platforms/Gree/g0245dPkg/Include/*.inc` | `APRIORI`, `DXE`, `RAW` module lists |
| `Platforms/Gree/g0245dPkg/Drivers/` | BootMark beacons, CrashLogDxe, DisplayDiagDxe, KeyProbeDxe |
| `Platforms/Gree/g0245dPkg/Library/` | Keypad, memory map, configuration map and diagnostic drawing libs |
| `Silicon/Qualcomm/Msm8996Pkg/` | SPMI, PMIC, display, UFS host, SEC, serial port support |
| `Resources/Configs/g0245d.toml` | Boot shim, FD geometry and boot image parameters |
| `patches/mu/` | Changes that must be applied to the `Mu_Basecore` submodule |

## Apply

```sh
git clone https://github.com/Project-Silicium/Mu-Silicium.git
git -C Mu-Silicium checkout 6b3d90b7
git -C Mu-Silicium submodule update --init --recursive
bash scripts/apply-overlay.sh Mu-Silicium
```

Then build, pack and flash from the device repository:

```sh
cd ../UniXNU-g0245d
MU=../Mu-Silicium bash scripts/linux/build-firmware.sh
UEFI=<Mu-Silicium> bash scripts/linux/pack-boot.sh
bash scripts/linux/flash.sh --wait 300
```

## Boot chain

The stock bootloader on this device is LK-based aboot, not UEFI:
PBL -> `xbl.elf` -> `emmc_appsboot.mbn` -> `boot.img`. BootShim therefore replaces
the kernel slot of the boot image: it keeps a valid ARM64 header so aboot's
`*(kernel+0x38) == 'ARMd'` check passes, relocates the firmware volume to
`0x92000000` and branches to the UEFI SEC entry with `x0` still holding the device
tree. The FD window sits above the resident aboot image (`0x916c4478`) and below
the HDMI splash region (`0xb1c00000`), clear of every XBL carveout.

## Vendor inputs

`Binaries/g0245d` holds Qualcomm DXE images (SPMI and others) that cannot be
redistributed. They are not in this repository: restore them from the device's own
QFIL package before building, or the build stops at `SPMI.inf is not found`. The
device repository lists them by hash in `proprietary-files.txt`.

## Mu_Basecore patch

`patches/mu/0001-trace-symbol-definitions.patch` restores three progress-beacon
symbol definitions (`mPrePiTraceHook`, `mArmMmuTraceHook`, `mArmMmuFailStage`) whose
declarations exist upstream but whose definitions are absent in the trimmed pack,
which otherwise fails the SEC link with an undefined symbol. Apply it to the
submodule once; `scripts/apply-overlay.sh` does that and is idempotent.

Build-time patches in `Resources/MuPatches` (Timer, Boot-Manager, Usb-Bus,
Auth-Service) come from upstream Mu-Silicium and are applied and reverted by
`build_uefi.py`; do not apply them by hand.

## State

The firmware builds green and boots on hardware to the UniXNU menu. The FD is
exactly `0x300000` bytes. Two known gaps, both recorded in the device repository's
`porting.toml`:

- the payload discovery chain (`UniXNURamDiskDxe`, `DiskIoDxe`, `Fat`) is not in the
  DSC/FDF yet, so the menu reports `drivers 0 loaded` and `no boot entry found`;
- the native MDP5/DSI path is not implemented; the menu renders through the GOP
  handoff into `cont_splash_mem@0x83401000`.

### Advanced Logger (v101)

`DebugLib` used to be `BaseDebugLibNull` for every module, so no `DEBUG()` output
existed anywhere; the only evidence channel was a photo of the panel.  v101 links
the Mu `AdvLoggerPkg` into the platform:

| Piece | Where |
| --- | --- |
| 1 MiB log buffer at `0x92300000` | `g0245d.dsc` (`PcdAdvancedLoggerBase`/`Pages`) + `MemoryMapLib` (`EfiReservedMemoryType`, so DXE never allocates it) |
| buffer owner / protocol | `AdvancedLoggerLib` DxeCore instance (constructor installs `gAdvancedLoggerProtocolGuid`) |
| `DEBUG()` sink | `BaseDebugLibAdvancedLogger` for `DXE_CORE`, `DXE_DRIVER`, `DXE_RUNTIME_DRIVER`, `UEFI_DRIVER`, `UEFI_APPLICATION` |
| reader | `UniXNUPkg/Driver/AdvancedLogDxe` — `Tools -> Advanced Logger`, pages the log out as QR frames |

SEC/PEI deliberately stay on `BaseDebugLibNull`: the buffer only becomes mapped
in DXE.  There is no UART on this board, so `AdvancedLoggerHdwPortLibNull` is used.
See `docs/advanced-logger.md` for the why and the traps.


## License

BSD-2-Clause-Patent, matching UniXNU and Mu-Silicium.

[device]: https://github.com/midopix/UniXNU-g0245d
