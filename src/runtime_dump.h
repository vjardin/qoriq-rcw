/*
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright 2026 Free Mobile - Vincent Jardin
 *
 * Read the live RCWSR registers via /dev/mem and sanity-check the
 * resulting 128-byte dump.
 *
 * Everything platform-specific is contained in this module. The library
 * itself is unchanged: the dump is fed to rcw_decompile_buffer().
 */

#ifndef QORIQ_RCW_RUNTIME_DUMP_H
#define QORIQ_RCW_RUNTIME_DUMP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "soc_db.h"

/* RCW_BYTES is fixed at 128 for pbiformat=2; soc_info_t carries the
 * number too in case future entries deviate. */
#define RCW_DUMP_BYTES 128u

typedef enum {
  SANITY_PASS = 0,
  SANITY_WARN,
  SANITY_FAIL,
} sanity_status_t;

typedef struct {
  sanity_status_t status;
  const char     *id;       /* short identifier, e.g. "svr_match" */
  char            detail[160]; /* one-line human description */
} sanity_entry_t;

#define SANITY_LOG_MAX 16

typedef struct {
  sanity_entry_t entries[SANITY_LOG_MAX];
  size_t         count;
  bool           any_failed;
} sanity_log_t;

/*
 * Open `mem_path` (typically "/dev/mem"), mmap one page at
 * `soc->dcfg_base`, copy 128 bytes from offset `soc->rcwsr1_offset`
 * into `rcw_out`, copy the 32-bit SVR at offset `soc->svr_offset`
 * into `*svr_out`. Returns 0 on success or -errno on failure (with a
 * human-readable message in `err`).
 */
int runtime_read_rcw(const soc_info_t *soc, const char *mem_path, uint8_t rcw_out[RCW_DUMP_BYTES], uint32_t *svr_out, char *err, size_t errlen);

/*
 * Read PORSR1 (DCFG offset 0x0) - captures the CFG_RCW_SRC pin straps
 * sampled at PORESET. Used by --dump-flash to learn which boot medium
 * the bootrom loaded the RCW from.
 */
int runtime_read_porsr1(const soc_info_t *soc, const char *mem_path, uint32_t *porsr1_out, char *err, size_t errlen);

/*
 * Run the sanity-check battery. Always populates `log` with one entry
 * per check; never returns an error. Caller decides what to do with
 * `log->any_failed`.
 *
 * If `dt_detail` is NULL, the dt_compat check is recorded as WARN
 * ("DT compatible unavailable; SoC identified via SVR probe").
 */
void runtime_sanity_run(const soc_info_t *soc, const char *dt_detail, uint32_t svr, const uint8_t rcw[RCW_DUMP_BYTES], sanity_log_t *log);

/*
 * Write the C-comment header describing the dump (SoC, addresses,
 * sanity log, capture time, tool version) to `out`. Returns 0 on
 * success.
 */
int runtime_write_header(FILE *out, const soc_info_t *soc, uint32_t svr, const sanity_log_t *log, const char *tool_version);

#endif /* QORIQ_RCW_RUNTIME_DUMP_H */
