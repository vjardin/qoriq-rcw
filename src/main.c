/*
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright 2026 Free Mobile - Vincent Jardin
 *
 * qoriq-rcw: CLI tool for compiling/decompiling RCW files.
 * Equivalent to rcw.py for pbiformat=2 platforms.
 */

#define _POSIX_C_SOURCE 200809L

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <qoriq-rcw/rcw.h>

#include "runtime_dump.h"
#include "soc_db.h"

#ifndef QORIQ_RCW_DATADIR
# define QORIQ_RCW_DATADIR "/usr/share/qoriq-rcw"
#endif
#ifndef QORIQ_RCW_VERSION
# define QORIQ_RCW_VERSION "0.0.0"
#endif

static void
usage(const char *prog) {
  fprintf(stderr,
"Usage: %s [options]\n"
"\n"
"Modes:\n"
"  (default)     Compile  : -i SRC.rcw -o OUT.bin\n"
"  -r            Decompile: -r -i IN.bin --rcwi FILE.rcwi -o OUT.rcw\n"
"  --dump        Runtime  : --dump -o OUT.rcw [--rcwi FILE.rcwi]\n"
"\n"
"Options:\n"
"  -i FILE       Input filename (compile / decompile)\n"
"  -o FILE       Output filename (required)\n"
"  -r            Reverse: decompile binary to source\n"
"  --rcwi FILE   RCWI definition file (required with -r;\n"
"                optional with --dump if " QORIQ_RCW_DATADIR " holds it)\n"
"  -I PATH       Include path (can be repeated)\n"
"  -D FIELD=VAL  Override bitfield value (can be repeated)\n"
"  --no-pbl      Do not generate PBL preamble/end-command\n"
"  -w            Enable warning messages\n"
"  --dump        Read live RCWSR registers via /dev/mem and emit a .rcw\n"
"  --mem PATH    Override /dev/mem (for testing with a fixture file)\n"
"  --force       Emit dump even if sanity checks fail\n"
"  -h, --help    Show this help\n",
    prog);
}

static int
file_readable(const char *p) {
  return access(p, R_OK) == 0;
}

/* Resolve a .rcwi for --dump mode:
 *   1. honour an explicit --rcwi if given
 *   2. else try $DATADIR/qoriq-rcw/<soc->default_rcwi>
 *   3. else NULL (caller errors out)
 */
static char *
resolve_rcwi(const char *user_rcwi, const soc_info_t *soc, char *err, size_t errlen) {
  if (user_rcwi) {
    if (!file_readable(user_rcwi)) {
      snprintf(err, errlen, "rcwi not readable: %s", user_rcwi);
      return NULL;
    }
    return strdup(user_rcwi);
  }

  size_t need = strlen(QORIQ_RCW_DATADIR) + 1 + strlen(soc->default_rcwi) + 1;
  char *path = malloc(need);
  if (!path) {
    snprintf(err, errlen, "out of memory");
    return NULL;
  }
  snprintf(path, need, "%s/%s", QORIQ_RCW_DATADIR, soc->default_rcwi);

  if (file_readable(path))
    return path;

  snprintf(err, errlen, "no .rcwi found at %s - install qoriq-rcw-data or pass --rcwi", path);
  free(path);
  return NULL;
}

