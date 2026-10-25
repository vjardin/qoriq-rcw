/*
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright 2026 Free Mobile - Vincent Jardin
 *
 * The sanity log: PASS / WARN / FAIL entry per check, printed as a
 * C comment at the top of the --dump and --dump-flash output and as
 * the --check report.
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>

typedef enum {
  SANITY_PASS = 0,
  SANITY_WARN,
  SANITY_FAIL,
} sanity_status_t;

typedef struct {
  sanity_status_t status;
  const char     *id;          /* short identifier, e.g. "svr_match" */
  char            detail[160]; /* one-line human description */
} sanity_entry_t;

#define SANITY_LOG_MAX 16

typedef struct {
  sanity_entry_t entries[SANITY_LOG_MAX];
  size_t         count;
  bool           any_failed;
} sanity_log_t;

void sanity_log_add(sanity_log_t *log, sanity_status_t st, const char *id, const char *fmt, ...);

/* "PASS" / "WARN" / "FAIL" */
const char *sanity_status_label(sanity_status_t s);
