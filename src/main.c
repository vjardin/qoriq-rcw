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

#include "flash_db.h"
#include "flash_dump.h"
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
"  --dump-flash  Flash    : --dump-flash -o OUT.rcw [--slot S] [--device PATH]\n"
"\n"
"Options:\n"
"  -i FILE       Input filename (compile / decompile)\n"
"  -o FILE       Output filename (required; with --slot=all the slot\n"
"                name is inserted before the extension: foo.rcw ->\n"
"                foo.primary.rcw + foo.fallback.rcw)\n"
"  -r            Reverse: decompile binary to source\n"
"  --rcwi FILE   RCWI definition file (required with -r;\n"
"                optional with --dump/--dump-flash if "
                  QORIQ_RCW_DATADIR " holds it)\n"
"  -I PATH       Include path (can be repeated)\n"
"  -D FIELD=VAL  Override bitfield value (can be repeated)\n"
"  --no-pbl      Do not generate PBL preamble/end-command\n"
"  -w            Enable warning messages\n"
"  --dump        Read live RCWSR registers via /dev/mem and emit a .rcw\n"
"  --dump-flash  Read RCW from the boot medium (recovers PBI commands too)\n"
"  --slot WHICH  primary | fallback | all (default: primary)\n"
"  --device PATH Override boot device (auto-detected via PORSR1.RCW_SRC\n"
"                + /proc/mtd scan; required for I2C EEPROM)\n"
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

/* ------------------------------------------------------------------ */
/* --dump-flash                                                       */
/* ------------------------------------------------------------------ */

/*
 * Insert ".<slot>" before the last "." of `base`, or append it if no
 * dot is present. Returns malloc'd string the caller must free.
 *
 * Examples: ("foo.rcw", "primary") -> "foo.primary.rcw"
 *           ("foo",     "primary") -> "foo.primary"
 *           ("a/b.x",   "fb")      -> "a/b.fb.x"
 */
static char *
slot_filename(const char *base, const char *slot) {
  /* Find last "." after the last "/" so paths like "a.b/c" work. */
  const char *last_slash = strrchr(base, '/');
  const char *search_from = last_slash ? last_slash + 1 : base;
  const char *dot = strrchr(search_from, '.');
  size_t prefix_len = dot ? (size_t)(dot - base) : strlen(base);
  const char *suffix = dot ? dot : "";
  size_t need = prefix_len + 1 + strlen(slot) + strlen(suffix) + 1;
  char *out = malloc(need);
  if (!out) return NULL;
  snprintf(out, need, "%.*s.%s%s",
           (int)prefix_len, base, slot, suffix);
  return out;
}

/*
 * Write one slot's output: header + (preprocessed .rcwi -> decompiled
 * source via rcw_decompile_buffer). Returns 0 / -1.
 */
static int
write_slot_output(rcw_ctx_t *ctx, const char *out_path,
                  const soc_info_t *soc, uint32_t svr,
                  uint32_t porsr1, uint32_t rcw_src,
                  const flash_info_t *flash, const char *device,
                  const flash_slot_t *slot, const sanity_log_t *log,
                  const char *rcwi_pp, size_t rcwi_pp_len,
                  const char *rcwi_basename) {
  char *source = NULL;
  size_t source_len = 0;
  rcw_error_t e = rcw_decompile_buffer(ctx, rcwi_pp, rcwi_pp_len,
                                       slot->data, slot->pbl_len,
                                       rcwi_basename, &source, &source_len);
  if (e != RCW_OK) {
    warnx("decompile %s slot: %s: %s", slot->name,
          rcw_strerror(e), rcw_ctx_last_error_detail(ctx));
    return -1;
  }

  FILE *f = fopen(out_path, "w");
  if (!f) {
    warn("%s", out_path);
    rcw_free(source);
    return -1;
  }
  flash_dump_write_header(f, soc, svr, porsr1, rcw_src, flash,
                          device, slot, log, QORIQ_RCW_VERSION);
  fwrite(source, 1, source_len, f);
  fclose(f);
  rcw_free(source);
  return 0;
}

