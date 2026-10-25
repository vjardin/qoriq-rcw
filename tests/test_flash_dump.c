/*
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright 2026 Free Mobile - Vincent Jardin
 *
 * Tests for flash_dump.c - slot reading, sanity battery, header.
 */

#define _POSIX_C_SOURCE 200809L

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <setjmp.h>
#include <string.h>
#include <unistd.h>
#include <cmocka.h>

#include "flash_db.h"
#include "flash_dump.h"
#include "pbl_fixture.h"

/* Compile src into out, 0xFF-padded to cap like flash is. Returns the image length. */
static size_t
make_pbl(uint8_t *out, size_t cap, const char *src) {
  memset(out, 0xFF, cap);
  size_t len = fix_compile(src, out, cap);
  assert_int_equal(len, FIX_IMG_BYTES);
  return len;
}

static char *
make_flash_fixture(const uint8_t *pbl_primary, size_t plen, const uint8_t *pbl_fallback, size_t flen) {
  /* 16 MiB image: primary @ 0, fallback @ 0x800000 */
  uint8_t *flash = malloc(16u * 1024u * 1024u);
  memset(flash, 0xFF, 16u * 1024u * 1024u);
  if (pbl_primary && plen)
    memcpy(flash, pbl_primary, plen);
  if (pbl_fallback && flen)
    memcpy(flash + 0x800000u, pbl_fallback, flen);

  char tmpl[] = "/tmp/flash-fix-XXXXXX";
  int fd = mkstemp(tmpl);
  assert_true(fd >= 0);
  ssize_t n = write(fd, flash, 16u * 1024u * 1024u);
  assert_int_equal(n, (ssize_t)(16u * 1024u * 1024u));
  close(fd);
  free(flash);

  return strdup(tmpl);
}

static void
test_read_slot_primary(void **state) {
  (void)state;
  uint8_t pbl[200];
  size_t pblen = make_pbl(pbl, sizeof(pbl), FIX_SRC_STOP);
  char *fix = make_flash_fixture(pbl, pblen, NULL, 0);

  flash_slot_t slot = {0};
  char err[256] = {0};
  int rc = flash_dump_read_slot(fix, 0, "primary", &slot, err, sizeof(err));
  assert_int_equal(rc, 0);
  assert_int_equal(slot.data_len, FLASH_SLOT_READ_BYTES);
  assert_memory_equal(slot.data, pbl, pblen);
  /* Bytes after the PBL come from the 0xFF padding. */
  for (size_t i = pblen; i < pblen + 8; i++)
    assert_int_equal(slot.data[i], 0xFF);

  /* The sanity run trims the slot to the image. */
  sanity_log_t log;
  flash_dump_sanity_run(&slot, NULL, &log);
  assert_false(log.any_failed);
  assert_int_equal(slot.pbl_len, pblen);

  flash_slot_free(&slot);
  unlink(fix);
  free(fix);
}

static void
test_read_slot_fallback(void **state) {
  (void)state;
  uint8_t pbl[200];
  size_t pblen = make_pbl(pbl, sizeof(pbl), FIX_SRC_STOP);
  char *fix = make_flash_fixture(NULL, 0, pbl, pblen);

  flash_slot_t slot = {0};
  char err[256] = {0};
  int rc = flash_dump_read_slot(fix, 0x800000, "fallback", &slot, err, sizeof(err));
  assert_int_equal(rc, 0);
  assert_memory_equal(slot.data, pbl, pblen);

  flash_slot_free(&slot);
  unlink(fix);
  free(fix);
}

static void
test_read_slot_open_fails(void **state) {
  (void)state;
  flash_slot_t slot = {0};
  char err[256] = {0};
  int rc = flash_dump_read_slot("/nonexistent/path/qzzx", 0, "primary", &slot, err, sizeof(err));
  assert_int_not_equal(rc, 0);
  assert_non_null(strstr(err, "open"));
}

static const sanity_entry_t *
find_entry(const sanity_log_t *log, const char *id) {
  for (size_t i = 0; i < log->count; i++)
    if (strcmp(log->entries[i].id, id) == 0)
      return &log->entries[i];

  return NULL;
}

