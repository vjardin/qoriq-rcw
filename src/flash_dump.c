/*
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright 2026 Free Mobile - Vincent Jardin
 */

#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "flash_dump.h"
#include "pbl_check.h"

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

  memset(out, 0, sizeof(*out));
  out->name = slot_name;
  out->offset = offset;
  out->data = buf;
  out->data_len = total;
  out->pbl_len = total; /* refined by sanity_run() once the terminator is found */
  err[0] = '\0';

  return 0;
}

void
flash_dump_sanity_run(flash_slot_t *slot, const uint8_t *live_rcw, sanity_log_t *log) {
  memset(log, 0, sizeof(*log));

  if (!slot || !slot->data) {
    sanity_log_add(log, SANITY_FAIL, "preamble", "no slot data");
    return;
  }

  /* Every pbiformat=2 .rcwi sets %littleendian=1. */
  rcw_pbl_report_t rep;
  rcw_pbl_verify(slot->data, slot->data_len, 1, NULL, NULL, &rep);
  pbl_sanity_run(&rep, slot->data_len, live_rcw, log);

  slot->pbl_len = rep.image_end;
  if (rep.envelope == RCW_OK) {
    slot->pbi_length = rep.pbl.pbi_length;
    slot->sb_en = rep.pbl.sb_en != 0;
    slot->ncmd = rep.chk.ncmd;
  }
}

void
flash_dump_compare_slots(const flash_slot_t *primary, const flash_slot_t *fallback, sanity_log_t *log) {
  if (!primary || !fallback || !primary->data || !fallback->data) {
    sanity_log_add(log, SANITY_WARN, "fallback_match", "skipped (one or both slots empty)");
    return;
  }
  if (primary->pbl_len == 0 || fallback->pbl_len == 0) {
    sanity_log_add(log, SANITY_WARN, "fallback_match", "skipped (zero bytes to compare)");
    return;
  }
  if (primary->pbl_len != fallback->pbl_len)
    sanity_log_add(log, SANITY_WARN, "fallback_match", "primary (%zu bytes) and fallback (%zu bytes) PBL images differ in size", primary->pbl_len, fallback->pbl_len);
  else if (memcmp(primary->data, fallback->data, primary->pbl_len) == 0)
    sanity_log_add(log, SANITY_PASS, "fallback_match", "primary and fallback PBL images are identical (%zu bytes)", primary->pbl_len);
  else
    sanity_log_add(log, SANITY_WARN, "fallback_match", "primary and fallback slots differ - could be intentional (A/B images) or accidental drift");
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
" * PBI           : RCW[PBI_LENGTH]=%u words, %zu commands incl. terminator, SB_EN=%u\n"
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
    , slot->pbi_length, slot->ncmd, (unsigned)slot->sb_en
    );

  for (size_t i = 0; i < log->count; i++) {
    const sanity_entry_t *e = &log->entries[i];
    fprintf(out, " *   [%s] %-16s : %s\n", sanity_status_label(e->status), e->id, e->detail);
  }

  fprintf(out,
" *\n"
" * Captured: %s by qoriq-rcw %s\n"
" */\n\n",
    tstamp, tool_version ? tool_version : "0.0.0");

  return 0;
}
