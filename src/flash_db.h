/*
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright 2026 Free Mobile - Vincent Jardin
 *
 * Database of boot-source / flash-device mappings for --dump-flash.
 *
 * The boot interface (FlexSPI NOR/NAND, SDHC, I2C EEPROM) is encoded
 * in PORSR1.RCW_SRC at DCFG offset 0x0 - it mirrors the CFG_RCW_SRC[3:0]
 * pin straps sampled at PORESET (LX2160ARM:38244). For each known
 * encoding we record the bootrom slot offsets and a sensible default
 * Linux device path; for FlexSPI we also scan /proc/mtd to find the
 * partition by name (rcw, bl2, nor0, qspi, ...).
 *
 * Per LX2160ARM:17688-9, the bootrom searches:
 *   primary  : offset 0      (or 0x1000 on SD/eMMC)
 *   fallback : offset 0x800000 (or 0x801000 on SD/eMMC)
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
  uint32_t rcw_src_value; /* PORSR1.RCW_SRC encoding (CFG_RCW_SRC pins) */
  const char *rcw_src_name; /* "FlexSPI Serial NOR 24-bit" etc. */
  const char *interface; /* short tag: "flexspi" | "sd" | "emmc" | "i2c" */
  const char *default_device;  /* "/dev/mtd0", "/dev/mmcblk0", ... or NULL */
  uint64_t primary_offset;
  uint64_t fallback_offset; /* 0 -> no fallback (I2C EEPROM) */
  bool device_required; /* true -> caller must pass --device */
} flash_info_t;

/* Lookup by raw RCW_SRC value (4-bit field per CLAUDE.md). NULL if unknown. */
const flash_info_t *flash_db_find_by_rcw_src(uint32_t rcw_src);

/* Iterate the table - used by tests. */
size_t flash_db_count(void);
const flash_info_t *flash_db_at(size_t i);

/*
 * Resolve the boot device for an info entry:
 *   - flexspi  : scan /proc/mtd for a partition whose name matches one of
 *                {"rcw", "bl2", "nor0", "qspi", "flash", "boot"}
 *                (case-insensitive substring, priority by list order).
 *                Falls back to info->default_device if /proc/mtd has no
 *                obvious candidate. The override path is read from the
 *                $QORIQ_RCW_PROC_MTD environment variable so unit tests
 *                can point at a fixture.
 *   - sd/emmc  : returns info->default_device unchanged.
 *   - i2c      : returns NULL (caller must pass --device explicitly).
 *
 * Returns a malloc()'d path the caller must free, or NULL with `err`
 * populated on failure.
 */
char *flash_db_resolve_device(const flash_info_t *info, char *err, size_t errlen);

/*
 * Extract the RCW_SRC field from a raw PORSR1 value. The field
 * encoding follows CLAUDE.md: a 4-bit value (0b1xxx). The hardware
 * field on LX2160 sits at bits[28:23] of PORSR1, but only the low
 * 4 bits carry the source code; we mask accordingly.
 */
uint32_t flash_db_porsr1_to_rcw_src(uint32_t porsr1);