static flash_slot_t
slot_of(uint8_t *data, size_t len) {
  flash_slot_t slot = {
    .name = "primary",
    .offset = 0,
    .data = data,
    .data_len = len,
    .pbl_len = len,
  };

  return slot;
}

static void
test_sanity_all_pass(void **state) {
  (void)state;
  uint8_t pbl[200];
  size_t pblen = make_pbl(pbl, sizeof(pbl), FIX_SRC_STOP);
  flash_slot_t slot = slot_of(pbl, sizeof(pbl));
  const uint8_t *live_rcw = pbl + RCW_PBL_RCW_OFFSET; /* matches the in-PBL RCW */

  sanity_log_t log;
  flash_dump_sanity_run(&slot, live_rcw, &log);

  assert_false(log.any_failed);
  assert_int_equal(find_entry(&log, "preamble")->status, SANITY_PASS);
  assert_int_equal(find_entry(&log, "load_cmd")->status, SANITY_PASS);
  assert_int_equal(find_entry(&log, "checksum")->status, SANITY_PASS);
  assert_int_equal(find_entry(&log, "terminator")->status, SANITY_PASS);
  assert_non_null(strstr(find_entry(&log, "terminator")->detail, "Stop at byte 168"));
  assert_int_equal(find_entry(&log, "pbi_crc")->status, SANITY_PASS);
  assert_int_equal(find_entry(&log, "rcw_consistent")->status, SANITY_PASS);
  assert_int_equal(find_entry(&log, "pbi_commands")->status, SANITY_PASS);
  /* Slot was trimmed to the image (preamble..terminator value), padding dropped. */
  assert_int_equal(slot.pbl_len, pblen);
  assert_int_equal(slot.pbi_length, FIX_PBI_WORDS);
  assert_int_equal(slot.ncmd, FIX_NCMD);
  assert_false(slot.sb_en);
}

static void
test_sanity_crcstop_pass(void **state) {
  (void)state;
  uint8_t pbl[200];
  size_t pblen = make_pbl(pbl, sizeof(pbl), FIX_SRC_CRCSTOP);
  flash_slot_t slot = slot_of(pbl, pblen);

  sanity_log_t log;
  flash_dump_sanity_run(&slot, NULL, &log);
  assert_false(log.any_failed);
  assert_non_null(strstr(find_entry(&log, "terminator")->detail, "CRC and Stop"));
  assert_int_equal(find_entry(&log, "pbi_crc")->status, SANITY_PASS);
  assert_non_null(strstr(find_entry(&log, "pbi_crc")->detail, "matches"));
  assert_null(find_entry(&log, "rcw_consistent"));
}

static void
test_sanity_without_checksum(void **state) {
  (void)state;
  uint8_t pbl[200];
  size_t pblen = make_pbl(pbl, sizeof(pbl), FIX_SRC_NOCS);
  flash_slot_t slot = slot_of(pbl, pblen);

  sanity_log_t log;
  flash_dump_sanity_run(&slot, NULL, &log);
  assert_false(log.any_failed);
  assert_non_null(strstr(find_entry(&log, "load_cmd")->detail, "without checksum"));
  assert_non_null(strstr(find_entry(&log, "checksum")->detail, "skipped"));
}

static void
test_sanity_bad_preamble(void **state) {
  (void)state;
  uint8_t pbl[200];
  size_t pblen = make_pbl(pbl, sizeof(pbl), FIX_SRC_STOP);
  pbl[0] = 0xDE;  /* wreck preamble */
  flash_slot_t slot = slot_of(pbl, pblen);

  sanity_log_t log;
  flash_dump_sanity_run(&slot, NULL, &log);
  assert_true(log.any_failed);
  assert_int_equal(find_entry(&log, "preamble")->status, SANITY_FAIL);
  assert_null(find_entry(&log, "load_cmd"));
  assert_int_equal(slot.pbl_len, pblen);
}

static void
test_sanity_bad_load_cmd(void **state) {
  (void)state;
  uint8_t pbl[200];
  size_t pblen = make_pbl(pbl, sizeof(pbl), FIX_SRC_STOP);
  pbl[6] = 0x12;  /* 0x80120000 is no Load RCW */
  flash_slot_t slot = slot_of(pbl, pblen);

  sanity_log_t log;
  flash_dump_sanity_run(&slot, NULL, &log);
  assert_true(log.any_failed);
  assert_int_equal(find_entry(&log, "preamble")->status, SANITY_PASS);
  assert_int_equal(find_entry(&log, "load_cmd")->status, SANITY_FAIL);
}

