---
title: QORIQ-RCW
section: 1
header: User Commands
footer: qoriq-rcw 1.1.0
date: 2026-10-25
---

<!-- SPDX-License-Identifier: BSD-3-Clause -->
<!-- Copyright 2026 Free Mobile -- Vincent Jardin -->

# NAME

qoriq-rcw - compile, decompile and check QorIQ Reset Configuration Word files

# SYNOPSIS

**qoriq-rcw** **-i** *input* **-o** *output* [**-I** *path*]...
[**-D** *field=value*]... [**\-\-define** *name*[=*value*]]...
[**\-\-no-pbl**] [**-w**]

**qoriq-rcw** **-r** **-i** *binary* **-o** *source* **\-\-rcwi** *rcwi*
[**-I** *path*]... [**\-\-define** *name*[=*value*]]...

**qoriq-rcw** **\-\-check** **-i** *image* [**-v**]

**qoriq-rcw** **\-\-dump** **-o** *source* [**\-\-rcwi** *rcwi*]
[**\-\-mem** *path*] [**\-\-force**]

**qoriq-rcw** **\-\-dump-flash** **-o** *source* [**\-\-slot** *which*]
[**\-\-device** *path*] [**\-\-rcwi** *rcwi*] [**\-\-mem** *path*]
[**\-\-force**]

# DESCRIPTION

**qoriq-rcw** compiles RCW source files into PBL/RCW binary images for NXP
QorIQ and Layerscape SoCs that use PBI format 2 (LS1028, LS1088, LS2088,
LX2160 families). In reverse mode, it decompiles a binary image back to RCW
source text. With **\-\-check**, it verifies an image the way the Service
Processor's BootROM should do before running it, so that a bad image is caught before it
is flashed.

The input *.rcw* file is first run through the embedded C preprocessor
(mcpp, invoked in-process - no external toolchain required),
which enables the use of **#include**, **#define**, and **#ifdef** directives
in source files. The preprocessed output is then parsed and compiled into a
binary image containing the PBL preamble, the 1024-bit RCW register data, PBI
commands, and a CRC or Stop terminator.

# OPTIONS

**-i** *file*
:   Input filename. In forward mode, this is an *.rcw* source file. In reverse
    mode (**-r**) and with **\-\-check**, this is a binary image. Required.

**-o** *file*
:   Output filename. In forward mode, a binary *.bin* file is produced. In
    reverse mode, RCW source text is produced. Required, except with
    **\-\-check**.

**-r**
:   Reverse mode: decompile a binary image into RCW source text. Requires
    **\-\-rcwi**.

**\-\-rcwi** *file*
:   Path to the *.rcwi* bitfield definition file. Required when **-r** is
    specified. The *.rcwi* file provides the symbol names and bit positions
    needed to decode the binary.

**-I** *path*
:   Add *path* to the include search path for the C preprocessor. The current
    directory (**.**) is always included. May be specified multiple times.

**-D** *FIELD=VALUE*
:   Override a bitfield value, equivalent to adding an assignment line in the
    source file. *FIELD* is the bitfield name (e.g. **SYS_PLL_RAT**), and
    *VALUE* is read like Python's **int(VALUE, 0)**:
    decimal, or **0x** hexadecimal, **0o** octal, **0b** binary; a decimal
    with a leading zero is an error. Overrides take effect after source
    parsing, so they supersede values set in the *.rcw* file. May be
    specified multiple times.

**\-\-define** *NAME*[=*VALUE*]
:   Define the preprocessor macro *NAME* as *VALUE*, or as **1** without one,
    like the **-D** option of a C compiler. The macro is seen by the *.rcw*
    source and by the *.rcwi* file that **-r**, **\-\-dump** and
    **\-\-dump-flash** preprocess, so that the **#ifdef** variants of the
    sources (the LX2160A errata and SerDes protocol files, for instance) can
    be selected from the command line. May be specified multiple times.

**\-\-no-pbl**
:   Do not generate the PBL preamble (0xAA55AA55), Load RCW command, checksum,
    or terminator. Only the raw 128-byte RCW register data (and any PBI
    commands) are emitted.

**-w**
:   Enable warning messages. When set, duplicate bitfield assignments produce a
    warning on standard error.

