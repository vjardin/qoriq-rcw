/*
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright 2026 Free Mobile - Vincent Jardin
 */

#define _POSIX_C_SOURCE 200809L

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <setjmp.h>
#include <unistd.h>
#include <cmocka.h>

#include "soc_db.h"

static void
test_compat_lookup(void **state) {
  (void)state;
  const soc_info_t *s = soc_db_find_by_compat("fsl,lx2160a");
  assert_non_null(s);
  assert_string_equal(s->compat, "fsl,lx2160a");
  assert_string_equal(s->default_rcwi, "lx2160a.rcwi");
  assert_int_equal(s->dcfg_base, 0x01E00000);
  assert_int_equal(s->rcwsr1_offset, 0x100);
  assert_int_equal(s->svr_offset, 0xA4);
  assert_int_equal(s->rcw_bytes, 128);
}

static void
test_compat_unknown(void **state) {
  (void)state;
  assert_null(soc_db_find_by_compat("fsl,acme-mips"));
  assert_null(soc_db_find_by_compat(""));
  assert_null(soc_db_find_by_compat(NULL));
}

static void
test_svr_lookup_masks_revision(void **state) {
  (void)state;
  /* LX2160A reset value 0x87360120 (rev 1.2 per the doc) - should
   * still match the entry whose svr_match is the family pattern. */
  const soc_info_t *s = soc_db_find_by_svr(0x87360120);
  assert_non_null(s);
  assert_string_equal(s->compat, "fsl,lx2160a");

  /* A different revision still matches. */
  s = soc_db_find_by_svr(0x873601FF);
  assert_non_null(s);
  assert_string_equal(s->compat, "fsl,lx2160a");
}

static void
test_svr_lookup_distinguishes_families(void **state) {
  (void)state;
  assert_string_equal(soc_db_find_by_svr(0x87360100)->compat, "fsl,lx2160a");
  assert_string_equal(soc_db_find_by_svr(0x87370100)->compat, "fsl,lx2120a");
  assert_string_equal(soc_db_find_by_svr(0x87380100)->compat, "fsl,lx2080a");
  assert_string_equal(soc_db_find_by_svr(0x870B0010)->compat, "fsl,ls1028a");
  assert_string_equal(soc_db_find_by_svr(0x87030001)->compat, "fsl,ls1088a");
  assert_string_equal(soc_db_find_by_svr(0x87090020)->compat, "fsl,ls2088a");
  assert_null(soc_db_find_by_svr(0xDEADBEEF));
}

static void
test_lx2120_lx2080_share_lx2160_rcwi(void **state) {
  (void)state;
  assert_string_equal(soc_db_find_by_compat("fsl,lx2120a")->default_rcwi,
                      "lx2160a.rcwi");
  assert_string_equal(soc_db_find_by_compat("fsl,lx2080a")->default_rcwi,
                      "lx2160a.rcwi");
}

static void
test_table_iteration_nonempty(void **state) {
  (void)state;
  size_t n = soc_db_count();
  assert_true(n >= 4);
  for (size_t i = 0; i < n; i++) {
    const soc_info_t *s = soc_db_at(i);
    assert_non_null(s);
    assert_non_null(s->compat);
    assert_non_null(s->default_rcwi);
  }
  assert_null(soc_db_at(n));
}

/* Write a NUL-separated DT compatible blob to a tmpfile, point
 * QORIQ_RCW_DT_COMPAT_FILE at it, and exercise soc_db_detect(). */
static char *
make_dt_fixture(const char *const *entries, size_t n) {
  char tmpl[] = "/tmp/soc-dt-compatXXXXXX";
  int fd = mkstemp(tmpl);
  assert_true(fd >= 0);
  for (size_t i = 0; i < n; i++) {
    write(fd, entries[i], strlen(entries[i]) + 1);
  }
  close(fd);
  return strdup(tmpl);
}

static void
test_detect_dt_match_first_entry(void **state) {
  (void)state;
  const char *e[] = { "fsl,lx2160a", "arm,armv8" };
  char *fix = make_dt_fixture(e, 2);
  setenv("QORIQ_RCW_DT_COMPAT_FILE", fix, 1);

  char detail[256] = {0};
  const soc_info_t *s = soc_db_detect(detail, sizeof(detail));
  assert_non_null(s);
  assert_string_equal(s->compat, "fsl,lx2160a");
  assert_non_null(strstr(detail, "fsl,lx2160a"));

  unsetenv("QORIQ_RCW_DT_COMPAT_FILE");
  unlink(fix);
  free(fix);
}

static void
test_detect_dt_match_later_entry(void **state) {
  (void)state;
  /* "Most specific" first per DT convention; first entry may be
   * board-specific (no match) and the second one the SoC. */
  const char *e[] = { "vendor,my-board", "fsl,ls1028a", "arm,armv8" };
  char *fix = make_dt_fixture(e, 3);
  setenv("QORIQ_RCW_DT_COMPAT_FILE", fix, 1);

  char detail[256] = {0};
  const soc_info_t *s = soc_db_detect(detail, sizeof(detail));
  assert_non_null(s);
  assert_string_equal(s->compat, "fsl,ls1028a");

  unsetenv("QORIQ_RCW_DT_COMPAT_FILE");
  unlink(fix);
  free(fix);
}

static void
test_detect_no_match(void **state) {
  (void)state;
  const char *e[] = { "vendor,unknown", "arm,armv8" };
  char *fix = make_dt_fixture(e, 2);
  setenv("QORIQ_RCW_DT_COMPAT_FILE", fix, 1);

  char detail[256] = {0};
  const soc_info_t *s = soc_db_detect(detail, sizeof(detail));
  assert_null(s);
  assert_non_null(strstr(detail, "no entry matched"));

  unsetenv("QORIQ_RCW_DT_COMPAT_FILE");
  unlink(fix);
  free(fix);
}

static void
test_detect_missing_dt_file(void **state) {
  (void)state;
  setenv("QORIQ_RCW_DT_COMPAT_FILE", "/nonexistent/path/xyzzy", 1);

  char detail[256] = {0};
  const soc_info_t *s = soc_db_detect(detail, sizeof(detail));
  assert_null(s);
  assert_non_null(strstr(detail, "could not open"));

  unsetenv("QORIQ_RCW_DT_COMPAT_FILE");
}

int
main(void) {
  const struct CMUnitTest tests[] = {
    cmocka_unit_test(test_compat_lookup),
    cmocka_unit_test(test_compat_unknown),
    cmocka_unit_test(test_svr_lookup_masks_revision),
    cmocka_unit_test(test_svr_lookup_distinguishes_families),
    cmocka_unit_test(test_lx2120_lx2080_share_lx2160_rcwi),
    cmocka_unit_test(test_table_iteration_nonempty),
    cmocka_unit_test(test_detect_dt_match_first_entry),
    cmocka_unit_test(test_detect_dt_match_later_entry),
    cmocka_unit_test(test_detect_no_match),
    cmocka_unit_test(test_detect_missing_dt_file),
  };
  return cmocka_run_group_tests(tests, NULL, NULL);
}
