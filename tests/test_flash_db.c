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
#include <unistd.h>
#include <cmocka.h>

#include "flash_db.h"

static void
test_lookup_by_rcw_src(void **state) {
  (void)state;
  assert_string_equal(flash_db_find_by_rcw_src(0xF)->interface, "flexspi");
  assert_string_equal(flash_db_find_by_rcw_src(0xC)->interface, "flexspi");
  assert_string_equal(flash_db_find_by_rcw_src(0xD)->interface, "flexspi");
  assert_string_equal(flash_db_find_by_rcw_src(0x8)->interface, "sd");
  assert_string_equal(flash_db_find_by_rcw_src(0x9)->interface, "emmc");
  assert_string_equal(flash_db_find_by_rcw_src(0xA)->interface, "i2c");
  assert_null(flash_db_find_by_rcw_src(0x0));
  assert_null(flash_db_find_by_rcw_src(0xE));
}

static void
test_porsr1_extract(void **state) {
  (void)state;
  /* RCW_SRC field at bits[28:23], low 4 bits carry the source code. */
  assert_int_equal(flash_db_porsr1_to_rcw_src(0x07800000), 0xF);
  assert_int_equal(flash_db_porsr1_to_rcw_src(0x04000000), 0x8);
  assert_int_equal(flash_db_porsr1_to_rcw_src(0x05000000), 0xA);

  /* Garbage in upper bits is ignored. */
  assert_int_equal(flash_db_porsr1_to_rcw_src(0xFFFFFFFF), 0xF);
  assert_int_equal(flash_db_porsr1_to_rcw_src(0x00000000), 0x0);
}

static void
test_offsets_per_interface(void **state) {
  (void)state;
  /* FlexSPI: primary @ 0, fallback @ 8 MiB */
  assert_int_equal(flash_db_find_by_rcw_src(0xF)->primary_offset, 0x0);
  assert_int_equal(flash_db_find_by_rcw_src(0xF)->fallback_offset, 0x800000);

  /* SD/eMMC: primary @ 0x1000, fallback @ 0x801000 */
  assert_int_equal(flash_db_find_by_rcw_src(0x8)->primary_offset, 0x1000);
  assert_int_equal(flash_db_find_by_rcw_src(0x8)->fallback_offset, 0x801000);
  assert_int_equal(flash_db_find_by_rcw_src(0x9)->primary_offset, 0x1000);
  assert_int_equal(flash_db_find_by_rcw_src(0x9)->fallback_offset, 0x801000);

  /* I2C: no fallback, device required */
  assert_int_equal(flash_db_find_by_rcw_src(0xA)->fallback_offset, 0);
  assert_true(flash_db_find_by_rcw_src(0xA)->device_required);
  assert_null(flash_db_find_by_rcw_src(0xA)->default_device);
}

static void
test_resolve_device_emmc_default(void **state) {
  (void)state;
  char err[256] = {0};
  char *dev = flash_db_resolve_device(flash_db_find_by_rcw_src(0x9), err, sizeof(err));
  assert_non_null(dev);
  assert_string_equal(dev, "/dev/mmcblk0boot0");
  free(dev);
}

static void
test_resolve_device_i2c_errors(void **state) {
  (void)state;
  char err[256] = {0};
  char *dev = flash_db_resolve_device(flash_db_find_by_rcw_src(0xA), err, sizeof(err));
  assert_null(dev);
  assert_non_null(strstr(err, "requires --device"));
}

/* Write a /proc/mtd-style fixture and point QORIQ_RCW_PROC_MTD at it. */
static char *
make_proc_mtd_fixture(const char *body) {
  char tmpl[] = "/tmp/proc-mtd-XXXXXX";
  int fd = mkstemp(tmpl);
  assert_true(fd >= 0);
  ssize_t n = write(fd, body, strlen(body));
  assert_true(n > 0);
  close(fd);

  return strdup(tmpl);
}