static int
cmd_dump_flash(rcw_ctx_t *ctx, const char *output_path, const char *user_rcwi,
               const char *mem_path, const char *user_device,
               const char *slot_arg, int force) {
  if (!output_path)
    errx(EXIT_FAILURE, "--dump-flash requires -o");

  const char *want_slot = slot_arg ? slot_arg : "primary";
  bool want_primary  = (strcmp(want_slot, "primary")  == 0 ||
                        strcmp(want_slot, "all")      == 0);
  bool want_fallback = (strcmp(want_slot, "fallback") == 0 ||
                        strcmp(want_slot, "all")      == 0);
  if (!want_primary && !want_fallback)
    errx(EXIT_FAILURE, "--slot must be one of: primary, fallback, all");

  /* SoC detection (DT then SVR-probe). */
  char dt_detail[256] = {0};
  const soc_info_t *soc = soc_db_detect(dt_detail, sizeof(dt_detail));
  if (!soc) {
    /* Probe via SVR using the first table entry as the address template. */
    const soc_info_t *probe = soc_db_at(0);
    uint32_t svr_probe = 0;
    char rerr[256] = {0};
    uint8_t dummy_rcw[RCW_DUMP_BYTES];
    if (runtime_read_rcw(probe, mem_path, dummy_rcw, &svr_probe,
                         rerr, sizeof(rerr)) != 0)
      errx(EXIT_FAILURE,
           "SoC detection failed: DT (%s) and SVR probe (%s)",
           dt_detail, rerr);
    soc = soc_db_find_by_svr(svr_probe);
    if (!soc)
      errx(EXIT_FAILURE,
           "SoC detection failed: SVR=0x%08x has no entry", svr_probe);
  }

  /* Read PORSR1 + RCW + SVR (RCW used for rcw_consistent check). */
  uint32_t porsr1 = 0, svr = 0, rcw_src = 0;
  uint8_t live_rcw[RCW_DUMP_BYTES];
  char rerr[256] = {0};

  if (runtime_read_porsr1(soc, mem_path, &porsr1, rerr, sizeof(rerr)) != 0)
    errx(EXIT_FAILURE, "%s", rerr);
  if (runtime_read_rcw(soc, mem_path, live_rcw, &svr, rerr, sizeof(rerr)) != 0)
    errx(EXIT_FAILURE, "%s", rerr);

  rcw_src = flash_db_porsr1_to_rcw_src(porsr1);
  const flash_info_t *flash = flash_db_find_by_rcw_src(rcw_src);
  if (!flash)
    errx(EXIT_FAILURE,
         "PORSR1=0x%08x -> RCW_SRC=0b%u%u%u%u not in known boot-source table",
         porsr1, (rcw_src>>3)&1,(rcw_src>>2)&1,(rcw_src>>1)&1,rcw_src&1);

  /* Resolve boot device. */
  char *device = NULL;
  if (user_device) {
    device = strdup(user_device);
  } else {
    char derr[256] = {0};
    device = flash_db_resolve_device(flash, derr, sizeof(derr));
    if (!device)
      errx(EXIT_FAILURE, "%s", derr);
    warnx("auto-detected boot device: %s (override with --device)", device);
  }

  /* Resolve .rcwi (shared with --dump). */
  char rerr2[256] = {0};
  char *rcwi_path = resolve_rcwi(user_rcwi, soc, rerr2, sizeof(rerr2));
  if (!rcwi_path) {
    free(device);
    errx(EXIT_FAILURE, "%s", rerr2);
  }
  const char *rcwi_basename = strrchr(rcwi_path, '/');
  rcwi_basename = rcwi_basename ? rcwi_basename + 1 : rcwi_path;

  /* Preprocess .rcwi once via mcpp; reused for both slots. */
  char *rcwi_pp = NULL;
  size_t rcwi_pp_len = 0;
  rcw_error_t pe = rcw_preprocess_file(ctx, rcwi_path,
                                       &rcwi_pp, &rcwi_pp_len);
  if (pe != RCW_OK) {
    /* Don't free rcwi_path before errx - keep the pointer valid for
     * the diagnostic. The process exits immediately after. */
    free(device);
    errx(EXIT_FAILURE, "preprocess %s: %s",
         rcwi_path, rcw_strerror(pe));
  }

  /* Read the requested slot(s). */
  flash_slot_t primary  = {0}, fallback = {0};
  sanity_log_t  plog    = {0}, flog     = {0};

  if (want_primary) {
    if (flash_dump_read_slot(device, flash->primary_offset, "primary",
                             &primary, rerr, sizeof(rerr)) != 0)
      errx(EXIT_FAILURE, "%s", rerr);
    flash_dump_sanity_run(&primary, live_rcw, &plog);
  }
  if (want_fallback) {
    if (flash->fallback_offset == 0) {
      warnx("boot source %s has no fallback slot - skipping",
            flash->rcw_src_name);
      want_fallback = false;
    } else if (flash_dump_read_slot(device, flash->fallback_offset, "fallback",
                                    &fallback, rerr, sizeof(rerr)) != 0) {
      warnx("could not read fallback: %s", rerr);
      want_fallback = false;
    } else {
      flash_dump_sanity_run(&fallback, live_rcw, &flog);
    }
  }

  /* If both, also append fallback_match to whichever log gets written. */
  if (want_primary && want_fallback)
    flash_dump_compare_slots(&primary, &fallback, &flog);

  /* Honour --force. Both slots collapse into one decision. */
  bool any_failed = (want_primary && plog.any_failed) ||
                    (want_fallback && flog.any_failed);
  if (any_failed && !force) {
    flash_slot_free(&primary); flash_slot_free(&fallback);
    rcw_free(rcwi_pp); free(rcwi_path); free(device);
    errx(EXIT_FAILURE,
         "sanity checks failed (use --force to emit anyway)");
  }

  int rc = 0;
  bool slot_all = (strcmp(want_slot, "all") == 0);

  if (want_primary) {
    char *path = slot_all
                 ? slot_filename(output_path, "primary")
                 : strdup(output_path);
    if (!path) rc = -1;
    else if (write_slot_output(ctx, path, soc, svr, porsr1, rcw_src,
                               flash, device, &primary, &plog,
                               rcwi_pp, rcwi_pp_len, rcwi_basename) != 0)
      rc = -1;
    free(path);
  }

  if (want_fallback) {
    char *path = slot_all
                 ? slot_filename(output_path, "fallback")
                 : strdup(output_path);
    if (!path) rc = -1;
    else if (write_slot_output(ctx, path, soc, svr, porsr1, rcw_src,
                               flash, device, &fallback, &flog,
                               rcwi_pp, rcwi_pp_len, rcwi_basename) != 0)
      rc = -1;
    free(path);
  }

  flash_slot_free(&primary);
  flash_slot_free(&fallback);
  rcw_free(rcwi_pp);
  free(rcwi_path);
  free(device);
  return rc == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}

