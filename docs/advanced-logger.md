# Advanced Logger on g0245d (v101)

## Why

This board has **no usable serial port** and, at the time of writing, a keypad
that does not deliver scancodes.  Every diagnosis so far cost a reboot, a photo
of the panel and a guess.  `DebugLib` was `BaseDebugLibNull` for every module, so
each `DEBUG()` in the tree compiled to nothing at runtime — the firmware had no
log at all, not even in memory.

## What v101 does

```
DxeCore ── AdvancedLoggerLib(DxeCore)  ──> initialises LoggerInfo at 0x92300000
                                            installs gAdvancedLoggerProtocolGuid
   │
   ├─ every DXE driver's DEBUG() ── BaseDebugLibAdvancedLogger ──> append to buffer
   │
   └─ AdvancedLogDxe (depex = gAdvancedLoggerProtocolGuid)
          └─ Tools -> Advanced Logger -> QR frames on the panel
```

| Knob | Value | Note |
| --- | --- | --- |
| `PcdAdvancedLoggerBase` | `0x92300000` | first byte after the FD window (`0x92000000 + 0x00300000`) |
| `PcdAdvancedLoggerPages` | `0x100` | 1 MiB, wraps when full |
| `PcdAdvancedLoggerFixedInRAM` | `TRUE` | buffer is preallocated by the platform, not carved by the library |
| `PcdAdvancedLoggerLocator` | `TRUE` | publishes the `AdvLoggerLocator` variable so the log survives a reset |
| `AdvancedLoggerHdwPortLib` | `...HdwPortLibNull` | there is no UART to mirror into |
| `AssertLib` | `AdvLoggerPkg/Library/AssertLib` | **required**: `BaseDebugLibAdvancedLogger` needs it or DxeMain fails to resolve the class (build error 4000) |

`MemoryMapLib` gained a matching `EfiReservedMemoryType` entry.  It must be
`Reserv`, not `BsData`: the DxeCore constructor writes to the fixed address, and
`BsData` would let the allocator hand the same pages to the next allocation.

## Traps found while wiring it

1. **`[Packages]` is optional.** This DSC has no `[Packages]` section at all and
   still builds, because every INF declares its own.  Library instance paths such
   as `AdvLoggerPkg/Library/...` resolve through `DeviceBuild.py`'s
   `PackagesPath`, which already contains `Common/Mu`.
2. **Do not switch SEC/PEI.** The buffer address is only mapped once DXE's page
   tables are up; `BaseDebugLibAdvancedLogger` in PEI faults before the first line
   is printed.
3. **`-UMDEPKG_NDEBUG` was already in `[BuildOptions]`.** Without it the RELEASE
   flags delete every `DEBUG()` body and the logger would stay empty no matter how
   correct the wiring is.
4. **The FD stays exactly `0x300000`.** Adding the logger pulls in ~35 KB of new
   PE32 plus the debug strings, which still fits; a future growth spurt will have
   to grow `[uefi_fd] size` in `Resources/Configs/g0245d.toml` and `board.toml`
   together, plus the `UEFI FD` entry in `MemoryMapLib`.

## Why not the official `unixnu payload` route

`https://unixnu.com/zh/porting/log-capture` suggests building the payload volume
with `./unixnu payload` so `[payload] tools = true` adds the **Tools** entry.
Measured on this tree, that route stops twice before it produces anything:

```
$ ./unixnu payload --device ../UniXNU-g0245d --uefi ../uefi-g0245d
unixnu: kernel not found: .../kernel/kernel.macho; run "unixnu build kernel" or pass --kernel FILE

$ ... --kernel out/ipsw/21H461/kernelcache.decompressed --unique-chip-id 0123456789ABCDEF
unixnu: arm-io needs a non-zero base: an exported register lies below 16 MiB
```

The second one is the interesting one: `scripts/unixnu/devicetree.py:192`
(`arm_io_window`) rounds the lowest exported register down to a 16 MiB boundary and
rejects `base == 0`.  MSM8996 puts a large part of its peripheral space *below*
16 MiB (BLSP1 at `0x75xxxx`, UFS at `0x624000`, MDM/MDSS at `0x900000`), so the
guard trips on this SoC by construction.  Every SoC the upstream porting guide was
written against keeps its peripherals above `0x1000000`.

**And it is not needed anyway.**  The built-in configuration inside
`UniXNULib/Config.c` (`mBuiltinConfig`) already contains the Tools entry:

```
timeout = 10 / Return to firmware / Restart / Power off / Tools
```

so the menu shows **Tools** with no payload volume at all — the volume only
replaces that built-in menu.  What was missing is the *plugin* behind Tools, and
that is `AdvancedLogDxe`, which needs the Advanced Logger buffer and nothing else.
