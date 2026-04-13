/*
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright 2026 Free Mobile - Vincent Jardin
 */

#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "flash_dump.h"

/* Preamble + load-cmd constants from rcw_internal.h (kept in sync). */
#define PBL_PREAMBLE          0xAA55AA55u
#define PBL_CMD_LOAD_RCW      0x80100000u /* with checksum */
#define PBL_CMD_LOAD_RCW_NCS  0x80110000u /* no checksum */
#define PBL_CMD_STOP          0x80FF0000u
#define PBL_CMD_CRC_STOP      0x808F0000u

#define RCW_BYTES             128u /* same as runtime_dump.h's RCW_DUMP_BYTES */

void
flash_slot_free(flash_slot_t *slot) {
  if (!slot)
    return;

  free(slot->data);
  slot->data = NULL;
  slot->data_len = 0;
}

int
flash_dump_read_slot(const char *device, uint64_t offset, const char *slot_name, flash_slot_t *out, char *err, size_t errlen) {
  if (!device || !out) {
    snprintf(err, errlen, "internal: NULL argument");
    return -EINVAL;
  }

  int fd = open(device, O_RDONLY);
  if (fd < 0) {
    snprintf(err, errlen, "open(%s): %s", device, strerror(errno));
    return -errno;
  }

  if (lseek(fd, (off_t)offset, SEEK_SET) == (off_t)-1) {
    int saved = errno;
    close(fd);
    snprintf(err, errlen, "lseek(%s, 0x%llx): %s", device,
             (unsigned long long)offset, strerror(saved));
    return -saved;
  }

  uint8_t *buf = malloc(FLASH_SLOT_READ_BYTES);
  if (!buf) {
    close(fd);
    snprintf(err, errlen, "out of memory");
    return -ENOMEM;
  }

  /* Read up to FLASH_SLOT_READ_BYTES; short reads are normal at EOF
   * (e.g., MTD partition smaller than 16 KiB, fallback slot past end
   * of an unprogrammed device).
   */
  size_t total = 0;
  while (total < FLASH_SLOT_READ_BYTES) {
    ssize_t n = read(fd, buf + total, FLASH_SLOT_READ_BYTES - total);
    if (n < 0) {
      if (errno == EINTR) continue;
      int saved = errno;
      free(buf);
      close(fd);
      snprintf(err, errlen, "read(%s): %s", device, strerror(saved));
      return -saved;
    }
    if (n == 0)
      break; /* EOF */
    total += (size_t)n;
  }
  close(fd);

  out->name = slot_name;
  out->offset = offset;
  out->data = buf;
  out->data_len = total;
  out->pbl_len = total; /* refined by sanity_run() once terminator is found */
  err[0] = '\0';

  return 0;
}

/* Lifted from runtime_dump.c - keep in sync. */
typedef struct {
  sanity_status_t status;
  const char *id;
  char detail[160];
} _entry_t;
_Static_assert(sizeof(_entry_t) == sizeof(sanity_entry_t), "sanity_entry_t layout drift");

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

static uint32_t
read_le32(const uint8_t *p) {
  return  (uint32_t)p[0]
       | ((uint32_t)p[1] << 8)
       | ((uint32_t)p[2] << 16)
       | ((uint32_t)p[3] << 24)
       ;
}

