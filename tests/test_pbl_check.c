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
#include <setjmp.h>
#include <string.h>
#include <cmocka.h>

#include "pbl_check.h"
#include "pbl_fixture.h"

static size_t
compile(const char *src, uint8_t *out, size_t cap) {
  size_t len = fix_compile(src, out, cap);

  assert_int_equal(len, FIX_IMG_BYTES);
  return len;
}

static const sanity_entry_t *
find_entry(const sanity_log_t *log, const char *id) {
  for (size_t i = 0; i < log->count; i++)
    if (strcmp(log->entries[i].id, id) == 0)
      return &log->entries[i];

  return NULL;
}

static void
verify(const uint8_t *img, size_t len, rcw_pbl_report_t *rep, sanity_log_t *log) {
  rcw_pbl_verify(img, len, 1, NULL, NULL, rep);
  memset(log, 0, sizeof(*log));
  pbl_sanity_run(rep, len, NULL, log);
}

static void
test_stop_image(void **state) {
  (void)state;
  uint8_t img[256];
  size_t len = compile(FIX_SRC_STOP, img, sizeof(img));

  rcw_pbl_report_t r;
  sanity_log_t log;
  verify(img, len, &r, &log);
  assert_true(r.ok);
  assert_int_equal(r.pbl.pbi_length, FIX_PBI_WORDS);
  assert_int_equal(r.chk.ncmd, FIX_NCMD);
  assert_int_equal(r.image_end, len);

  assert_false(log.any_failed);
  assert_int_equal(find_entry(&log, "preamble")->status, SANITY_PASS);
  assert_int_equal(find_entry(&log, "load_cmd")->status, SANITY_PASS);
  assert_non_null(strstr(find_entry(&log, "load_cmd")->detail, "with checksum"));
  assert_int_equal(find_entry(&log, "checksum")->status, SANITY_PASS);
  assert_int_equal(find_entry(&log, "terminator")->status, SANITY_PASS);
  assert_non_null(strstr(find_entry(&log, "terminator")->detail, "Stop at byte 168"));
  assert_int_equal(find_entry(&log, "pbi_crc")->status, SANITY_PASS);
  assert_non_null(strstr(find_entry(&log, "pbi_crc")->detail, "none"));
  assert_null(find_entry(&log, "rcw_consistent"));
  assert_int_equal(find_entry(&log, "pbi_commands")->status, SANITY_PASS);
  assert_non_null(strstr(find_entry(&log, "pbi_commands")->detail, "4 commands in 36 bytes"));
}

static void
test_crcstop_image(void **state) {
  (void)state;
  uint8_t img[256];
  size_t len = compile(FIX_SRC_CRCSTOP, img, sizeof(img));

  rcw_pbl_report_t r;
  sanity_log_t log;
  verify(img, len, &r, &log);
  assert_true(r.ok);
  assert_non_null(strstr(find_entry(&log, "terminator")->detail, "CRC and Stop"));
  assert_int_equal(find_entry(&log, "pbi_crc")->status, SANITY_PASS);
  assert_non_null(strstr(find_entry(&log, "pbi_crc")->detail, "matches"));

  /* Corrupt the value of the write: the CRC no longer matches. */
  img[RCW_PBL_PBI_OFFSET + FIX_PBI_OFF_WRITE + 4] ^= 0x01;
  verify(img, len, &r, &log);
  assert_false(r.ok);
  assert_true(log.any_failed);
  assert_int_equal(find_entry(&log, "terminator")->status, SANITY_PASS);
  assert_int_equal(find_entry(&log, "pbi_crc")->status, SANITY_FAIL);
}

static void
test_without_checksum(void **state) {
  (void)state;
  uint8_t img[256];
  size_t len = compile(FIX_SRC_NOCS, img, sizeof(img));

  rcw_pbl_report_t r;
  sanity_log_t log;
  verify(img, len, &r, &log);
  assert_true(r.ok);
  assert_non_null(strstr(find_entry(&log, "load_cmd")->detail, "without checksum"));
  assert_non_null(strstr(find_entry(&log, "checksum")->detail, "skipped"));
}

