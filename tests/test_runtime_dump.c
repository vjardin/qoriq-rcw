/*
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright 2026 Free Mobile - Vincent Jardin
 *
 * Unit tests for runtime_dump.c.
 *
 * runtime_read_rcw() is exercised against a temp fixture file (no
 * /dev/mem, no root needed). The sanity-check suite is exercised
 * against synthetic 128-byte buffers.
 */

#define _POSIX_C_SOURCE 200809L

#include <fcntl.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <setjmp.h>
#include <sys/stat.h>
#include <unistd.h>
#include <cmocka.h>

#include "runtime_dump.h"
#include "soc_db.h"

/*
 * Build a 4 KiB DCFG page fixture:
 *   - SVR at offset 0xA4
 *   - 128-byte RCW at offset 0x100 (filled with `fill_byte` unless a
 *     custom buffer is provided)
 * Returns the temp file path (caller frees + unlinks).
 */
static char *
make_page_fixture(uint32_t svr_le, const uint8_t *rcw_or_null,
                  uint8_t fill_byte) {
  char tmpl[] = "/tmp/qoriq-page-XXXXXX";
  int fd = mkstemp(tmpl);
  assert_true(fd >= 0);

  uint8_t page[4096];
  memset(page, 0, sizeof(page));

  /* SVR @ 0xA4, little-endian native uint32 store */
  memcpy(page + 0xA4, &svr_le, sizeof(svr_le));

  /* RCW @ 0x100 */
  if (rcw_or_null)
    memcpy(page + 0x100, rcw_or_null, 128);
  else
    memset(page + 0x100, fill_byte, 128);

  ssize_t n = write(fd, page, sizeof(page));
  assert_int_equal(n, sizeof(page));
  close(fd);
  return strdup(tmpl);
}

/* A slightly less artificial RCW for sanity-check tests: enough nonzero
 * bytes to satisfy not_zero, with a small PBI_LENGTH. */
static void
fill_realistic_rcw(uint8_t rcw[128]) {
  memset(rcw, 0, 128);
  rcw[0]  = 0x38;  /* SYS_PLL_RAT-ish bits */
  rcw[1]  = 0x54;
  rcw[34] = 0x00;  /* PBI_LENGTH high byte */
  rcw[35] = 0x42;  /* PBI_LENGTH low byte = 66 (sane) */
}

/* ------------------------------------------------------------------ */
/* runtime_read_rcw                                                   */
/* ------------------------------------------------------------------ */

static void
test_read_rcw_from_fixture(void **state) {
  (void)state;
  uint8_t rcw_in[128];
  for (size_t i = 0; i < 128; i++) rcw_in[i] = (uint8_t)i;

  char *fix = make_page_fixture(0x87360120, rcw_in, 0);

  /* Override dcfg_base = 0 so mmap targets the start of our fixture
   * file. The other fields stay at the LX2160 values. */
  soc_info_t soc = *soc_db_find_by_compat("fsl,lx2160a");
  soc.dcfg_base = 0;

  uint8_t  rcw_out[128] = {0};
  uint32_t svr_out = 0;
  char err[256] = {0};

  int rc = runtime_read_rcw(&soc, fix, rcw_out, &svr_out,
                            err, sizeof(err));
  assert_int_equal(rc, 0);
  assert_int_equal(svr_out, 0x87360120);
  assert_memory_equal(rcw_out, rcw_in, 128);

  unlink(fix);
  free(fix);
}

static void
test_read_rcw_open_fails(void **state) {
  (void)state;
  const soc_info_t *soc = soc_db_find_by_compat("fsl,lx2160a");
  uint8_t  rcw_out[128];
  uint32_t svr_out = 0;
  char err[256] = {0};

  int rc = runtime_read_rcw(soc, "/nonexistent/path/qzzx",
                            rcw_out, &svr_out, err, sizeof(err));
  assert_int_not_equal(rc, 0);
  assert_non_null(strstr(err, "open"));
}

/* ------------------------------------------------------------------ */
/* runtime_sanity_run                                                 */
/* ------------------------------------------------------------------ */

static const sanity_entry_t *
find_entry(const sanity_log_t *log, const char *id) {
  for (size_t i = 0; i < log->count; i++)
    if (strcmp(log->entries[i].id, id) == 0)
      return &log->entries[i];
  return NULL;
}

static void
test_sanity_all_pass_normal(void **state) {
  (void)state;
  const soc_info_t *soc = soc_db_find_by_compat("fsl,lx2160a");
  uint8_t rcw[128];
  fill_realistic_rcw(rcw);

  sanity_log_t log;
  runtime_sanity_run(soc, "match-detail", 0x87360120, rcw, &log);

  assert_false(log.any_failed);
  assert_int_equal(find_entry(&log, "dt_compat")->status, SANITY_PASS);
  assert_int_equal(find_entry(&log, "svr_match")->status, SANITY_PASS);
  assert_int_equal(find_entry(&log, "not_zero")->status, SANITY_PASS);
  assert_int_equal(find_entry(&log, "not_ones")->status, SANITY_PASS);
  assert_int_equal(find_entry(&log, "endianness")->status, SANITY_PASS);
  /* informational warning is always present */
  assert_int_equal(find_entry(&log, "pbi_not_recoverable")->status,
                   SANITY_WARN);
}

