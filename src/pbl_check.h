/*
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright 2026 Free Mobile - Vincent Jardin
 *
 * The sanity log of a PBL image, from the verdict of libqoriq-rcw
 */

#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include <qoriq-rcw/rcw.h>

#include "sanity_log.h"

void pbl_sanity_run(const rcw_pbl_report_t *rep, size_t len, const uint8_t *live_rcw, sanity_log_t *log);

const char *pbl_op_label(rcw_pbi_op_t op);

void pbl_print_cmd(FILE *out, const rcw_pbi_cmd_t *cmd, size_t off);
