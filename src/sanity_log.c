/*
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright 2026 Free Mobile - Vincent Jardin
 */

#include <stdarg.h>
#include <stdio.h>

#include "sanity_log.h"

void
sanity_log_add(sanity_log_t *log, sanity_status_t st, const char *id, const char *fmt, ...) {
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

const char *
sanity_status_label(sanity_status_t s) {
  switch (s) {
    case SANITY_PASS: return "PASS";
    case SANITY_WARN: return "WARN";
    case SANITY_FAIL: return "FAIL";
  }
  return "????";
}