/* Run the --dump pipeline. Returns EXIT_SUCCESS / EXIT_FAILURE. */
static int cmd_dump(rcw_ctx_t *ctx, const char *output_path, const char *user_rcwi,
         const char *mem_path, int force) {
  if (!output_path) {
    warnx("--dump requires -o");
    return EXIT_FAILURE;
  }

  char dt_detail[256] = {0};
  const soc_info_t *soc = soc_db_detect(dt_detail, sizeof(dt_detail));
  const char *dt_detail_for_log = soc ? dt_detail : NULL;

  /* If DT detection failed, try an SVR probe via the default DCFG
   * layout (all four families share 0x01E00000). */
  if (!soc) {
    /* Pick the first table entry as a "probe template": all entries
     * share dcfg_base/svr_offset, so any one works for reading SVR. */
    const soc_info_t *probe = soc_db_at(0);
    if (!probe) {
      warnx("internal: empty SoC database");
      return EXIT_FAILURE;
    }

    uint8_t  dummy_rcw[RCW_DUMP_BYTES];
    uint32_t svr = 0;
    char rerr[256] = {0};
    int rc = runtime_read_rcw(probe, mem_path, dummy_rcw, &svr,
                              rerr, sizeof(rerr));
    if (rc != 0) {
      warnx("SoC detection failed via DT (%s) and SVR probe (%s)", dt_detail, rerr);
      return EXIT_FAILURE;
    }

    soc = soc_db_find_by_svr(svr);
    if (!soc) {
      warnx("SoC detection failed: DT match: %s; SVR=0x%08x has no " "matching SoC entry", dt_detail, svr);
      return EXIT_FAILURE;
    }
  }

  /* Now read the live RCWSR + SVR for real. */
  uint8_t  rcw[RCW_DUMP_BYTES];
  uint32_t svr = 0;
  char rerr[256] = {0};
  int rc = runtime_read_rcw(soc, mem_path, rcw, &svr,
                            rerr, sizeof(rerr));
  if (rc != 0)
    errx(EXIT_FAILURE, "%s", rerr);

  /* Sanity. */
  sanity_log_t log;
  runtime_sanity_run(soc, dt_detail_for_log, svr, rcw, &log);
  if (log.any_failed && !force)
    errx(EXIT_FAILURE, "sanity checks failed (use --force to emit anyway); the most useful failed check is the first FAIL above");

  /* Locate .rcwi. */
  char rerr2[256] = {0};
  char *rcwi_path = resolve_rcwi(user_rcwi, soc, rerr2, sizeof(rerr2));
  if (!rcwi_path)
    errx(EXIT_FAILURE, "%s", rerr2);

  /*
   * In-memory decompile path: preprocess the .rcwi via mcpp (so
   * macros / #include in the .rcwi expand), then feed the resulting
   * text together with the live 128-byte RCW buffer to
   * rcw_decompile_buffer(). No tempfile, no double I/O. The library
   * accepts a bare 128-byte buffer with no PBL preamble (see
   * rcw_decompile.c - len == 128 skips the preamble check).
   */
  char *rcwi_pp = NULL;
  size_t rcwi_pp_len = 0;
  rcw_error_t rcw_err = rcw_preprocess_file(ctx, rcwi_path, &rcwi_pp, &rcwi_pp_len);
  if (rcw_err != RCW_OK) {
    char *path_copy = strdup(rcwi_path);
    free(rcwi_path);
    errx(EXIT_FAILURE, "preprocess %s: %s", path_copy ? path_copy : "(?)", rcw_strerror(rcw_err));
  }

  /* basename for the #include line in the output. */
  const char *base = strrchr(rcwi_path, '/');
  base = base ? base + 1 : rcwi_path;

  char *source = NULL;
  size_t source_len = 0;
  rcw_err = rcw_decompile_buffer(ctx, rcwi_pp, rcwi_pp_len, rcw, sizeof(rcw), base, &source, &source_len);
  rcw_free(rcwi_pp);
  free(rcwi_path);
  if (rcw_err != RCW_OK)
    errx(EXIT_FAILURE, "decompile: %s: %s", rcw_strerror(rcw_err), rcw_ctx_last_error_detail(ctx));

  /* Emit: our header + the library's source text. */
  FILE *out = fopen(output_path, "w");
  if (!out) {
    rcw_free(source);
    err(EXIT_FAILURE, "%s", output_path);
  }
  runtime_write_header(out, soc, svr, &log, QORIQ_RCW_VERSION);
  fwrite(source, 1, source_len, out);
  fclose(out);
  rcw_free(source);

  return EXIT_SUCCESS;
}

enum {
  OPT_DUMP = 1000,
  OPT_MEM,
  OPT_FORCE,
};

