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

## Persistent patches

`scripts/apply-overlay.sh` applies both patches under `patches/mu/` and is
idempotent. They have **different git scopes**, which matters on a fresh checkout:

| Patch | Applies to | What it fixes |
| --- | --- | --- |
| `0001-trace-symbol-definitions.patch` | the `Mu_Basecore` submodule | Restores `mPrePiTraceHook` / `mArmMmuTraceHook` / `mArmMmuFailStage`, whose declarations exist upstream but whose definitions are missing from the trimmed pack (otherwise SEC fails to link with undefined symbols). |
| `0002-bootshim-image-size.patch` | the Mu-Silicium root | ARM64 `image_size` must cover the shim plus the FD: `(_Payload - _Head) + FD_SIZE`, not `FD_SIZE` alone. A label difference is resolved by the assembler; an absolute `_Payload` would need a relocation `objcopy -O binary` cannot apply. |

`Resources/MuPatches` (Timer, Boot-Manager, Usb-Bus, Auth-Service) is a separate
mechanism: those come from upstream Mu-Silicium and are applied and reverted by
`build_uefi.py`; do not apply them by hand.

## State

The firmware builds green and boots on hardware to the UniXNU menu. The FD is
exactly `0x300000` bytes. The remaining known gap, recorded in the device
repository's `porting.toml`, is the native MDP5/DSI path: the menu renders
through the GOP handoff into `cont_splash_mem@0x83401000` instead of driving the
panel itself.

### Payload discovery chain (v103)

The earlier "no boot entry found / drivers 0 loaded" gap is closed at the
firmware level. The documented six-module chain (plus `PartitionDxe`) is now
in the DSC and FDF:

| Module | Role |
| --- | --- |
| `UniXNURamDiskDxe` | SEC entry-argument HOB -> FDT `/chosen/linux,initrd-*` -> read-only BlockIo |
| `DiskIoDxe` / `PartitionDxe` / `Fat` | SimpleFileSystem on that BlockIo |
| `UniXNU` / `UniXNUDxe` | menu + `Boot####` registration |
| `DarwinBoot` | UEFI_DRIVER producing `gUniXNULoaderProtocolGuid` |

`DarwinBootPkg` is added to `DeviceBuild.py`'s `PACKAGES_PATH`; it is a genuine
UEFI driver, not the XNU kernel component with the same name. The FV has ample
room (v102 occupied `0x9cbc0` of `0x300000`).

**Build-level verification is not runtime verification.** Whether the SEC HOB
actually carries the payload FDT on this board, whether the menu enumerates a
real boot entry, and whether DarwinBoot can hand off to the kernel all require a
hardware boot. Those are listed as on-device items in `porting.toml`.

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
