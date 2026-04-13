/*
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright 2026 Free Mobile - Vincent Jardin
 */

#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "soc_db.h"

/*
 * SVR signatures (mask 0xFFFF0000 - match by 16-bit family ID, ignoring
 * variant byte and silicon revision). The full reset value for LX2160A
 * is 0x87360120 per LX2160ARM.txt:37468; the other families use the
 * same encoding scheme with different family IDs.
 *
 * Family IDs taken from the Linux kernel guts driver and U-Boot's
 * arch/arm/include/asm/arch-fsl-layerscape/cpu.h:
 *   LS1028A = 0x870B  LS1088A = 0x8703  LS2088A = 0x8709
 *   LX2160A = 0x8736  LX2120A = 0x8737  LX2080A = 0x8738
 *
 * LX2080A and LX2120A are reduced personalities of the LX2160A die
 * and use the same .rcwi.
 */
static const soc_info_t SOCS[] = {
  { "fsl,ls1028a", "NXP Layerscape LS1028A", 0x870B0000u, 0xFFFF0000u, 0x01E00000ULL, 0x100u, 0x0A4u, 128, "ls1028a.rcwi" },
  { "fsl,ls1088a", "NXP Layerscape LS1088A", 0x87030000u, 0xFFFF0000u, 0x01E00000ULL, 0x100u, 0x0A4u, 128, "ls1088a.rcwi" },
  { "fsl,ls2088a", "NXP Layerscape LS2088A", 0x87090000u, 0xFFFF0000u, 0x01E00000ULL, 0x100u, 0x0A4u, 128, "ls2088a.rcwi" },
  { "fsl,lx2160a", "NXP Layerscape LX2160A", 0x87360000u, 0xFFFF0000u, 0x01E00000ULL, 0x100u, 0x0A4u, 128, "lx2160a.rcwi" },
  { "fsl,lx2120a", "NXP Layerscape LX2120A", 0x87370000u, 0xFFFF0000u, 0x01E00000ULL, 0x100u, 0x0A4u, 128, "lx2160a.rcwi" },
  { "fsl,lx2080a", "NXP Layerscape LX2080A", 0x87380000u, 0xFFFF0000u, 0x01E00000ULL, 0x100u, 0x0A4u, 128, "lx2160a.rcwi" },
};

#define SOCS_COUNT (sizeof(SOCS) / sizeof(SOCS[0]))

size_t
soc_db_count(void) {
  return SOCS_COUNT;
}

const soc_info_t *
soc_db_at(size_t i) {
  if (i >= SOCS_COUNT)
    return NULL;

  return &SOCS[i];
}

const soc_info_t *
soc_db_find_by_compat(const char *compat) {
  if (!compat)
    return NULL;

  for (size_t i = 0; i < SOCS_COUNT; i++) {
    if (strcmp(SOCS[i].compat, compat) == 0)
      return &SOCS[i];
  }

  return NULL;
}

const soc_info_t *
soc_db_find_by_svr(uint32_t svr) {
  for (size_t i = 0; i < SOCS_COUNT; i++) {
    if ((svr & SOCS[i].svr_mask) == SOCS[i].svr_match)
      return &SOCS[i];
  }

  return NULL;
}

/*
 * Read /proc/device-tree/compatible - a NUL-separated list of strings,
 * "most specific first". Walk each entry through the SoC table and
 * return the first match.
 *
 * The path can be overridden via $QORIQ_RCW_DT_COMPAT_FILE so unit
 * tests can point at a fixture without needing /proc.
 */
const soc_info_t *
soc_db_detect(char *detail, size_t detail_size) {
  const char *path = getenv("QORIQ_RCW_DT_COMPAT_FILE");
  if (!path)
    path = "/proc/device-tree/compatible";

  FILE *fp = fopen(path, "rb");
  if (!fp) {
    snprintf(detail, detail_size, "could not open %s: %s", path, strerror(errno));
    return NULL;
  }

  /*
   * The DT 'compatible' property is bounded - typical size is a few
   * hundred bytes. 4 KiB is plenty.
   */
  char buf[BUFSIZ];
  size_t n = fread(buf, 1, sizeof(buf) - 1, fp);
  fclose(fp);
  buf[n] = '\0';

  for (size_t off = 0; off < n; ) {
    const char *entry = buf + off;
    size_t entry_len = strnlen(entry, n - off);
    if (entry_len == 0) {
      off++;
      continue;
    }
    const soc_info_t *soc = soc_db_find_by_compat(entry);
    if (soc) {
      snprintf(detail, detail_size, "%s: %s", path, entry);
      return soc;
    }
    off += entry_len + 1;
  }

  snprintf(detail, detail_size, "%s: no entry matched a known QorIQ/Layerscape SoC", path);

  return NULL;
}