static void
test_checksum_mismatch(void **state) {
  (void)state;
  uint8_t img[256];
  size_t len = compile(FIX_SRC_STOP, img, sizeof(img));
  img[RCW_PBL_RCW_OFFSET] ^= 0x01; /* SYS_PLL_RAT bits, not PBI_LENGTH */

  rcw_pbl_report_t r;
  sanity_log_t log;
  verify(img, len, &r, &log);
  assert_false(r.ok);
  assert_int_equal(find_entry(&log, "checksum")->status, SANITY_FAIL);
  /* The PBI is still fine. */
  assert_int_equal(find_entry(&log, "terminator")->status, SANITY_PASS);
}

static void
test_pbi_length_too_small(void **state) {
  (void)state;
  uint8_t img[256];
  size_t len = compile(FIX_SRC_STOP, img, sizeof(img));
  /* Points into the wait: the walk reaches it without a Stop. */
  fix_set_pbi_length(img, FIX_PBI_WORDS - 2);
  fix_checksum(img);

  rcw_pbl_report_t r;
  sanity_log_t log;
  verify(img, len, &r, &log);
  assert_false(r.ok);
  assert_int_equal(find_entry(&log, "checksum")->status, SANITY_PASS);
  assert_int_equal(find_entry(&log, "terminator")->status, SANITY_FAIL);
  assert_non_null(strstr(find_entry(&log, "terminator")->detail, "RCW[PBI_LENGTH]=7"));
  assert_non_null(strstr(find_entry(&log, "terminator")->detail, "after 3 commands"));
  assert_null(find_entry(&log, "pbi_crc"));
}

static void
test_bad_word(void **state) {
  (void)state;
  uint8_t img[256];
  size_t len = compile(FIX_SRC_STOP, img, sizeof(img));
  /* CMD 0x30 is in no table: the walk stops at the wait's slot. */
  fix_put_le32(img + RCW_PBL_PBI_OFFSET + FIX_PBI_OFF_WAIT, 0x80300000u);

  rcw_pbl_report_t r;
  sanity_log_t log;
  verify(img, len, &r, &log);
  assert_false(r.ok);
  assert_int_equal(find_entry(&log, "terminator")->status, SANITY_FAIL);
  assert_non_null(strstr(find_entry(&log, "terminator")->detail, "0x80300000 at byte 164"));
}

static void
test_truncated(void **state) {
  (void)state;
  uint8_t img[256];
  size_t len = compile(FIX_SRC_STOP, img, sizeof(img));

  /* Cut inside the Stop. */
  rcw_pbl_report_t r;
  sanity_log_t log;
  verify(img, len - 4, &r, &log);
  assert_false(r.ok);
  assert_int_equal(r.image_end, len - 4);
  /* WARN, not FAIL - the rest may just not have been read. */
  assert_false(log.any_failed);
  assert_int_equal(find_entry(&log, "terminator")->status, SANITY_WARN);
  assert_non_null(strstr(find_entry(&log, "terminator")->detail, "runs past the 32 bytes available"));
}

static void
test_not_a_pbl(void **state) {
  (void)state;
  uint8_t img[256];
  size_t len = compile(FIX_SRC_STOP, img, sizeof(img));

  rcw_pbl_report_t r;
  sanity_log_t log;
  verify(img, RCW_PBL_RCW_BYTES, &r, &log);
  assert_false(r.ok);
  assert_int_equal(find_entry(&log, "preamble")->status, SANITY_FAIL);
  assert_non_null(strstr(find_entry(&log, "preamble")->detail, "too short"));
  assert_null(find_entry(&log, "load_cmd"));

  img[0] = 0xDE;
  verify(img, len, &r, &log);
  assert_int_equal(find_entry(&log, "preamble")->status, SANITY_FAIL);
  assert_non_null(strstr(find_entry(&log, "preamble")->detail, "got 0xaa55aade"));
  assert_null(find_entry(&log, "load_cmd"));

  img[0] = 0x55;
  img[6] = 0x12; /* 0x80120000 is no Load RCW */
  verify(img, len, &r, &log);
  assert_int_equal(find_entry(&log, "preamble")->status, SANITY_PASS);
  assert_int_equal(find_entry(&log, "load_cmd")->status, SANITY_FAIL);
  assert_non_null(strstr(find_entry(&log, "load_cmd")->detail, "got 0x80120000"));
  assert_null(find_entry(&log, "checksum"));
}

