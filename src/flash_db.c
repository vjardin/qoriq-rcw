/*
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright 2026 Free Mobile - Vincent Jardin
 */

#define _POSIX_C_SOURCE 200809L

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "flash_db.h"

/*
 * Boot source table - values from CLAUDE.md's CFG_RCW_SRC encoding.
 *
 * Slot offsets per LX2160ARM:17688-9: primary at 0 (0x1000 on SD), fallback
 * at 0x800000 (0x801000 on SD). eMMC follows the same convention as SD; the
 * boot partition is selected by EXT_CSD[PARTITION_CONFIG] and exposed by
 * the kernel as /dev/mmcblk0boot0.
 */
static const flash_info_t FLASH_TABLE[] = {
  { 0x8, "SDHC1 (SD card)", "sd", "/dev/mmcblk0", 0x00001000ULL, 0x00801000ULL, false },
  { 0x9, "SDHC2 (eMMC boot partition)", "emmc", "/dev/mmcblk0boot0", 0x00001000ULL, 0x00801000ULL, false },
  { 0xA, "I2C1 EEPROM (extended addressing)", "i2c", NULL, 0x00000000ULL, 0x00000000ULL, true },
  { 0xC, "FlexSPI Serial NAND (2 KiB pages)", "flexspi", "/dev/mtd0", 0x00000000ULL, 0x00800000ULL, false },
  { 0xD, "FlexSPI Serial NAND (4 KiB pages)", "flexspi", "/dev/mtd0", 0x00000000ULL, 0x00800000ULL, false },
  { 0xF, "FlexSPI Serial NOR (24-bit addressing)", "flexspi", "/dev/mtd0", 0x00000000ULL, 0x00800000ULL, false },
};

#define FLASH_COUNT (sizeof(FLASH_TABLE) / sizeof(FLASH_TABLE[0]))

size_t
flash_db_count(void) {
  return FLASH_COUNT;
}

const flash_info_t *
flash_db_at(size_t i) {
  return (i < FLASH_COUNT) ? &FLASH_TABLE[i] : NULL;
}

const flash_info_t *
flash_db_find_by_rcw_src(uint32_t rcw_src) {
  for (size_t i = 0; i < FLASH_COUNT; i++)
    if (FLASH_TABLE[i].rcw_src_value == rcw_src)
      return &FLASH_TABLE[i];

  return NULL;
}

/*
 * On LX2160 PORSR1 places RCW_SRC at bits[28:23]. The low 4 bits of
 * that field carry the boot-source code (per the CFG_RCW_SRC pin
 * straps). We shift down by 23 and mask to 6 bits, then keep only the
 * low 4 bits - that is what matches the table values.
 *
 * Some tools encode RCW_SRC differently per SoC (LS1028 etc.); for
 * pbiformat=2 the mapping is consistent across the four supported
 * families (CLAUDE.md table).
 */
uint32_t
flash_db_porsr1_to_rcw_src(uint32_t porsr1) {
  return (porsr1 >> 23) & 0xFu;
}

/* Lowercase in place. */
static void
strlower(char *s) {
  for (; *s; s++) *s = (char)tolower((unsigned char)*s);
}

/*
 * MTD scanning. Parses lines of /proc/mtd (or its override) of the form:
 *
 *   dev:    size   erasesize  name
 *   mtd0: 02000000 00040000 "qspi"
 *   mtd1: 00100000 00040000 "rcw"
 *
 * Returns malloc'd "/dev/mtd<N>" or NULL.
 */
char *
flash_db_resolve_device(const flash_info_t *info,
                        char *err, size_t errlen) {
  if (!info) {
    snprintf(err, errlen, "internal: NULL flash_info");
    return NULL;
  }

  if (info->device_required) {
    snprintf(err, errlen, "boot source %s requires --device (no Linux default)", info->rcw_src_name);
    return NULL;
  }

  /* Non-FlexSPI: just hand back the static default. */
  if (strcmp(info->interface, "flexspi") != 0) {
    if (!info->default_device) {
      snprintf(err, errlen, "internal: NULL default_device for %s", info->rcw_src_name);
      return NULL;
    }
    return strdup(info->default_device);
  }

  /* FlexSPI: scan /proc/mtd for a partition with a recognisable name. */
  const char *path = getenv("QORIQ_RCW_PROC_MTD");
  if (!path) path = "/proc/mtd";

  FILE *fp = fopen(path, "r");
  if (!fp) {
    /* /proc/mtd missing -> fall back to default */
    if (!info->default_device) {
      snprintf(err, errlen, "could not open %s and no default device", path);
      return NULL;
    }
    return strdup(info->default_device);
  }

  /* First line is the header; skip. */
  char line[256];
  if (!fgets(line, sizeof(line), fp)) {
    fclose(fp);
    return strdup(info->default_device);
  }

  typedef struct { int idx; char name[64]; } mtd_entry_t;
  mtd_entry_t entries[32];
  size_t n = 0;

  while (n < 32 && fgets(line, sizeof(line), fp)) {
    int idx;
    char name[64] = {0};
    /* %63[^"] is locale-safe and matches everything up to the closing quote */
    if (sscanf(line, " mtd%d: %*x %*x \"%63[^\"]\"", &idx, name) == 2) {
      entries[n].idx = idx;
      snprintf(entries[n].name, sizeof(entries[n].name), "%s", name);
      strlower(entries[n].name);
      n++;
    }
  }
  fclose(fp);

  if (n == 0)
    return strdup(info->default_device);

  /* Priority order: most specific first. Substring match against
   * lowercased partition names. */
  static const char *patterns[] = {
    "rcw", "bl2", "nor0", "qspi", "flash", "boot", NULL
  };

  for (size_t p = 0; patterns[p]; p++) {
    for (size_t i = 0; i < n; i++) {
      if (strstr(entries[i].name, patterns[p])) {
        char buf[32];
        snprintf(buf, sizeof(buf), "/dev/mtd%d", entries[i].idx);
        return strdup(buf);
      }
    }
  }

  /* No name match -> first MTD entry. */
  char buf[32];
  snprintf(buf, sizeof(buf), "/dev/mtd%d", entries[0].idx);

  return strdup(buf);
}
