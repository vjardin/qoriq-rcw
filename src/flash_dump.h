/*
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright 2026 Free Mobile - Vincent Jardin
 *
 * Read RCW slots from the boot medium (FlexSPI / SD / eMMC / I2C
 * EEPROM) and sanity-check the on-flash PBL binary.
 *
 * Differs from runtime_dump (which reads the live RCWSR registers via
 * /dev/mem) by recovering BOTH the RCW and the PBI commands - PBI
 * runs once at boot and is not present in the runtime registers.
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "flash_db.h"
#include "runtime_dump.h"

/* Fixed read window per slot. Covers preamble + 128B RCW + checksum +
 * a generous PBI block + terminator. Increase if a real-world board
 * ever exceeds it. */
#define FLASH_SLOT_READ_BYTES (16u * 1024u)

typedef struct {
  const char *name;        /* "primary" | "fallback" */
  uint64_t    offset;      /* offset within the boot device */
  uint8_t    *data;        /* malloc()'d; caller frees */
  size_t      data_len;    /* bytes read from device (< FLASH_SLOT_READ_BYTES) */
  size_t      pbl_len;     /* preamble..terminator-value extent within data; populated by flash_dump_sanity_run().
                            * Use this length (not data_len) when feeding the slot to rcw_decompile_buffer() - it strips the
                            * 0xFF flash padding that follows the terminator.
			    */
} flash_slot_t;

void flash_slot_free(flash_slot_t *slot);

/*
 * Open `device`, lseek to `offset`, read up to FLASH_SLOT_READ_BYTES
 * bytes into a freshly-allocated buffer in `out`. Works equally well on
 * /dev/mtd*, /dev/mmcblk*, and a regular file (for tests).
 *
 * Returns 0 on success, -errno on failure (with `err` populated).
 */
int flash_dump_read_slot(const char *device, uint64_t offset, const char *slot_name, flash_slot_t *out, char *err, size_t errlen);

/*
 * Run the sanity battery on a slot's bytes:
 *
 *   preamble        : first 4 bytes are 0xAA55AA55 (little-endian)
 *   load_cmd        : bytes 4..7 are 0x80100000 (with-checksum) or 0x80110000 (without)
 *   checksum        : when with-checksum, recomputes the 32-bit sum across preamble..RCW and compares
 *   terminator      : finds 0x808F0000 (CRC+Stop) or 0x80FF0000 (Stop) within the buffer
 *   rcw_consistent  : the on-flash 128-byte RCW matches `live_rcw` (if non-NULL); pass NULL to skip
 *   pbi_recovered   : informational PASS - N bytes of PBI commands recovered (vs --dump which loses them)
 */
void flash_dump_sanity_run(const flash_slot_t *slot,
                           const uint8_t *live_rcw,   /* may be NULL */
                           sanity_log_t *log);

/*
 * Optional cross-slot check: append a fallback_match entry comparing
 * `primary` and `fallback` slot bytes (preamble through terminator).
 * Either operand may be NULL/empty (recorded as WARN).
 */
void flash_dump_compare_slots(const flash_slot_t *primary, const flash_slot_t *fallback, sanity_log_t *log);

/*
 * Write the C-comment header describing a flash slot dump (SoC, boot
 * source/device, slot offset/size, sanity log, capture time, version).
 */
int flash_dump_write_header(FILE *out,
                            const soc_info_t *soc,
                            uint32_t svr,
                            uint32_t porsr1,
                            uint32_t rcw_src,
                            const flash_info_t *flash,
                            const char *device,
                            const flash_slot_t *slot,
                            const sanity_log_t *log,
                            const char *tool_version);
