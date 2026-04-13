# qoriq-rcw

[![CI](https://github.com/vjardin/qoriq-rcw/actions/workflows/ci.yml/badge.svg?branch=main)](https://github.com/vjardin/qoriq-rcw/actions/workflows/ci.yml)

Command-line tool for compiling and decompiling NXP QorIQ Reset Configuration Word (RCW) files.

## What is the RCW?

On NXP QorIQ and Layerscape SoCs, the **Reset Configuration Word** is a 1024-bit block that governs the very first stage of hardware initialization. At power-on reset, the SoC's on-chip **Service Processor** (a dedicated Cortex-A5 core running from its own 64 KB boot ROM) samples the `CFG_RCW_SRC` configuration pins, loads 128 bytes of RCW data from the selected boot interface (FlexSPI NOR, SD card, eMMC, or I2C) into the Device Configuration RCWSR registers, then locks the PLLs accordingly. If a Pre-Boot Initialization (PBI) section is present, the Service Processor executes those commands next -- writing CCSR registers and copying data into on-chip SRAM (OCRAM). Only after all of this completes are the application cores released from reset.

The RCW binary image produced by this tool is what gets flashed onto the board's boot medium. It encodes PLL ratios, SerDes lane assignments, boot source selection, pin muxing, and errata workarounds. RCW source files (`.rcw`) are human-readable descriptions of these settings; this tool compiles them into binary and can decompile binaries back to source for inspection.

`qoriq-rcw` is a C reimplementation of the `rcw.py` Python tool, producing byte-identical output. It is built on top of `libqoriq-rcw`.

Supported platforms: LS1028, LS1088, LS2088, and LX2160 families (PBI format 2).

## Dependencies

- C17 compiler (gcc or clang)
- Meson (>= 0.56) and Ninja
- `libqoriq-rcw` (built automatically as part of the same Meson project)
- gcc in `PATH` at runtime (used as the C preprocessor for `.rcw` files)

## Build

From the repository root (`rcw/`):

```bash
meson setup build
meson compile -C build
```

The `qoriq-rcw` binary is at `build/qoriq-rcw/qoriq-rcw`.

## Test

```bash
meson test -C build
```

The test suite includes a cross-validation script that compiles every `.rcw` configuration for all supported boards with both `qoriq-rcw` and the reference `rcw.py`, then compares the outputs byte-for-byte. This requires Python 3.

## Install

```bash
meson install -C build
```

## Usage

Compile an RCW source file to a binary:

```bash
cd ls1088ardb
qoriq-rcw -i FCQQQQQQQQ_PPP_H_0x1d_0x0d/rcw_1600_qspi.rcw -o rcw.bin
```

Override a bitfield value:

```bash
qoriq-rcw -i config/rcw_2000.rcw -o rcw.bin -D SYS_PLL_RAT=10
```

Add an include path:

```bash
qoriq-rcw -i rcw_2000.rcw -o rcw.bin -I ../lx2160asi
```

Produce raw RCW data without PBL preamble:

```bash
qoriq-rcw -i rcw_2000.rcw -o rcw_raw.bin --no-pbl
```

Decompile a binary back to source:

```bash
qoriq-rcw -r -i rcw.bin --rcwi lx2160a.rcwi -o rcw_decoded.rcw
```

Capture the live RCW from a running QorIQ/Layerscape board (auto-detects
SoC from `/proc/device-tree/compatible`, falls back to an SVR probe).
Requires root for `/dev/mem`:

```bash
sudo qoriq-rcw --dump -o /tmp/live.rcw
```

The output starts with a C-comment header listing the detected SoC, the
DCFG base address used, and the result of every sanity check (SVR
match, non-zero/non-stuck-high RCW, endianness, ...). PBI commands are
not recoverable in this mode - only the 1024-bit RCW register state.

If the `qoriq-rcw-data` package is not installed, pass the matching
`.rcwi` explicitly:

```bash
sudo qoriq-rcw --dump --rcwi ./lx2160a.rcwi -o /tmp/live.rcw
```

Recover the RCW and the PBI commands directly from the boot flash
(unlike `--dump`, which only sees the post-execution RCW state because
PBI commands run once at boot and aren't preserved). The boot device
is auto-detected from `PORSR1.RCW_SRC` and a `/proc/mtd` partition
name scan:

```bash
sudo qoriq-rcw --dump-flash -o /tmp/flash.rcw
```

Read both bootrom slots (primary at offset 0/0x1000, fallback at
+8 MiB) - useful for verifying A/B image consistency:

```bash
sudo qoriq-rcw --dump-flash --slot all -o /tmp/dump.rcw
# produces /tmp/dump.primary.rcw and /tmp/dump.fallback.rcw
```

See `qoriq-rcw(1)` for the full reference.

## License

BSD-3-Clause. Copyright 2026 Free Mobile -- Vincent Jardin.