int
main(int argc, char **argv) {
  const char *input = NULL;
  const char *output = NULL;
  const char *rcwi = NULL;
  const char *mem_path = "/dev/mem";
  int reverse = 0;
  int dump = 0;
  int force = 0;
  int no_pbl = 0;
  int warnings = 0;

  /* Collect -D and -I options */
  const char *includes[32];
  int nincludes = 0;
  const char *defines[64];
  int ndefines = 0;

  static struct option long_opts[] = {
    {"rcwi",   required_argument, NULL, 'R'},
    {"no-pbl", no_argument,       NULL, 'P'},
    {"help",   no_argument,       NULL, 'h'},
    {"dump",   no_argument,       NULL, OPT_DUMP},
    {"mem",    required_argument, NULL, OPT_MEM},
    {"force",  no_argument,       NULL, OPT_FORCE},
    {NULL, 0, NULL, 0},
  };

  int opt;
  while ((opt = getopt_long(argc, argv, "i:o:rI:D:wh", long_opts, NULL)) != -1) {
    switch (opt) {
      case 'i': input = optarg; break;
      case 'o': output = optarg; break;
      case 'r': reverse = 1; break;
      case 'R': rcwi = optarg; break;
      case 'I':
        if (nincludes < 32) includes[nincludes++] = optarg;
        break;
      case 'D':
        if (ndefines < 64) defines[ndefines++] = optarg;
        break;
      case 'P': no_pbl = 1; break;
      case 'w': warnings = 1; break;
      case OPT_DUMP:  dump = 1; break;
      case OPT_MEM:   mem_path = optarg; break;
      case OPT_FORCE: force = 1; break;
      case 'h':
        usage(argv[0]);
        return EXIT_SUCCESS;
      default:
        usage(argv[0]);
        return EXIT_FAILURE;
    }
  }

  /* Mode validation. */
  if (dump && reverse)
    errx(EXIT_FAILURE, "--dump and -r are mutually exclusive");
  if (dump && input)
    errx(EXIT_FAILURE, "--dump does not take -i (input is /dev/mem)");
  if (!dump) {
    if (!input) {
      warnx("-i is required");
      usage(argv[0]);
      return EXIT_FAILURE;
    }
    if (!output) {
      warnx("-o is required");
      usage(argv[0]);
      return EXIT_FAILURE;
    }
    if (reverse && !rcwi) {
      warnx("-r requires --rcwi");
      return EXIT_FAILURE;
    }
  }

  rcw_ctx_t *ctx = rcw_ctx_new();
  if (!ctx)
    errx(EXIT_FAILURE, "out of memory");

  if (no_pbl)
    rcw_ctx_set_pbl(ctx, 0);
  if (warnings)
    rcw_ctx_set_warnings(ctx, 1);

  for (int i = 0; i < nincludes; i++)
    rcw_ctx_add_include_path(ctx, includes[i]);

  /* Always add the data dir to the search path so #include <foo.rcwi>
   * lines emitted by --dump or by the decompiler resolve when the
   * qoriq-rcw-data package is installed. Cheap if the dir is absent. */
  rcw_ctx_add_include_path(ctx, QORIQ_RCW_DATADIR);

  for (int i = 0; i < ndefines; i++) {
    char *eq = strchr(defines[i], '=');
    if (!eq) {
      rcw_ctx_free(ctx);
      errx(EXIT_FAILURE, "bad -D syntax: %s", defines[i]);
    }
    *eq = '\0';
    uint64_t val = strtoull(eq + 1, NULL, 0);
    rcw_error_t rcw_err = rcw_ctx_set_bitfield(ctx, defines[i], val);
    *eq = '='; /* restore for error messages */
    if (rcw_err != RCW_OK) {
      rcw_ctx_free(ctx);
      errx(EXIT_FAILURE, "%s", rcw_strerror(rcw_err));
    }
  }

  int status = EXIT_SUCCESS;

  if (dump) {
    status = cmd_dump(ctx, output, rcwi, mem_path, force);
  } else if (reverse) {
    char *source = NULL;
    size_t source_len = 0;
    rcw_error_t rcw_err = rcw_decompile_file(ctx, input, rcwi,
                                             &source, &source_len);
    if (rcw_err != RCW_OK) {
      rcw_ctx_free(ctx);
      errx(EXIT_FAILURE, "%s: %s", rcw_strerror(rcw_err),
           rcw_ctx_last_error_detail(ctx));
    }
    FILE *f = fopen(output, "w");
    if (!f) {
      rcw_free(source);
      rcw_ctx_free(ctx);
      err(EXIT_FAILURE, "%s", output);
    }
    fwrite(source, 1, source_len, f);
    fclose(f);
    rcw_free(source);
  } else {
    uint8_t *binary = NULL;
    size_t binary_len = 0;
    rcw_error_t rcw_err = rcw_compile_file(ctx, input, &binary, &binary_len);
    if (rcw_err != RCW_OK) {
      rcw_ctx_free(ctx);
      errx(EXIT_FAILURE, "%s: %s", rcw_strerror(rcw_err),
           rcw_ctx_last_error_detail(ctx));
    }
    FILE *f = fopen(output, "wb");
    if (!f) {
      rcw_free(binary);
      rcw_ctx_free(ctx);
      err(EXIT_FAILURE, "%s", output);
    }
    fwrite(binary, 1, binary_len, f);
    fclose(f);
    rcw_free(binary);
  }

  rcw_ctx_free(ctx);
  return status;
}