static void
test_sanity_all_zero_rcw_fails(void **state) {
  (void)state;
  const soc_info_t *soc = soc_db_find_by_compat("fsl,lx2160a");
  uint8_t rcw[128] = {0};

  sanity_log_t log;
  runtime_sanity_run(soc, "match-detail", 0x87360120, rcw, &log);

  assert_true(log.any_failed);
  assert_int_equal(find_entry(&log, "not_zero")->status, SANITY_FAIL);
}

static void
test_sanity_all_ones_rcw_fails(void **state) {
  (void)state;
  const soc_info_t *soc = soc_db_find_by_compat("fsl,lx2160a");
  uint8_t rcw[128];
  memset(rcw, 0xFF, sizeof(rcw));

  sanity_log_t log;
  runtime_sanity_run(soc, "match-detail", 0x87360120, rcw, &log);

  assert_true(log.any_failed);
  assert_int_equal(find_entry(&log, "not_ones")->status, SANITY_FAIL);
}

static void
test_sanity_wrong_svr_fails(void **state) {
  (void)state;
  const soc_info_t *soc = soc_db_find_by_compat("fsl,lx2160a");
  uint8_t rcw[128];
  fill_realistic_rcw(rcw);

  sanity_log_t log;
  runtime_sanity_run(soc, "match-detail", 0xDEADBEEF, rcw, &log);

  assert_true(log.any_failed);
  assert_int_equal(find_entry(&log, "svr_match")->status, SANITY_FAIL);
}

static void
test_sanity_byteswapped_svr_warns(void **state) {
  (void)state;
  const soc_info_t *soc = soc_db_find_by_compat("fsl,lx2160a");
  uint8_t rcw[128];
  fill_realistic_rcw(rcw);

  /* If the bus is wrong-endian we'd read 0x87360120 as 0x20013687. */
  uint32_t bswapped = 0x20013687u;

  sanity_log_t log;
  runtime_sanity_run(soc, "match-detail", bswapped, rcw, &log);

  /* svr_match still FAILs (we don't auto-swap), and endianness WARNs
   * with a hint that the data may need swapping. */
  assert_true(log.any_failed);
  const sanity_entry_t *e = find_entry(&log, "endianness");
  assert_int_equal(e->status, SANITY_WARN);
  assert_non_null(strstr(e->detail, "byte-swapped"));
}

static void
test_sanity_dt_unavailable_warns(void **state) {
  (void)state;
  const soc_info_t *soc = soc_db_find_by_compat("fsl,lx2160a");
  uint8_t rcw[128];
  fill_realistic_rcw(rcw);

  sanity_log_t log;
  runtime_sanity_run(soc, NULL, 0x87360120, rcw, &log);

  assert_int_equal(find_entry(&log, "dt_compat")->status, SANITY_WARN);
  /* The other checks still pass. */
  assert_int_equal(find_entry(&log, "svr_match")->status, SANITY_PASS);
  assert_false(log.any_failed);
}

/* ------------------------------------------------------------------ */
/* runtime_write_header                                               */
/* ------------------------------------------------------------------ */

static void
test_write_header_contains_key_fields(void **state) {
  (void)state;
  const soc_info_t *soc = soc_db_find_by_compat("fsl,lx2160a");
  uint8_t rcw[128];
  fill_realistic_rcw(rcw);
  sanity_log_t log;
  runtime_sanity_run(soc, "fsl,lx2160a", 0x87360120, rcw, &log);

  char buf[8192];
  FILE *out = fmemopen(buf, sizeof(buf), "w");
  assert_non_null(out);
  runtime_write_header(out, soc, 0x87360120, &log, "0.1.0");
  fclose(out);

  assert_non_null(strstr(buf, "Reset Configuration Word"));
  assert_non_null(strstr(buf, "NXP Layerscape LX2160A"));
  assert_non_null(strstr(buf, "fsl,lx2160a"));
  assert_non_null(strstr(buf, "0x87360120"));
  assert_non_null(strstr(buf, "0x01e00000"));
  assert_non_null(strstr(buf, "0x100"));   /* RCWSR1 offset */
  assert_non_null(strstr(buf, "qoriq-rcw 0.1.0"));
  assert_non_null(strstr(buf, "[PASS]"));
  assert_non_null(strstr(buf, "[WARN]")); /* pbi_not_recoverable */
}

int
main(void) {
  const struct CMUnitTest tests[] = {
    cmocka_unit_test(test_read_rcw_from_fixture),
    cmocka_unit_test(test_read_rcw_open_fails),
    cmocka_unit_test(test_sanity_all_pass_normal),
    cmocka_unit_test(test_sanity_all_zero_rcw_fails),
    cmocka_unit_test(test_sanity_all_ones_rcw_fails),
    cmocka_unit_test(test_sanity_wrong_svr_fails),
    cmocka_unit_test(test_sanity_byteswapped_svr_warns),
    cmocka_unit_test(test_sanity_dt_unavailable_warns),
    cmocka_unit_test(test_write_header_contains_key_fields),
  };
  return cmocka_run_group_tests(tests, NULL, NULL);
}
