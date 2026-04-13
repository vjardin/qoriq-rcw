# qoriq-rcw data

Canonical `.rcwi` bitfield definitions for the four pbiformat=2 SoC
families supported by `qoriq-rcw`:

| File             | SoC family          | Source in `nxp-qoriq/rcw`         |
|------------------|---------------------|-----------------------------------|
| `ls1028a.rcwi`   | NXP LS1028A         | `ls1028asi/ls1028a.rcwi`          |
| `ls1088a.rcwi`   | NXP LS1088A         | `ls1088ardb/ls1088rdb.rcwi`       |
| `ls2088a.rcwi`   | NXP LS2088A         | `ls2088asi/ls2088a.rcwi`          |
| `lx2160a.rcwi`   | NXP LX2160A / LX2120A / LX2080A | `lx2160asi/lx2160a.rcwi` |

These files are vendored from <https://github.com/nxp-qoriq/rcw> and are
  (c) 2011-2016 Freescale Semiconductor
  (c) 2017-2020 NXP
under the 3-clause BSD license (see `LICENSE.upstream`). Each file has a SPDX
header prepended; the body is unmodified.

They are installed to `$datadir/qoriq-rcw/` so:
* `qoriq-rcw --dump -o out.rcw` resolves the matching `.rcwi` automatically (no `--rcwi` needed).
* `qoriq-rcw -i out.rcw -o out.bin` recompiles dump output without needing `-I` because the CLI
  adds `$datadir/qoriq-rcw` to the preprocessor search path automatically.
