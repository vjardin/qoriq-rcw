/*
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright 2026 Free Mobile - Vincent Jardin
 */

#pragma once

#include <stdint.h>
#include <string.h>

#include <qoriq-rcw/rcw.h>

/*
 * The envelope, then write (2 words), blockcopy (4), wait (1) and the
 * terminator (2): RCW[PBI_LENGTH] = 9
 */
#define FIX_HDR                            \
  "%size=1024\n"                           \
  "%pbiformat=2\n"                         \
  "%classicbitnumbers=1\n"                 \
  "%littleendian=1\n"                      \
  "SYS_PLL_RAT[6:2]\n"                     \
  "SB_EN[266]\n"                           \
  "PBI_LENGTH[287:276]\n"                  \
  "SYS_PLL_RAT=7\n"

#define FIX_PBI                                     \
  ".pbi\n"                                          \
  "write 0x01e00620,0x80000000\n"                   \
  "blockcopy 0x0f,0x20009000,0x1800d000,0x1000\n"   \
  "wait 0x64\n"                                     \
  ".end\n"

#define FIX_SRC_STOP     FIX_HDR "%nocrc=1\n" FIX_PBI
#define FIX_SRC_CRCSTOP  FIX_HDR FIX_PBI
#define FIX_SRC_NOCS     FIX_HDR "%nocrc=1\n" "%loadwochecksum=1\n" FIX_PBI

#define FIX_PBI_WORDS   9
#define FIX_PBI_BYTES   (4 * FIX_PBI_WORDS)
#define FIX_NCMD        4 /* write, blockcopy, wait, the terminator */
#define FIX_IMG_BYTES   (RCW_PBL_PBI_OFFSET + FIX_PBI_BYTES)
#define FIX_PBI_OFF_WRITE      0
#define FIX_PBI_OFF_BLOCKCOPY  8
#define FIX_PBI_OFF_WAIT       24
#define FIX_PBI_OFF_STOP       28

/* RCW[PBI_LENGTH] is bits 287:276: the top 12 bits of RCW word 8. */
#define FIX_PBI_LENGTH_AT (RCW_PBL_RCW_OFFSET + 8 * 4)

static inline uint32_t
fix_get_le32(const uint8_t *p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static inline void
fix_put_le32(uint8_t *p, uint32_t w) {
  p[0] = (uint8_t)w;
  p[1] = (uint8_t)(w >> 8);
  p[2] = (uint8_t)(w >> 16);
  p[3] = (uint8_t)(w >> 24);
}

/* Compile src into out (cap bytes). Returns the image length, 0 on failure. */
static inline size_t
fix_compile(const char *src, uint8_t *out, size_t cap) {
  rcw_ctx_t *ctx = rcw_ctx_new();
  if (!ctx)
    return 0;

  uint8_t *bin = NULL;
  size_t len = 0;
  rcw_error_t err = rcw_compile_buffer(ctx, src, strlen(src), &bin, &len);
  if (err == RCW_OK && len <= cap)
    memcpy(out, bin, len);
  else
    len = 0;
  rcw_free(bin);
  rcw_ctx_free(ctx);

  return len;
}

static inline void
fix_set_pbi_length(uint8_t *img, uint32_t words) {
  uint32_t w = fix_get_le32(img + FIX_PBI_LENGTH_AT);

  fix_put_le32(img + FIX_PBI_LENGTH_AT, (w & 0x000FFFFFu) | (words << 20));
}

/* Recompute the Load RCW checksum after patching the RCW. */
static inline void
fix_checksum(uint8_t *img) {
  uint32_t sum = 0;

  for (size_t off = 0; off < RCW_PBL_CHECKSUM_OFFSET; off += 4)
    sum += fix_get_le32(img + off);
  fix_put_le32(img + RCW_PBL_CHECKSUM_OFFSET, sum);
}