static void
test_no_pbi(void **state) {
  (void)state;
  uint8_t img[256];
  size_t len = compile(FIX_SRC_STOP, img, sizeof(img));
  fix_set_pbi_length(img, 0);
  fix_checksum(img);

  rcw_pbl_report_t r;
  sanity_log_t log;
  verify(img, len, &r, &log);
  assert_true(r.ok);
  assert_int_equal(r.image_end, RCW_PBL_PBI_OFFSET);
  assert_false(log.any_failed);
  assert_int_equal(find_entry(&log, "terminator")->status, SANITY_WARN);
  assert_null(find_entry(&log, "pbi_crc"));
}

static void
test_rcw_consistent(void **state) {
  (void)state;
  uint8_t img[256];
  size_t len = compile(FIX_SRC_STOP, img, sizeof(img));
  uint8_t live[RCW_PBL_RCW_BYTES];
  memcpy(live, img + RCW_PBL_RCW_OFFSET, sizeof(live));

  rcw_pbl_report_t r;
  rcw_pbl_verify(img, len, 1, NULL, NULL, &r);

  sanity_log_t log = {0};
  pbl_sanity_run(&r, len, live, &log);
  assert_int_equal(find_entry(&log, "rcw_consistent")->status, SANITY_PASS);

  live[0] ^= 0x42;
  memset(&log, 0, sizeof(log));
  pbl_sanity_run(&r, len, live, &log);
  assert_false(log.any_failed);
  assert_int_equal(find_entry(&log, "rcw_consistent")->status, SANITY_WARN);
}

struct listing {
  FILE *f;
  size_t n;
};

static void
list_visit(const rcw_pbi_cmd_t *cmd, size_t off, void *arg) {
  struct listing *l = arg;

  pbl_print_cmd(l->f, cmd, RCW_PBL_PBI_OFFSET + off);
  l->n++;
}

static void
test_print(void **state) {
  (void)state;
  uint8_t img[256];
  size_t len = compile(FIX_SRC_STOP, img, sizeof(img));
  char *buf = NULL;
  size_t buflen = 0;
  struct listing l = { .f = open_memstream(&buf, &buflen), .n = 0 };

  assert_non_null(l.f);
  rcw_pbl_report_t r;
  rcw_pbl_verify(img, len, 1, list_visit, &l, &r);
  fclose(l.f);
  assert_true(r.ok);
  assert_int_equal(l.n, FIX_NCMD);
  assert_string_equal(buf,
    "   140  write 0x01e00620,0x80000000\n"
    "   148  blockcopy 0x0f,0x20009000,0x1800d000,0x00001000\n"
    "   164  wait 0x00000064\n"
    "   168  /* Stop command */\n");
  free(buf);

  assert_string_equal(pbl_op_label(RCW_PBI_STOP), "Stop");
  assert_string_equal(pbl_op_label(RCW_PBI_CRC_STOP), "CRC and Stop");
  assert_string_equal(pbl_op_label(RCW_PBI_WAIT), "wait");
}

int
main(void) {
  const struct CMUnitTest tests[] = {
    cmocka_unit_test(test_stop_image),
    cmocka_unit_test(test_crcstop_image),
    cmocka_unit_test(test_without_checksum),
    cmocka_unit_test(test_checksum_mismatch),
    cmocka_unit_test(test_pbi_length_too_small),
    cmocka_unit_test(test_bad_word),
    cmocka_unit_test(test_truncated),
    cmocka_unit_test(test_not_a_pbl),
    cmocka_unit_test(test_no_pbi),
    cmocka_unit_test(test_rcw_consistent),
    cmocka_unit_test(test_print),
  };

  return cmocka_run_group_tests(tests, NULL, NULL);
}