**\-\-check**
:   Check a PBL image as the Service Processor does, and print on standard
    output **RCW[PBI_LENGTH]** and **RCW[SB_EN]**, then the same sanity log
    that heads a **\-\-dump-flash** output: *preamble*, *load_cmd*,
    *checksum* (the Load RCW checksum), *terminator* (every word up to the
    terminator must be a command, and the command **RCW[PBI_LENGTH]**
    points at must be a Stop or a CRC and Stop, RM section 8.2.6),
    *pbi_crc* (when the terminator is a CRC and Stop) and *pbi_commands*.
    The walk follows the jumps as the Service Processor's BootROM should do.
    A Jump Conditional depends on a register read at boot, so the walk stops
    there: the terminator and the CRC are still checked, the commands past
    it are not walked, and a *jumpc* WARN says so.

**-v**, **\-\-verbose**
:   With **\-\-check**, list every PBI command executed, terminator included,
    as its byte offset in the image and its *.pbi* source line, in the
    decompiler's spelling, so that the listing pastes back into a source
    file.

**\-\-dump**
:   Read the live 128-byte RCW snapshot from the running SoC's RCWSR
    registers via */dev/mem* and emit a *.rcw* source file. Detects the SoC
    from */proc/device-tree/compatible* (or, as a fallback, by reading the
    SVR register at the well-known DCFG offset). The output begins with a
    C-comment block listing the detected SoC, the DCFG base address used,
    and the result of every sanity check. Requires root (for */dev/mem*).
    Mutually exclusive with **-r** and **-i**. PBI commands are not
    recoverable in this mode - only the 1024-bit RCW register state; the
    sanity log reports **RCW[PBI_LENGTH]**, the amount of PBI the bootrom
    ran, which **\-\-dump-flash** recovers.

**\-\-dump-flash**
:   Read the RCW + PBI commands directly from the boot medium. Auto-
    detects the boot device from PORSR1.RCW_SRC at DCFG offset 0x0
    (mirrors the *CFG_RCW_SRC[3:0]* pin straps sampled at PORESET):
    FlexSPI NOR/NAND -> */dev/mtd0* (or whichever MTD partition matches
    *rcw* / *bl2* / *nor0* / *qspi* / *flash* / *boot* in
    */proc/mtd*), SDHC1 -> */dev/mmcblk0*, SDHC2 -> */dev/mmcblk0boot0*,
    I2C EEPROM -> must pass **\-\-device**. Recovers PBI commands too,
    unlike **\-\-dump** which only sees post-execution RCW state.
    Each slot is checked as **\-\-check** does (preamble, Load RCW
    checksum, terminator where **RCW[PBI_LENGTH]** points, PBI CRC) and
    compared with the live RCW; the source is decompiled from the image
    alone, the flash padding after the terminator is left out.
    Requires root.

**\-\-slot** *which*
:   With **\-\-dump-flash**, selects the bootrom slot to read:
    *primary* (offset 0 or 0x1000), *fallback* (offset 0x800000 or
    0x801000 - the bootrom retry location) or *all* (both, written to
    separate files; see **-o**). Default: *primary*.

**\-\-device** *path*
:   With **\-\-dump-flash**, override the auto-detected boot device.
    Required when **PORSR1.RCW_SRC** indicates I2C EEPROM (no Linux
    convention for that). Otherwise honours the auto-detection result
    if absent.

**\-\-mem** *path*
:   Override */dev/mem* with an alternate path. Intended for testing
    against a fixture file containing a snapshot of the DCFG page (RCWSR1
    at offset 0x100, SVR at offset 0xA4, PORSR1 at offset 0x0, the rest
    unused).

**\-\-force**
:   With **\-\-dump** and **\-\-dump-flash**, emit the source file even if
    one or more sanity checks failed (e.g. */dev/mem* returned all-zero,
    the SVR didn't match the detected SoC, or the on-flash PBI CRC is
    wrong). The failures are still recorded in the output's header comment
    block.

**-h**, **\-\-help**
:   Display usage information and exit.

# EXIT STATUS

**0**
:   Success. With **\-\-check**, no check failed; a WARN does not fail the
    image.

**1**
:   An error occurred (preprocessing failure, parse error, I/O error, etc.),
    or the **\-\-check** verdict is FAIL. A diagnostic message is printed to
    standard error; the **\-\-check** report goes to standard output.

# BINARY FORMAT