static void
test_sanity_too_short(void **state) {
  (void)state;
  uint8_t pbl[200];
  make_pbl(pbl, sizeof(pbl), FIX_SRC_STOP);
  flash_slot_t slot = slot_of(pbl, RCW_PBL_PBI_OFFSET - 1);

  sanity_log_t log;
  flash_dump_sanity_run(&slot, NULL, &log);
  assert_true(log.any_failed);
  assert_int_equal(find_entry(&log, "preamble")->status, SANITY_FAIL);
  assert_non_null(strstr(find_entry(&log, "preamble")->detail, "too short"));
}

static void
test_sanity_bad_checksum(void **state) {
  (void)state;
  uint8_t pbl[200];
  size_t pblen = make_pbl(pbl, sizeof(pbl), FIX_SRC_STOP);
  pbl[RCW_PBL_CHECKSUM_OFFSET] ^= 0xFF;  /* corrupt one checksum byte */
  flash_slot_t slot = slot_of(pbl, pblen);

  sanity_log_t log;
  flash_dump_sanity_run(&slot, NULL, &log);
  assert_true(log.any_failed);
  assert_int_equal(find_entry(&log, "checksum")->status, SANITY_FAIL);
  /* The PBI is still fine. */
  assert_int_equal(find_entry(&log, "terminator")->status, SANITY_PASS);
}

static void
test_sanity_crc_mismatch(void **state) {
  (void)state;
  uint8_t pbl[200];
  size_t pblen = make_pbl(pbl, sizeof(pbl), FIX_SRC_CRCSTOP);
  pbl[RCW_PBL_PBI_OFFSET + FIX_PBI_OFF_WRITE + 4] ^= 0x01;  /* the write's value */
  flash_slot_t slot = slot_of(pbl, pblen);

  sanity_log_t log;
  flash_dump_sanity_run(&slot, NULL, &log);
  assert_true(log.any_failed);
  assert_int_equal(find_entry(&log, "terminator")->status, SANITY_PASS);
  assert_int_equal(find_entry(&log, "pbi_crc")->status, SANITY_FAIL);
  assert_int_equal(slot.pbl_len, pblen);
}

static void
test_sanity_bad_pbi_length(void **state) {
  (void)state;
  uint8_t pbl[200];
  size_t pblen = make_pbl(pbl, sizeof(pbl), FIX_SRC_STOP);
  fix_set_pbi_length(pbl, FIX_PBI_WORDS - 2);  /* points into the wait */
  fix_checksum(pbl);
  flash_slot_t slot = slot_of(pbl, pblen);

  sanity_log_t log;
  flash_dump_sanity_run(&slot, NULL, &log);
  assert_true(log.any_failed);
  assert_int_equal(find_entry(&log, "checksum")->status, SANITY_PASS);
  assert_int_equal(find_entry(&log, "terminator")->status, SANITY_FAIL);
  assert_non_null(strstr(find_entry(&log, "terminator")->detail, "RCW[PBI_LENGTH]=7"));
  assert_null(find_entry(&log, "pbi_crc"));
}

static void
test_sanity_truncated(void **state) {
  (void)state;
  uint8_t pbl[200];
  size_t pblen = make_pbl(pbl, sizeof(pbl), FIX_SRC_STOP);
  /* The read window ends inside the PBI. */
  flash_slot_t slot = slot_of(pbl, pblen - 4);

  sanity_log_t log;
  flash_dump_sanity_run(&slot, NULL, &log);
  /* WARN, not FAIL - the flash may hold the rest. */
  assert_false(log.any_failed);
  assert_int_equal(find_entry(&log, "terminator")->status, SANITY_WARN);
  assert_int_equal(slot.pbl_len, pblen - 4);
}

static void
test_sanity_no_pbi(void **state) {
  (void)state;
  uint8_t pbl[200];
  size_t pblen = make_pbl(pbl, sizeof(pbl), FIX_SRC_STOP);
  fix_set_pbi_length(pbl, 0);
  fix_checksum(pbl);
  flash_slot_t slot = slot_of(pbl, pblen);

  sanity_log_t log;
  flash_dump_sanity_run(&slot, NULL, &log);
  assert_false(log.any_failed);
  assert_int_equal(find_entry(&log, "terminator")->status, SANITY_WARN);
  assert_null(find_entry(&log, "pbi_crc"));
  assert_int_equal(slot.pbl_len, RCW_PBL_PBI_OFFSET);
  assert_int_equal(slot.ncmd, 0);
}