void
flash_dump_sanity_run(const flash_slot_t *slot, const uint8_t *live_rcw, sanity_log_t *log) {
  memset(log, 0, sizeof(*log));

  if (!slot || !slot->data || slot->data_len < 8) {
    log_add(log, SANITY_FAIL, "preamble", "slot too short (%zu bytes) - no PBL header", slot ? slot->data_len : 0);
    return;
  }

  /* preamble */
  uint32_t pre = read_le32(slot->data);
  if (pre == PBL_PREAMBLE)
    log_add(log, SANITY_PASS, "preamble", "0xAA55AA55 at offset 0");
  else
    log_add(log, SANITY_FAIL, "preamble", "expected 0xAA55AA55 at offset 0, got 0x%08x", pre);

  /* load_cmd */
  uint32_t load = read_le32(slot->data + 4);
  bool with_checksum = (load == PBL_CMD_LOAD_RCW);
  bool no_checksum = (load == PBL_CMD_LOAD_RCW_NCS);
  if (with_checksum)
    log_add(log, SANITY_PASS, "load_cmd", "0x80100000 (Load RCW with checksum)");
  else if (no_checksum)
    log_add(log, SANITY_PASS, "load_cmd", "0x80110000 (Load RCW without checksum)");
  else
    log_add(log, SANITY_FAIL, "load_cmd", "expected 0x8010_0000 or 0x8011_0000, got 0x%08x", load);

  /* RCW present */
  if (slot->data_len < 8 + RCW_BYTES) {
    log_add(log, SANITY_FAIL, "rcw_present", "slot truncated - only %zu bytes (need >= %u)", slot->data_len, 8 + RCW_BYTES);
    return;
  }
  const uint8_t *rcw_bytes = slot->data + 8;

  /* checksum (only when with-checksum) */
  if (with_checksum && slot->data_len >= 8 + RCW_BYTES + 4) {
    uint32_t sum = 0;
    /* Sum the (8 + 128) bytes preceding the checksum, as 32-bit LE words. */
    for (size_t i = 0; i + 4 <= 8 + RCW_BYTES; i += 4)
      sum += read_le32(slot->data + i);
    uint32_t stored = read_le32(slot->data + 8 + RCW_BYTES);
    if (sum == stored)
      log_add(log, SANITY_PASS, "checksum", "stored 0x%08x matches computed sum", stored);
    else
      log_add(log, SANITY_FAIL, "checksum", "stored 0x%08x != computed 0x%08x", stored, sum);
  } else if (no_checksum) {
    log_add(log, SANITY_PASS, "checksum", "skipped (load_cmd is no-checksum)");
  }

  /* terminator search - Stop or CRC+Stop, at any 4-byte alignment */
  bool found_term = false;
  size_t term_off = 0;
  uint32_t term_word = 0;
  size_t scan_start = with_checksum ? (8 + RCW_BYTES + 4) : (8 + RCW_BYTES);
  for (size_t off = scan_start; off + 4 <= slot->data_len; off += 4) {
    uint32_t w = read_le32(slot->data + off);
    if (w == PBL_CMD_STOP || w == PBL_CMD_CRC_STOP) {
      found_term = true;
      term_off = off;
      term_word = w;
      break;
    }
  }
  if (found_term) {
    log_add(log, SANITY_PASS, "terminator", "%s at byte %zu (0x%08x)", term_word == PBL_CMD_CRC_STOP ? "CRC+Stop" : "Stop", term_off, term_word);
    /* Trim slot to the PBL extent: terminator command + 4-byte trailer
     * (CRC value or zero). The remainder of the buffer is flash padding
     * (typically 0xFF) and shouldn't be fed to the disassembler.
     * Cast away const because pbl_len is mutable state on the slot. */
    if (term_off + 8 <= slot->data_len)
      ((flash_slot_t *)slot)->pbl_len = term_off + 8;
  } else {
    log_add(log, SANITY_WARN, "terminator", "no Stop/CRC-Stop found in %zu bytes (slot may be truncated)", slot->data_len);
  }

  /* rcw_consistent (optional) */
  if (live_rcw) {
    if (memcmp(rcw_bytes, live_rcw, RCW_BYTES) == 0)
      log_add(log, SANITY_PASS, "rcw_consistent", "on-flash RCW matches live RCWSR snapshot");
    else
      log_add(log, SANITY_WARN, "rcw_consistent", "on-flash RCW differs from live RCWSR - perhaps reading the fallback slot, or flash was rewritten since boot");
  }

  /* pbi_recovered - informational */
  size_t pbi_bytes = 0;
  if (found_term) {
    /* PBI sits between Load RCW (+ optional checksum) and the terminator. */
    pbi_bytes = (term_off > scan_start) ? (term_off - scan_start) : 0;
  }

  log_add(log, SANITY_PASS, "pbi_recovered", "%zu bytes of PBI commands present (--dump cannot recover these)", pbi_bytes);
}

void
flash_dump_compare_slots(const flash_slot_t *primary, const flash_slot_t *fallback, sanity_log_t *log) {
  if (!primary || !fallback || !primary->data || !fallback->data) {
    log_add(log, SANITY_WARN, "fallback_match", "skipped (one or both slots empty)");
    return;
  }
  size_t n = primary->data_len < fallback->data_len
             ? primary->data_len : fallback->data_len;
  if (n == 0) {
    log_add(log, SANITY_WARN, "fallback_match", "skipped (zero bytes to compare)");
    return;
  }
  if (memcmp(primary->data, fallback->data, n) == 0)
    log_add(log, SANITY_PASS, "fallback_match", "primary and fallback slots agree across %zu bytes", n);
  else
    log_add(log, SANITY_WARN, "fallback_match", "primary and fallback slots differ - could be intentional (A/B images) or accidental drift");
}

static const char *
status_label(sanity_status_t s) {
  switch (s) {
    case SANITY_PASS:
      return "PASS";
    case SANITY_WARN:
      return "WARN";
    case SANITY_FAIL:
      return "FAIL";
  }

  return "????";
}

int
flash_dump_write_header(FILE *out,
                        const soc_info_t *soc,
                        uint32_t svr,
                        uint32_t porsr1,
                        uint32_t rcw_src,
                        const flash_info_t *flash,
                        const char *device,
                        const flash_slot_t *slot,
                        const sanity_log_t *log,
                        const char *tool_version) {
  char tstamp[32] = "unknown";
  time_t now = time(NULL);
  struct tm tmv;
  if (gmtime_r(&now, &tmv))
    strftime(tstamp, sizeof(tstamp), "%Y-%m-%dT%H:%M:%SZ", &tmv);

  fprintf(out,
"/*\n"
" * Reset Configuration Word - flash dump (%s slot)\n"
" *\n"
" * SoC family    : %s\n"
" * SoC compatible: %s\n"
" * SVR           : 0x%08x\n"
" * PORSR1        : 0x%08x  (RCW_SRC=0b%u%u%u%u -> %s)\n"
" * Boot device   : %s\n"
" * Slot          : %s  (offset 0x%08llx, %zu/%zu bytes PBL/read)\n"
" *\n"
" * Sanity checks:\n"
    , slot->name
    , soc->pretty_name
    , soc->compat
    , svr
    , porsr1
    , (rcw_src >> 3) & 1, (rcw_src >> 2) & 1, (rcw_src >> 1) & 1, rcw_src & 1
    , flash->rcw_src_name
    , device
    , slot->name
    , (unsigned long long)slot->offset
    , slot->pbl_len, slot->data_len
    );

  for (size_t i = 0; i < log->count; i++) {
    const sanity_entry_t *e = &log->entries[i];
    fprintf(out, " *   [%s] %-16s : %s\n", status_label(e->status), e->id, e->detail);
  }

  fprintf(out,
" *\n"
" * Captured: %s by qoriq-rcw %s\n"
" */\n\n",
    tstamp, tool_version ? tool_version : "0.0.0");

  return 0;
}