The output binary has the following layout when PBL generation is enabled (the
default):

```
Offset   Size    Content
0x000    4       Preamble: 0xAA55AA55
0x004    4       Load RCW command (0x80100000 or 0x80110000)
0x008    128     RCW register data (1024 bits)
0x088    4       Checksum (sum of all preceding 32-bit words)
0x08C    var     PBI commands
var      4       Terminator command (Stop or CRC-and-Stop)
var+4    4       Terminator value (0 or CRC-32)
```

All 32-bit words are little-endian. **RCW[PBI_LENGTH]** counts the PBI in
32-bit words, terminator included: the Service Processor reads that many
words after the checksum and requires the last command among them to be
the terminator. The CRC-32 uses polynomial 0x04C11DB7 with initial value
0xFFFFFFFF.

# RCW SOURCE SYNTAX

*NAME*[*begin*:*end*]
:   Bitfield definition. Declares a named field occupying bit positions *begin*
    through *end*. Bit ordering matters: **[6:2]** differs from **[2:6]**.

*NAME*[*pos*]
:   Single-bit field definition at bit position *pos*.

*NAME*=*value*
:   Assign a numeric value to a previously defined bitfield. Numbers read
    like Python's **int(x, 0)**: decimal, **0x**, **0o** or **0b** prefixed,
    underscores allowed between digits; a decimal with a leading zero is an
    error.

**%***var*=*value*
:   Set a control variable. Recognized variables: **%size**, **%pbiformat**,
    **%classicbitnumbers**, **%littleendian**, **%nocrc**, **%loadwochecksum**.
    **%sysaddr** belongs to the legacy PBL header and is refused with PBI
    format 2, as **rcw.py** refuses it.

**.pbi** / **.end**
:   Delimit a Pre-Boot Initialization command block.

**.uboot** / **.end**
:   Delimit a u-boot embedded-image block. The body is an **xxd**(1) hex
    dump of *u-boot.bin* (8 16-bit words per line, 16 bytes per line) used
    for SPI/SD/NAND boot chains. Each line has the form
    *ADDRESS*: *WORD WORD WORD WORD WORD WORD WORD WORD*. The
    encoder packs the lines into PBI write blocks anchored at fixed
    on-chip addresses; output is always big-endian regardless of
    **%littleendian**. Not used by any upstream pbiformat=2 board.

## PBI Commands

The following commands are available inside **.pbi** / **.end** blocks:

```
write addr,value                CCSR Write, 4 bytes
write.b1 addr,value             CCSR Write, 1 byte
awrite addr,word                AltConfig Write, 4 bytes (the same as .b3)
awrite.bB addr,word[,word...]   AltConfig Write of 2^(B-1) bytes, B = 1..10,
                                one operand word per 4 bytes
wait count                      Wait, count up to 0xFFFF
poll addr,mask,value            Poll Short
poll.long addr,mask,value       Poll Long
blockcopy type,src,dst,size     Block Copy
loadacwindow value              Load Alt Config Window
ccsrcopy.bN src,dest,words      CCSR Write from Address; .b1, .b2 or .b4
                                is the width of the destination registers
ccsrset.bN addr,mask            CCSR Read-Modify-Write: reg |= mask
ccsrclear.bN addr,mask          CCSR Read-Modify-Write: reg &= ~mask
ccsrreplace.bN addr,mask,data   CCSR Read-Modify-Write:
                                reg = (reg & ~mask) | (data & mask);
                                .b1, .b2 or .b4 is the access width
loadsecurityheader w0,...,w19   Load Security Header: the 20 CSF header
                                words, or a quoted hex text file name
loadboot1csfptr addr            Load Boot 1 CSF Header Pointer
loadc addr,mask                 Load Condition: *addr & mask, kept for
                                the next jumpc
jump target                     Jump
jumpc target,condition          Jump Conditional: taken when the loaded
                                condition equals condition
label:                          a jump target, on a line of its own
```

The width suffix of **ccsrcopy**, **ccsrset**, **ccsrclear** and
**ccsrreplace** is required. A jump *target* is a label, or a byte offset
counted from the start of the jump itself (a multiple of 4, at least 8).
**loadcondition** and **jumpif** are accepted as aliases of **loadc** and
**jumpc**. **flush** is a command of the legacy PBI formats with no PBI
format 2 encoding and is refused; its equivalent is a poll with an all-zero
mask, **poll** *addr*,0,0.

