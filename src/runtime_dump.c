/*
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright 2026 Free Mobile - Vincent Jardin
 */

#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "runtime_dump.h"

static void
log_add(sanity_log_t *log, sanity_status_t st, const char *id, const char *fmt, ...) {
  if (log->count >= SANITY_LOG_MAX)
    return;

  sanity_entry_t *e = &log->entries[log->count++];
  e->status = st;
  e->id = id;

  va_list ap;
  va_start(ap, fmt);
  vsnprintf(e->detail, sizeof(e->detail), fmt, ap);
  va_end(ap);

  if (st == SANITY_FAIL)
    log->any_failed = true;
}

int
runtime_read_rcw(const soc_info_t *soc, const char *mem_path, uint8_t rcw_out[RCW_DUMP_BYTES], uint32_t *svr_out, char *err, size_t errlen) {
  if (!soc || !mem_path || !rcw_out || !svr_out) {
    snprintf(err, errlen, "internal: NULL argument");
    return -EINVAL;
  }

  int fd = open(mem_path, O_RDONLY | O_SYNC);
  if (fd < 0) {
    snprintf(err, errlen, "open(%s): %s", mem_path, strerror(errno));
    return -errno;
  }

  long page = sysconf(_SC_PAGESIZE);
  if (page <= 0)
    page = 4096;

  /*
   * Map exactly one page. RCWSR1..32 (128 bytes starting at offset
   * 0x100) plus SVR (offset 0xA4) all fit comfortably in the first
   * page of DCFG.
   *
   * For /dev/mem (a character device) we offset the mmap by
   * `soc->dcfg_base` to land on the DCFG block. For a regular file
   * (test fixture), the DCFG page IS the file: mmap from offset 0.
   */
  off_t map_off = (off_t)soc->dcfg_base;
  struct stat st;
  if (fstat(fd, &st) == 0 && S_ISREG(st.st_mode))
    map_off = 0;

  void *m = mmap(NULL, (size_t)page, PROT_READ, MAP_SHARED, fd, map_off);
  if (m == MAP_FAILED) {
    int saved = errno;
    close(fd);
    snprintf(err, errlen, "mmap(%s @ 0x%llx): %s", mem_path, (unsigned long long)soc->dcfg_base, strerror(saved));
    return -saved;
  }

  if (soc->rcwsr1_offset + soc->rcw_bytes > (uint32_t)page ||
      soc->svr_offset + 4 > (uint32_t)page) {
    munmap(m, (size_t)page);
    close(fd);
    snprintf(err, errlen, "internal: page size (%ld) too small for SoC layout", page);
    return -EINVAL;
  }

  /*
   * Read SVR as a 32-bit native-endian load, then memcpy the 128-byte
   * RCW snapshot. The CCSR space on Layerscape ARM is little-endian
   * by default, which matches the on-disk RCW binary layout (every
   * pbiformat=2 .rcwi sets %littleendian=1).
   */
  const volatile uint32_t *svr_ptr = (const volatile uint32_t *)((const uint8_t *)m + soc->svr_offset);
  *svr_out = *svr_ptr;

  memcpy(rcw_out, (const uint8_t *)m + soc->rcwsr1_offset, soc->rcw_bytes);

  munmap(m, (size_t)page);
  close(fd);
  err[0] = '\0';
  return 0;
}

