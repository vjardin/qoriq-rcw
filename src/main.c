/*
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright 2026 Free Mobile - Vincent Jardin
 *
 * qoriq-rcw: CLI tool for compiling/decompiling RCW files.
 * Equivalent to rcw.py for pbiformat=2 platforms.
 */

#include <err.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <qoriq-rcw/rcw.h>

static void
usage(const char *prog) {
  fprintf(stderr,

"Usage: %s [options]\n"
"\n"
"Options:\n"
"  -i FILE       Input filename\n"
"  -o FILE       Output filename\n"
"  -r            Reverse: decompile binary to source\n"
"  --rcwi FILE   RCWI definition file (required with -r)\n"
"  -I PATH       Include path (can be repeated)\n"
"  -D FIELD=VAL  Override bitfield value (can be repeated)\n"
"  --no-pbl      Do not generate PBL preamble/end-command\n"
"  -w            Enable warning messages\n"
"  -h, --help    Show this help\n",

  prog);
}

int
main(int argc, char **argv) {
  const char *input = NULL;
  const char *output = NULL;
  const char *rcwi = NULL;
  int reverse = 0;
  int no_pbl = 0;
  int warnings = 0;

  /* Collect -D and -I options */
  const char *includes[32];
  int nincludes = 0;
  const char *defines[64];
  int ndefines = 0;

  static struct option long_opts[] = {
    {"rcwi",  required_argument, NULL, 'R'},
    {"no-pbl", no_argument, NULL, 'P'},
    {"help",  no_argument, NULL, 'h'},
    {NULL, 0, NULL, 0},
  };

  int opt;
  while ((opt = getopt_long(argc, argv, "i:o:rI:D:wh", long_opts, NULL)) != -1) {
    switch (opt) {
      case 'i':
        input = optarg;
        break;
      case 'o':
        output = optarg;
        break;
      case 'r':
        reverse = 1;
        break;
      case 'R':
        rcwi = optarg;
        break;
      case 'I':
        if (nincludes < 32)
          includes[nincludes++] = optarg;
        break;
      case 'D':
        if (ndefines < 64)
          defines[ndefines++] = optarg;
        break;
      case 'P':
        no_pbl = 1;
        break;
      case 'w':
        warnings = 1;
        break;
      case 'h':
        usage(argv[0]);
        return EXIT_SUCCESS;
      default:
        usage(argv[0]);
        return EXIT_FAILURE;
    }
  }

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

  rcw_ctx_t *ctx = rcw_ctx_new();
  if (!ctx)
    errx(EXIT_FAILURE, "out of memory");

  if (no_pbl)
    rcw_ctx_set_pbl(ctx, 0);
  if (warnings)
    rcw_ctx_set_warnings(ctx, 1);

  for (int i = 0; i < nincludes; i++)
    rcw_ctx_add_include_path(ctx, includes[i]);

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

  rcw_error_t rcw_err;

  /* decompile */
  if (reverse) {
    char *source = NULL;
    size_t source_len = 0;

    rcw_err = rcw_decompile_file(ctx, input, rcwi, &source, &source_len);
    if (rcw_err != RCW_OK) {
      rcw_ctx_free(ctx);
      errx(EXIT_FAILURE, "%s: %s", rcw_strerror(rcw_err), rcw_ctx_last_error_detail(ctx));
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

    goto fini;
  }

  if (!reverse) {
    uint8_t *binary = NULL;
    size_t binary_len = 0;

    rcw_err = rcw_compile_file(ctx, input, &binary, &binary_len);
    if (rcw_err != RCW_OK) {
      rcw_ctx_free(ctx);
      errx(EXIT_FAILURE, "%s: %s", rcw_strerror(rcw_err), rcw_ctx_last_error_detail(ctx));
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

fini:
  rcw_ctx_free(ctx);

  return EXIT_SUCCESS;
}