static void
test_sanity_rcw_inconsistent(void **state) {
  (void)state;
  uint8_t pbl[200];
  size_t pblen = make_pbl(pbl, sizeof(pbl), FIX_SRC_STOP);
  flash_slot_t slot = slot_of(pbl, pblen);
  uint8_t live_rcw[RCW_PBL_RCW_BYTES];
  memcpy(live_rcw, pbl + RCW_PBL_RCW_OFFSET, sizeof(live_rcw));
  live_rcw[0] ^= 0x42;  /* differs from the in-PBL RCW */

  sanity_log_t log;
  flash_dump_sanity_run(&slot, live_rcw, &log);
  assert_false(log.any_failed);
  assert_int_equal(find_entry(&log, "rcw_consistent")->status, SANITY_WARN);
}

static void
test_compare_slots_match(void **state) {
  (void)state;
  uint8_t pbl[200];
  size_t pblen = make_pbl(pbl, sizeof(pbl), FIX_SRC_STOP);
  flash_slot_t a = { .name="primary",  .data=pbl, .data_len=pblen, .pbl_len=pblen };
  flash_slot_t b = { .name="fallback", .data=pbl, .data_len=pblen, .pbl_len=pblen };

  sanity_log_t log = {0};
  flash_dump_compare_slots(&a, &b, &log);
  assert_int_equal(find_entry(&log, "fallback_match")->status, SANITY_PASS);
}

static void
test_compare_slots_differ(void **state) {
  (void)state;
  uint8_t pbl_a[200], pbl_b[200];
  size_t pa = make_pbl(pbl_a, sizeof(pbl_a), FIX_SRC_STOP);
  size_t pb = make_pbl(pbl_b, sizeof(pbl_b), FIX_SRC_STOP);
  pbl_b[10] ^= 0xFF;
  flash_slot_t a = { .name="primary",  .data=pbl_a, .data_len=pa, .pbl_len=pa };
  flash_slot_t b = { .name="fallback", .data=pbl_b, .data_len=pb, .pbl_len=pb };

  sanity_log_t log = {0};
  flash_dump_compare_slots(&a, &b, &log);
  assert_int_equal(find_entry(&log, "fallback_match")->status, SANITY_WARN);

  /* Different sizes: the padding after the shorter image is not compared. */
  flash_slot_t c = { .name="fallback", .data=pbl_a, .data_len=pa, .pbl_len=pa - 4 };
  sanity_log_t log2 = {0};
  flash_dump_compare_slots(&a, &c, &log2);
  assert_int_equal(find_entry(&log2, "fallback_match")->status, SANITY_WARN);
  assert_non_null(strstr(find_entry(&log2, "fallback_match")->detail, "size"));
}

int
main(void) {
  const struct CMUnitTest tests[] = {
    cmocka_unit_test(test_read_slot_primary),
    cmocka_unit_test(test_read_slot_fallback),
    cmocka_unit_test(test_read_slot_open_fails),
    cmocka_unit_test(test_sanity_all_pass),
    cmocka_unit_test(test_sanity_crcstop_pass),
    cmocka_unit_test(test_sanity_without_checksum),
    cmocka_unit_test(test_sanity_bad_preamble),
    cmocka_unit_test(test_sanity_bad_load_cmd),
    cmocka_unit_test(test_sanity_too_short),
    cmocka_unit_test(test_sanity_bad_checksum),
    cmocka_unit_test(test_sanity_crc_mismatch),
    cmocka_unit_test(test_sanity_bad_pbi_length),
    cmocka_unit_test(test_sanity_truncated),
    cmocka_unit_test(test_sanity_no_pbi),
    cmocka_unit_test(test_sanity_rcw_inconsistent),
    cmocka_unit_test(test_compare_slots_match),
    cmocka_unit_test(test_compare_slots_differ),
  };

  return cmocka_run_group_tests(tests, NULL, NULL);
}
