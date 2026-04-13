/*
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright 2026 Free Mobile - Vincent Jardin
 *
 * Database of supported NXP Layerscape SoCs for runtime RCW dump.
 *
 * Each entry maps a Device Tree "compatible" string and an SVR (System
 * Version Register) signature to:
 *   - the CCSR DCFG block base address,
 *   - the RCWSR1 offset (where the 1024-bit RCW snapshot lives),
 *   - the SVR offset (used both for matching and as a sanity check),
 *   - the canonical .rcwi filename for the family.
 *
 * SVR values: see LX2160ARM.txt section 9.3.1.12 (LX2160A reset value 0x87360120)
 * and the equivalent registers in the LS1028A/LS1088A/LS2088A reference
 * manuals. The 16-bit family ID lives in bits[31:16]; we mask away the
 * variant and revision bytes so a single entry matches every silicon rev.
 *
 * Layout (DCFG @ 0x01E0_0000, RCWSR1 @ +0x100, SVR @ +0xA4) is identical
 * across all four pbiformat=2 families.
 */

#pragma once

#include <stddef.h>
#include <stdint.h>

typedef struct {
  const char *compat;        /* DT compatible, e.g. "fsl,lx2160a" */
  const char *pretty_name;   /* "NXP Layerscape LX2160A" */
  uint32_t    svr_match;     /* expected SVR after masking */
  uint32_t    svr_mask;      /* mask applied before compare */
  uint64_t    dcfg_base;     /* CCSR DCFG block base */
  uint32_t    rcwsr1_offset; /* offset of RCWSR1 within DCFG */
  uint32_t    svr_offset;    /* offset of SVR within DCFG */
  size_t      rcw_bytes;     /* always 128 for pbiformat=2 */
  const char *default_rcwi;  /* canonical .rcwi filename */
} soc_info_t;

/* Lookup helpers - return NULL if no match. */
const soc_info_t *soc_db_find_by_compat(const char *compat);
const soc_info_t *soc_db_find_by_svr(uint32_t svr);

/* Iterate the table (count + indexed access) - used by tests and by the SVR-probe fallback in soc_db_detect(). */
size_t soc_db_count(void);
const soc_info_t *soc_db_at(size_t i);

/*
 * Detect the running SoC.
 *
 * Strategy:
 *   1. Read /proc/device-tree/compatible (or the override at
 *      $QORIQ_RCW_DT_COMPAT_FILE for tests). Each NUL-separated entry
 *      is looked up via soc_db_find_by_compat().
 *   2. If DT detection fails, the caller may probe SVR directly
 *      (soc_db_find_by_svr() once an SVR has been read).
 *
 * On success returns a non-NULL soc_info_t* and writes a one-line
 * description of how detection succeeded into `detail` (e.g.
 * "/proc/device-tree/compatible: fsl,lx2160a"). On failure returns
 * NULL and writes the reason into `detail`.
 */
const soc_info_t *soc_db_detect(char *detail, size_t detail_size);
