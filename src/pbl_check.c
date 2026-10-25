/*
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright 2026 Free Mobile - Vincent Jardin
 */

#define _POSIX_C_SOURCE 200809L

#include <stdbool.h>
#include <string.h>

#include "pbl_check.h"

void
pbl_sanity_run(const rcw_pbl_report_t *rep, size_t len, const uint8_t *live_rcw, sanity_log_t *log) {
  /* preamble, load_cmd */
  if (rep->envelope == RCW_ERR_TRUNCATED) {
    sanity_log_add(log, SANITY_FAIL, "preamble", "image too short (%zu bytes) - a PBL image is at least %u", len, RCW_PBL_PBI_OFFSET);
    return;
  }
  if (!rep->preamble_ok) {
    sanity_log_add(log, SANITY_FAIL, "preamble", "expected 0x%08x at offset 0, got 0x%08x", RCW_PBL_PREAMBLE, rep->word0);
    return;
  }
  sanity_log_add(log, SANITY_PASS, "preamble", "0x%08x at offset 0", rep->word0);
  if (!rep->load_rcw_ok || rep->envelope != RCW_OK) {
    sanity_log_add(log, SANITY_FAIL, "load_cmd", "expected 0x%08x or 0x%08x at offset 4, got 0x%08x",
                   RCW_PBL_CMD_LOAD_RCW, RCW_PBL_CMD_LOAD_RCW_NOCS, rep->word1);
    return;
  }

  const rcw_pbl_t *pbl = &rep->pbl;
  const rcw_pbi_check_t *chk = &rep->chk;
  sanity_log_add(log, SANITY_PASS, "load_cmd", "0x%08x (Load RCW %s checksum)", pbl->load_rcw_cmd, rep->has_checksum ? "with" : "without");

  /* checksum */
  if (!rep->has_checksum)
    sanity_log_add(log, SANITY_PASS, "checksum", "skipped (Load RCW without checksum)");
  else if (rep->checksum_ok)
    sanity_log_add(log, SANITY_PASS, "checksum", "stored 0x%08x matches computed sum", pbl->checksum);
  else
    sanity_log_add(log, SANITY_FAIL, "checksum", "stored 0x%08x != computed 0x%08x", pbl->checksum, pbl->checksum_computed);

  /* terminator - the Boot 0 Reset Handler's PBI check */
  bool checked = rep->pbi == RCW_OK || rep->pbi == RCW_ERR_PBI_CONDITIONAL;
  switch (rep->pbi) {
    case RCW_OK:
    case RCW_ERR_PBI_CONDITIONAL:
      if (chk->len == 0)
        sanity_log_add(log, SANITY_WARN, "terminator", "RCW[PBI_LENGTH]=0 - no PBI, the bootrom runs nothing after the RCW");
      else
        sanity_log_add(log, SANITY_PASS, "terminator", "%s at byte %zu, where RCW[PBI_LENGTH]=%u words points",
                       pbl_op_label(chk->stop_op), RCW_PBL_PBI_OFFSET + chk->len - RCW_PBL_TERMINATOR_BYTES, pbl->pbi_length);
      break;
    case RCW_ERR_PBI_LENGTH:
      sanity_log_add(log, SANITY_FAIL, "terminator", "RCW[PBI_LENGTH]=%u words does not point at a Stop/CRC and Stop (walk ended at byte %zu after %zu commands)",
                     pbl->pbi_length, RCW_PBL_PBI_OFFSET + chk->end, chk->ncmd);
      break;
    case RCW_ERR_PBI_OVERRUN:
      sanity_log_add(log, SANITY_FAIL, "terminator", "%s at byte %zu runs past RCW[PBI_LENGTH]=%u words",
                     pbl_op_label(chk->cmd.op), RCW_PBL_PBI_OFFSET + chk->end, pbl->pbi_length);
      break;
    case RCW_ERR_PBI_JUMP:
      sanity_log_add(log, SANITY_FAIL, "terminator", "%s at byte %zu: offset 0x%08x is not a multiple of 4 above 8, or lands at or past RCW[PBI_LENGTH]=%u words",
                     pbl_op_label(chk->cmd.op), RCW_PBL_PBI_OFFSET + chk->end, chk->cmd.args[0], pbl->pbi_length);
      break;
    case RCW_ERR_PBI_NO_CONDITION:
      sanity_log_add(log, SANITY_FAIL, "terminator", "Jump Conditional at byte %zu before any Load Condition", RCW_PBL_PBI_OFFSET + chk->end);
      break;
    case RCW_ERR_BAD_BINARY:
      sanity_log_add(log, SANITY_FAIL, "terminator", "word 0x%08x at byte %zu is not a PBI command", chk->cmd.word, RCW_PBL_PBI_OFFSET + chk->end);
      break;
    case RCW_ERR_TRUNCATED:
      sanity_log_add(log, SANITY_WARN, "terminator", "RCW[PBI_LENGTH]=%u words (%zu bytes) runs past the %zu bytes available - truncated image or read window",
                     pbl->pbi_length, chk->len, pbl->pbi_avail);
      break;
    default:
      sanity_log_add(log, SANITY_FAIL, "terminator", "PBI check: %s", rcw_strerror(rep->pbi));
      break;
  }

  /* jumpc - the path the image leaves to the hardware */
  if (rep->pbi == RCW_ERR_PBI_CONDITIONAL)
    sanity_log_add(log, SANITY_WARN, "jumpc", "Jump Conditional at byte %zu: the path taken depends on a register read at boot, the commands after it are not walked",
                   RCW_PBL_PBI_OFFSET + chk->end);

  /* pbi_crc */
  if (checked && chk->len > 0) {
    if (chk->stop_op != RCW_PBI_CRC_STOP)
      sanity_log_add(log, SANITY_PASS, "pbi_crc", "none (plain Stop terminator)");
    else if (rep->crc_ok)
      sanity_log_add(log, SANITY_PASS, "pbi_crc", "stored 0x%08x matches computed CRC-32", chk->crc);
    else
      sanity_log_add(log, SANITY_FAIL, "pbi_crc", "stored 0x%08x != computed 0x%08x", chk->crc, chk->crc_computed);
  }

  /* rcw_consistent (optional) */
  if (live_rcw) {
    if (memcmp(pbl->rcw, live_rcw, RCW_PBL_RCW_BYTES) == 0)
      sanity_log_add(log, SANITY_PASS, "rcw_consistent", "on-flash RCW matches live RCWSR snapshot");
    else
      sanity_log_add(log, SANITY_WARN, "rcw_consistent", "on-flash RCW differs from live RCWSR - perhaps reading the fallback slot, or flash was rewritten since boot");
  }

  /* pbi_commands - informational */
  if (rep->pbi == RCW_ERR_PBI_CONDITIONAL)
    sanity_log_add(log, SANITY_PASS, "pbi_commands", "%zu commands walked, up to the Jump Conditional at byte %zu", chk->ncmd, RCW_PBL_PBI_OFFSET + chk->end);
  else
    sanity_log_add(log, SANITY_PASS, "pbi_commands", "%zu commands in %zu bytes, terminator included", chk->ncmd, chk->end);
}

const char *
pbl_op_label(rcw_pbi_op_t op) {
  switch (op) {
    case RCW_PBI_STOP:
      return "Stop";
    case RCW_PBI_CRC_STOP:
      return "CRC and Stop";
    default:
      return rcw_pbi_op_name(op);
  }
}

void
pbl_print_cmd(FILE *out, const rcw_pbi_cmd_t *cmd, size_t off) {
  char line[RCW_PBI_SOURCE_MAX];

  rcw_pbi_to_source(cmd, line, sizeof(line));
  fprintf(out, "%6zu  %s\n", off, line);
}