Operands are expressions à la Python's **eval()**: numbers as above,
parentheses, **+**, **-**, **\***, **&**, **|**, **<<**, **>>**, and
whatever macros the preprocessor expanded.

The decompiler prints the same mnemonics and width suffixes back, with
labels for the jump targets, and lists in a comment the RCW bits that no
field of the *.rcwi* covers.

## .uboot block input

The body inside **.uboot** / **.end** is generated from a u-boot binary by:

```
xxd u-boot.bin | cut -d ' ' -f1-10 > u-boot.xxd
```

then the contents of *u-boot.xxd* are pasted between the markers.

# EXAMPLES

Compile a board configuration:

```
$ cd ls1088ardb
$ qoriq-rcw -i FCQQQQQQQQ_PPP_H_0x1d_0x0d/rcw_1600_qspi.rcw \
              -o rcw_1600_qspi.bin
```

Override the system PLL ratio (decimal or binary, as **rcw.py** reads it):

```
$ qoriq-rcw -i config/rcw_2000.rcw -o rcw.bin -D SYS_PLL_RAT=10
$ qoriq-rcw -i config/rcw_2000.rcw -o rcw.bin -D SYS_PLL_RAT=0b1010
```

Select a variant that the sources guard with **#ifdef**:

```
$ qoriq-rcw -i rcw_2000.rcw -o rcw.bin --define RCW_ERRATA_NO_TRIM
```

Check an image before flashing it; **-v** would list the 271 commands:

```
$ qoriq-rcw --check -i rcw_2000_700_2900_18_5_2.bin
rcw_2000_700_2900_18_5_2.bin: 3156 bytes
  RCW[PBI_LENGTH]=754 words, SB_EN=0
  [PASS] preamble       : 0xaa55aa55 at offset 0
  [PASS] load_cmd       : 0x80100000 (Load RCW with checksum)
  [PASS] checksum       : stored 0xe317b9b3 matches computed sum
  [PASS] terminator     : Stop at byte 3148, where RCW[PBI_LENGTH]=754 words points
  [PASS] pbi_crc        : none (plain Stop terminator)
  [PASS] pbi_commands   : 271 commands in 3016 bytes, terminator included
  verdict: OK - the Service Processor/BootROM should accept this image
```

Decompile a binary back to source:

```
$ qoriq-rcw -r -i rcw.bin --rcwi ls1088rdb.rcwi -o rcw_decoded.rcw
```

Capture the live RCW from a running QorIQ board (auto-detects SoC):

```
# qoriq-rcw --dump -o /tmp/live.rcw
# head -20 /tmp/live.rcw
/*
 * Reset Configuration Word - runtime dump
 *
 * SoC family    : NXP Layerscape LX2160A
 * SoC compatible: fsl,lx2160a
 * SVR           : 0x87360120 (expected 0x87360000 mask 0xffff0000)
 * DCFG base     : 0x01e00000
 * ...
 */
```

If the canonical *.rcwi* files are not installed (no
*/usr/share/qoriq-rcw/* directory), pass **\-\-rcwi** explicitly:

```
# qoriq-rcw --dump --rcwi ./lx2160a.rcwi -o /tmp/live.rcw
```

A source file embedding a u-boot image:

```
%size=1024
%pbiformat=2
%classicbitnumbers=1
%littleendian=1
%nocrc=1
PBI_LENGTH[287:276]
.uboot
00000000: 27051956 04020602 4bcc7a40 00010000 ...
00000010: 0009ab21 00000003 00000000 00000004 ...
...
.end
```

Capture the RCW + PBI commands from the boot flash (recovers PBI
that the runtime registers no longer hold):

```
# qoriq-rcw --dump-flash -o /tmp/flash.rcw
qoriq-rcw: auto-detected boot device: /dev/mtd0 (override with --device)
```

Capture both bootrom slots (primary + fallback):

```
# qoriq-rcw --dump-flash --slot all -o /tmp/dump.rcw
# ls /tmp/dump.*.rcw
/tmp/dump.fallback.rcw  /tmp/dump.primary.rcw
```

# SEE ALSO

**libqoriq-rcw**(3), **mcpp**(1), **xxd**(1)

# AUTHORS

Vincent Jardin, Free Mobile.