static void
test_resolve_device_flexspi_picks_named_partition(void **state) {
  (void)state;
  /* Two MTD entries - "rcw" should win over "userdata" via priority. */
  char *fix = make_proc_mtd_fixture(
    "dev:    size   erasesize  name\n"
    "mtd0: 02000000 00040000 \"userdata\"\n"
    "mtd1: 00100000 00040000 \"rcw\"\n");
  setenv("QORIQ_RCW_PROC_MTD", fix, 1);

  char err[256] = {0};
  char *dev = flash_db_resolve_device(flash_db_find_by_rcw_src(0xF), err, sizeof(err));
  assert_non_null(dev);
  assert_string_equal(dev, "/dev/mtd1");
  free(dev);

  unsetenv("QORIQ_RCW_PROC_MTD");
  unlink(fix);
  free(fix);
}

static void
test_resolve_device_flexspi_priority_order(void **state) {
  (void)state;
  /* "qspi" appears in mtd0 but "bl2" should win because it ranks higher. */
  char *fix = make_proc_mtd_fixture(
    "dev:    size   erasesize  name\n"
    "mtd0: 02000000 00040000 \"qspi-flash\"\n"
    "mtd1: 00100000 00040000 \"bl2-image\"\n");
  setenv("QORIQ_RCW_PROC_MTD", fix, 1);

  char err[256] = {0};
  char *dev = flash_db_resolve_device(flash_db_find_by_rcw_src(0xF), err, sizeof(err));
  assert_non_null(dev);
  assert_string_equal(dev, "/dev/mtd1");
  free(dev);

  unsetenv("QORIQ_RCW_PROC_MTD");
  unlink(fix);
  free(fix);
}

static void
test_resolve_device_flexspi_falls_back_to_first_mtd(void **state) {
  (void)state;
  /* No name matches any pattern -> first MTD entry wins. */
  char *fix = make_proc_mtd_fixture(
    "dev:    size   erasesize  name\n"
    "mtd0: 02000000 00040000 \"system-a\"\n"
    "mtd1: 00100000 00040000 \"system-b\"\n");
  setenv("QORIQ_RCW_PROC_MTD", fix, 1);

  char err[256] = {0};
  char *dev = flash_db_resolve_device(flash_db_find_by_rcw_src(0xF), err, sizeof(err));
  assert_non_null(dev);
  assert_string_equal(dev, "/dev/mtd0");
  free(dev);

  unsetenv("QORIQ_RCW_PROC_MTD");
  unlink(fix);
  free(fix);
}

static void
test_resolve_device_flexspi_no_proc_mtd_falls_back_to_default(void **state) {
  (void)state;
  setenv("QORIQ_RCW_PROC_MTD", "/nonexistent/path/qzzx", 1);

  char err[256] = {0};
  char *dev = flash_db_resolve_device(flash_db_find_by_rcw_src(0xF), err, sizeof(err));
  assert_non_null(dev);
  assert_string_equal(dev, "/dev/mtd0");
  free(dev);

  unsetenv("QORIQ_RCW_PROC_MTD");
}

int
main(void) {
  const struct CMUnitTest tests[] = {
    cmocka_unit_test(test_lookup_by_rcw_src),
    cmocka_unit_test(test_porsr1_extract),
    cmocka_unit_test(test_offsets_per_interface),
    cmocka_unit_test(test_resolve_device_emmc_default),
    cmocka_unit_test(test_resolve_device_i2c_errors),
    cmocka_unit_test(test_resolve_device_flexspi_picks_named_partition),
    cmocka_unit_test(test_resolve_device_flexspi_priority_order),
    cmocka_unit_test(test_resolve_device_flexspi_falls_back_to_first_mtd),
    cmocka_unit_test(test_resolve_device_flexspi_no_proc_mtd_falls_back_to_default),
  };

  return cmocka_run_group_tests(tests, NULL, NULL);
}