void
runtime_sanity_run(const soc_info_t *soc, const char *dt_detail, uint32_t svr, const uint8_t rcw[RCW_DUMP_BYTES], sanity_log_t *log) {
  memset(log, 0, sizeof(*log));

  /* dt_compat */
  if (dt_detail && dt_detail[0])
    log_add(log, SANITY_PASS, "dt_compat", "matched %s", soc->compat);
  else
    log_add(log, SANITY_WARN, "dt_compat", "DT compatible unavailable; SoC identified via SVR probe");

  /* svr_match */
  if ((svr & soc->svr_mask) == soc->svr_match)
    log_add(log, SANITY_PASS, "svr_match", "0x%08x matches expected 0x%08x/0x%08x for %s", svr, soc->svr_match, soc->svr_mask, soc->compat);
  else
    log_add(log, SANITY_FAIL, "svr_match", "0x%08x does not match expected 0x%08x/0x%08x for %s", svr, soc->svr_match, soc->svr_mask, soc->compat);

  /* not_zero */
  size_t nonzero = 0;
  for (size_t i = 0; i < RCW_DUMP_BYTES; i++)
    if (rcw[i] != 0) nonzero++;

  if (nonzero > 0)
    log_add(log, SANITY_PASS, "not_zero", "%zu/%u bytes nonzero", nonzero, RCW_DUMP_BYTES);
  else
    log_add(log, SANITY_FAIL, "not_zero", "RCW dump is all-zero (likely /dev/mem restriction)");

  /* not_ones */
  size_t ones_bytes = 0;
  for (size_t i = 0; i < RCW_DUMP_BYTES; i++)
    if (rcw[i] == 0xFF) ones_bytes++;

  if (ones_bytes < RCW_DUMP_BYTES)
    log_add(log, SANITY_PASS, "not_ones", "not stuck-high (%zu/%u bytes are 0xFF)", ones_bytes, RCW_DUMP_BYTES);
  else
    log_add(log, SANITY_FAIL, "not_ones", "RCW dump is all-0xFF (likely unmapped region)");

  /* endianness - the SVR readback gives us a sanity check on byte
   * order. If the upper 16 bits of the LE-interpreted value match the
   * SoC family pattern, we're good. If they match when byte-swapped,
   * warn loudly (data would need swapping). */
  if ((svr & soc->svr_mask) == soc->svr_match) {
    log_add(log, SANITY_PASS, "endianness", "SVR readback is little-endian (matches %s)", soc->compat);
  } else {
    uint32_t swapped =
        | ((svr & 0xFF000000u) >> 24)
        | ((svr & 0x00FF0000u) >>  8)
        | ((svr & 0x0000FF00u) <<  8)
        | ((svr & 0x000000FFu) << 24)
        ;
    if ((swapped & soc->svr_mask) == soc->svr_match)
      log_add(log, SANITY_WARN, "endianness", "SVR matches when byte-swapped (0x%08x); RCW data may need swapping before decompile", swapped);
    else
      log_add(log, SANITY_WARN, "endianness", "SVR 0x%08x doesn't match either endianness - proceed with caution", svr);
  }

  /* pbi_not_recoverable - informational, always WARN */
  log_add(log, SANITY_WARN, "pbi_not_recoverable", "PBI commands ran once at boot and are not present in runtime registers");
}

static const char *
status_label(sanity_status_t s) {
  switch (s) {
    case SANITY_PASS: return "PASS";
    case SANITY_WARN: return "WARN";
    case SANITY_FAIL: return "FAIL";
  }
  return "????";
}

int
runtime_write_header(FILE *out, const soc_info_t *soc, uint32_t svr, const sanity_log_t *log, const char *tool_version) {
  char tstamp[32] = "unknown";
  time_t now = time(NULL);
  struct tm tmv;
  if (gmtime_r(&now, &tmv))
    strftime(tstamp, sizeof(tstamp), "%Y-%m-%dT%H:%M:%SZ", &tmv);

  fprintf(out,
"/*\n"
" * Reset Configuration Word - runtime dump\n"
" *\n"
" * SoC family    : %s\n"
" * SoC compatible: %s\n"
" * SVR           : 0x%08x  (expected 0x%08x mask 0x%08x)\n"
" * DCFG base     : 0x%08llx\n"
" * RCWSR1 offset : 0x%03x\n"
" * SVR offset    : 0x%03x\n"
" * RCW size      : %zu bytes (1024 bits)\n"
" *\n"
" * Sanity checks:\n"
    , soc->pretty_name
    , soc->compat
    , svr, soc->svr_match, soc->svr_mask
    , (unsigned long long)soc->dcfg_base
    , soc->rcwsr1_offset
    , soc->svr_offset
    , soc->rcw_bytes
    );

  for (size_t i = 0; i < log->count; i++) {
    const sanity_entry_t *e = &log->entries[i];
    fprintf(out, " *   [%s] %-22s : %s\n", status_label(e->status), e->id, e->detail);
  }

  fprintf(out,
" *\n"
" * Captured: %s by qoriq-rcw %s\n"
" */\n\n",
    tstamp, tool_version ? tool_version : "0.0.0");

  return 0;
}
