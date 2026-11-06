# Agent instructions

This repository owns the UEFI side of the g0245d port: the Mu-Silicium platform
package, the msm8996 SoC support it needs, the board config consumed by
`build_uefi.py`, and the changes that must be made to the `Mu_Basecore` submodule.
Board policy (device tree, partitions, payload, boot image parameters) belongs to
`UniXNU-g0245d`; shared SoC description belongs to `UniXNU-msm8996-common`.

Read before editing:

| Task | Required reading |
| --- | --- |
| Any change | [UEFI firmware](https://unixnu.com/porting/uefi-firmware), this README |
| Timer, keypad, BDS | `../UniXNU-g0245d/docs/timer-ppi-finding.md` |
| Build failures | `../UniXNU-g0245d/docs/pitfalls.md` |
| Log capture | [log capture](https://unixnu.com/porting/log-capture) |

## Constraints

- `TOOL_CHAIN_TAG` is pinned in `DeviceBuild.py`; do not "switch" the toolchain by
  editing `Conf/target.txt`, it has no effect and leaves a stale FD behind.
- `PcdArmArchTimer*IntrNum` are GIC INTIDs. The DTB PPI values 13/14/11/10 become
  29/30/27/26; anything in the 0-15 range registers on an SGI and breaks every
  `EVT_TIMER` consumer without printing an error.
- Never rely on `strings` over the built `.fd`: `FVMAIN` is LZMA-wrapped. Verify with
  `../UniXNU-g0245d/scripts/linux/verify-fd-unixnu.py`.
- `Resources/MuPatches/*.patch` are applied and reverted by `build_uefi.py`. Put a
  patch under `patches/mu/` only if it must persist in the submodule.
- Vendor binaries stay out of Git. `Binaries/` is ignored on purpose.
- The FD size must equal `[uefi_fd] size` in `../UniXNU-g0245d/board.toml`.
