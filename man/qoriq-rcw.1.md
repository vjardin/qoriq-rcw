---
title: QORIQ-RCW
section: 1
header: User Commands
footer: qoriq-rcw 0.1.0
date: 2026-04-12
---

<!-- SPDX-License-Identifier: BSD-3-Clause -->
<!-- Copyright 2026 Free Mobile -- Vincent Jardin -->

# NAME

qoriq-rcw - compile and decompile QorIQ Reset Configuration Word files

# SYNOPSIS

**qoriq-rcw** **-i** *input* **-o** *output* [**-I** *path*]...
[**-D** *field=value*]... [**\-\-no-pbl**] [**-w**]

**qoriq-rcw** **-r** **-i** *binary* **-o** *source* **\-\-rcwi** *rcwi*
[**-I** *path*]...

# DESCRIPTION

**qoriq-rcw** compiles RCW source files into PBL/RCW binary images for NXP
QorIQ and Layerscape SoCs that use PBI format 2 (LS1028, LS1088, LS2088,
LX2160 families). In reverse mode, it decompiles a binary image back to RCW
source text.

The input *.rcw* file is first run through the embedded C preprocessor
(mcpp, invoked in-process - no external toolchain required),
which enables the use of **#include**, **#define**, and **#ifdef** directives
in source files. The preprocessed output is then parsed and compiled into a
binary image containing the PBL preamble, the 1024-bit RCW register data, PBI
commands, and a CRC or Stop terminator.

# OPTIONS

**-i** *file*
:   Input filename. In forward mode, this is an *.rcw* source file. In reverse
    mode (**-r**), this is a *.bin* binary file. Required.

**-o** *file*
:   Output filename. In forward mode, a binary *.bin* file is produced. In
    reverse mode, RCW source text is produced. Required.

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
    *VALUE* is a numeric value (decimal or **0x** hexadecimal). Overrides take
    effect after source parsing, so they supersede values set in the *.rcw*
    file. May be specified multiple times.

**\-\-no-pbl**
:   Do not generate the PBL preamble (0xAA55AA55), Load RCW command, checksum,
    or terminator. Only the raw 128-byte RCW register data (and any PBI
    commands) are emitted.

**-w**
:   Enable warning messages. When set, duplicate bitfield assignments produce a
    warning on standard error.

**-h**, **\-\-help**
:   Display usage information and exit.

# EXIT STATUS

**0**
:   Success.

**1**
:   An error occurred (preprocessing failure, parse error, I/O error, etc.). A
    diagnostic message is printed to standard error.

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

All 32-bit words are little-endian. The CRC-32 uses polynomial 0x04C11DB7 with
initial value 0xFFFFFFFF.

# RCW SOURCE SYNTAX

*NAME*[*begin*:*end*]
:   Bitfield definition. Declares a named field occupying bit positions *begin*
    through *end*. Bit ordering matters: **[6:2]** differs from **[2:6]**.

*NAME*[*pos*]
:   Single-bit field definition at bit position *pos*.

*NAME*=*value*
:   Assign a numeric value to a previously defined bitfield.

**%***var*=*value*
:   Set a control variable. Recognized variables: **%size**, **%pbiformat**,
    **%classicbitnumbers**, **%littleendian**, **%nocrc**, **%loadwochecksum**.

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
write addr,value
write.b1 addr,value
awrite addr,value
awrite.b4 addr,v1,v2
awrite.b5 addr,v1,v2,v3,v4
wait N
poll addr,mask,value
poll.long addr,mask,value
blockcopy type,src,dst,size
loadacwindow value
```

Parameters accept decimal, hexadecimal (**0x**), and arithmetic expressions
(parentheses, **+**, **-**, **\***, **&**, **|**, **<<**, **>>**).

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

Override the system PLL ratio:

```
$ qoriq-rcw -i config/rcw_2000.rcw -o rcw.bin -D SYS_PLL_RAT=10
```

Decompile a binary back to source:

```
$ qoriq-rcw -r -i rcw.bin --rcwi ls1088rdb.rcwi -o rcw_decoded.rcw
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

# SEE ALSO

**libqoriq-rcw**(3), **mcpp**(1), **xxd**(1)

# AUTHORS

Vincent Jardin, Free Mobile.