enum {
  OPT_DUMP = 1000,
  OPT_MEM,
  OPT_FORCE,
  OPT_DUMP_FLASH,
  OPT_SLOT,
  OPT_DEVICE,
};

int
main(int argc, char **argv) {
  const char *input = NULL;
  const char *output = NULL;
  const char *rcwi = NULL;
  const char *mem_path = "/dev/mem";
  const char *device = NULL;
  const char *slot = NULL;
  int reverse = 0;
  int dump = 0;
  int dump_flash = 0;
  int force = 0;
  int no_pbl = 0;
  int warnings = 0;

  /* Collect -D and -I options */
  const char *includes[32];
  int nincludes = 0;
  const char *defines[64];
  int ndefines = 0;

  static struct option long_opts[] = {
    {"rcwi",       required_argument, NULL, 'R'},
    {"no-pbl",     no_argument,       NULL, 'P'},
    {"help",       no_argument,       NULL, 'h'},
    {"dump",       no_argument,       NULL, OPT_DUMP},
    {"dump-flash", no_argument,       NULL, OPT_DUMP_FLASH},
    {"slot",       required_argument, NULL, OPT_SLOT},
    {"device",     required_argument, NULL, OPT_DEVICE},
    {"mem",        required_argument, NULL, OPT_MEM},
    {"force",      no_argument,       NULL, OPT_FORCE},
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
      case OPT_DUMP:        dump = 1; break;
      case OPT_DUMP_FLASH:  dump_flash = 1; break;
      case OPT_SLOT:        slot = optarg; break;
      case OPT_DEVICE:      device = optarg; break;
      case OPT_MEM:         mem_path = optarg; break;
      case OPT_FORCE:       force = 1; break;
      case 'h':
        usage(argv[0]);
        return EXIT_SUCCESS;
      default:
        usage(argv[0]);
        return EXIT_FAILURE;
    }
  }

  /* Mode validation. */
  int mode_count = (dump != 0) + (dump_flash != 0) + (reverse != 0);
  if (mode_count > 1)
    errx(EXIT_FAILURE,
         "--dump, --dump-flash and -r are mutually exclusive");
  if ((dump || dump_flash) && input)
    errx(EXIT_FAILURE,
         "--dump/--dump-flash do not take -i");
  if (!dump && !dump_flash) {
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
  } else if (dump_flash) {
    status = cmd_dump_flash(ctx, output, rcwi, mem_path,
                            device, slot, force);
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
