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

/* Minimal valid PBL bytes: preamble + Load RCW + 128 zero RCW + checksum
 * + Stop terminator + 0 trailer. Caller can patch checksum value.
 */
static size_t
make_min_pbl(uint8_t *out, bool with_checksum) {
  /* Preamble */
  out[0] = 0x55; out[1] = 0xAA; out[2] = 0x55; out[3] = 0xAA;

  /* Load RCW command */
  uint32_t load = with_checksum ? 0x80100000u : 0x80110000u;
  out[4] = (uint8_t) load;
  out[5] = (uint8_t)(load >> 8);
  out[6] = (uint8_t)(load >> 16);
  out[7] = (uint8_t)(load >> 24);

  /* 128 bytes RCW = zeros */
  memset(out + 8, 0, 128);
  size_t off = 8 + 128;
  if (with_checksum) {
    /* Checksum = sum of (8+128) bytes / 4 words = preamble + load (RCW=0)
     * = 0xAA55AA55 + 0x80100000 = 0x2A65AA55
     */
    uint32_t sum = 0xAA55AA55u + 0x80100000u;
    out[off+0] = (uint8_t) sum;
    out[off+1] = (uint8_t)(sum >> 8);
    out[off+2] = (uint8_t)(sum >> 16);
    out[off+3] = (uint8_t)(sum >> 24);
    off += 4;
  }

  /* Stop terminator */
  out[off+0] = 0x00; out[off+1] = 0x00; out[off+2] = 0xFF; out[off+3] = 0x80;
  /* Stop trailer = 0 */
  out[off+4] = 0; out[off+5] = 0; out[off+6] = 0; out[off+7] = 0;

  return off + 8;
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
  size_t pblen = make_min_pbl(pbl, true);
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

  flash_slot_free(&slot);
  unlink(fix);
  free(fix);
}

static void
test_read_slot_fallback(void **state) {
  (void)state;
  uint8_t pbl[200];
  size_t pblen = make_min_pbl(pbl, true);
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

static void
test_sanity_all_pass(void **state) {
  (void)state;
  uint8_t pbl[200];
  size_t pblen = make_min_pbl(pbl, true);
  flash_slot_t slot = {
    .name = "primary",
    .offset = 0,
    .data = pbl,
    .data_len = pblen,
    .pbl_len = pblen
  };
  uint8_t live_rcw[128] = {0};  /* matches the in-PBL zero RCW */

  sanity_log_t log;
  flash_dump_sanity_run(&slot, live_rcw, &log);

  assert_false(log.any_failed);
  assert_int_equal(find_entry(&log, "preamble")->status, SANITY_PASS);
  assert_int_equal(find_entry(&log, "load_cmd")->status, SANITY_PASS);
  assert_int_equal(find_entry(&log, "checksum")->status, SANITY_PASS);
  assert_int_equal(find_entry(&log, "terminator")->status, SANITY_PASS);
  assert_int_equal(find_entry(&log, "rcw_consistent")->status, SANITY_PASS);
  /* Slot was trimmed to PBL extent (preamble..terminator-trailer). */
  assert_true(slot.pbl_len <= pblen);
}

static void
test_sanity_bad_preamble(void **state) {
  (void)state;
  uint8_t pbl[200];
  size_t pblen = make_min_pbl(pbl, true);
  pbl[0] = 0xDE;  /* wreck preamble */
  flash_slot_t slot = {
    .name="primary",
    .offset=0,
    .data=pbl,
    .data_len=pblen,
    .pbl_len=pblen
  };

  sanity_log_t log;
  flash_dump_sanity_run(&slot, NULL, &log);
  assert_true(log.any_failed);
  assert_int_equal(find_entry(&log, "preamble")->status, SANITY_FAIL);
}

static void
test_sanity_bad_checksum(void **state) {
  (void)state;
  uint8_t pbl[200];
  size_t pblen = make_min_pbl(pbl, true);
  pbl[8 + 128] ^= 0xFF;  /* corrupt one checksum byte */
  flash_slot_t slot = {
    .name="primary",
    .offset=0,
    .data=pbl,
    .data_len=pblen,
    .pbl_len=pblen
  };

  sanity_log_t log;
  flash_dump_sanity_run(&slot, NULL, &log);
  assert_true(log.any_failed);
  assert_int_equal(find_entry(&log, "checksum")->status, SANITY_FAIL);
}

static void
test_sanity_no_terminator(void **state) {
  (void)state;
  uint8_t pbl[200];
  size_t pblen = make_min_pbl(pbl, true);
  /* Erase the Stop terminator. */
  memset(pbl + 8 + 128 + 4, 0xAA, 8);
  flash_slot_t slot = {
    .name="primary",
    .offset=0,
    .data=pbl,
    .data_len=pblen,
    .pbl_len=pblen
  };

  sanity_log_t log;
  flash_dump_sanity_run(&slot, NULL, &log);
  /* WARN, not FAIL - buffer might just be truncated. */
  assert_int_equal(find_entry(&log, "terminator")->status, SANITY_WARN);
}

static void
test_sanity_rcw_inconsistent(void **state) {
  (void)state;
  uint8_t pbl[200];
  size_t pblen = make_min_pbl(pbl, true);
  flash_slot_t slot = {
    .name="primary",
    .offset=0,
    .data=pbl,
    .data_len=pblen,
    .pbl_len=pblen
  };
  uint8_t live_rcw[128] = {0};
  live_rcw[0] = 0x42;  /* differs from in-PBL zeros */

  sanity_log_t log;
  flash_dump_sanity_run(&slot, live_rcw, &log);
  assert_int_equal(find_entry(&log, "rcw_consistent")->status, SANITY_WARN);
}

static void
test_compare_slots_match(void **state) {
  (void)state;
  uint8_t pbl[200];
  size_t pblen = make_min_pbl(pbl, true);
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
  size_t pa = make_min_pbl(pbl_a, true);
  size_t pb = make_min_pbl(pbl_b, true);
  pbl_b[10] ^= 0xFF;
  flash_slot_t a = { .name="primary",  .data=pbl_a, .data_len=pa, .pbl_len=pa };
  flash_slot_t b = { .name="fallback", .data=pbl_b, .data_len=pb, .pbl_len=pb };

  sanity_log_t log = {0};
  flash_dump_compare_slots(&a, &b, &log);
  assert_int_equal(find_entry(&log, "fallback_match")->status, SANITY_WARN);
}

int
main(void) {
  const struct CMUnitTest tests[] = {
    cmocka_unit_test(test_read_slot_primary),
    cmocka_unit_test(test_read_slot_fallback),
    cmocka_unit_test(test_read_slot_open_fails),
    cmocka_unit_test(test_sanity_all_pass),
    cmocka_unit_test(test_sanity_bad_preamble),
    cmocka_unit_test(test_sanity_bad_checksum),
    cmocka_unit_test(test_sanity_no_terminator),
    cmocka_unit_test(test_sanity_rcw_inconsistent),
    cmocka_unit_test(test_compare_slots_match),
    cmocka_unit_test(test_compare_slots_differ),
  };

  return cmocka_run_group_tests(tests, NULL, NULL);
}
