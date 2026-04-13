project('qoriq-rcw', 'c',
  version : '0.2.0',
  license : 'BSD-3-Clause',
  default_options : [
    'c_std=c17',
    'warning_level=3',
  ])

# Find libqoriq-rcw.
#
# If installed system-wide, pkg-config is used automatically.
# For local development, point at the built library tree:
#   1. Build libqoriq-rcw first:
#        cd ../libqoriq-rcw && meson setup build && meson compile -C build
#   2. Then configure qoriq-rcw:
#        meson setup build -Dlibqoriq-rcw-path=../libqoriq-rcw
#
libqoriq_rcw_path = get_option('libqoriq-rcw-path')

if libqoriq_rcw_path != ''
  fs = import('fs')
  lib_src = fs.is_absolute(libqoriq_rcw_path) \
    ? libqoriq_rcw_path \
    : meson.project_source_root() / libqoriq_rcw_path

  lib_build = lib_src / 'build'

  cc = meson.get_compiler('c')
  libqoriq_rcw_lib = cc.find_library('qoriq-rcw',
    dirs : [lib_build],
    required : true)

  # Use -I flag directly since meson rejects absolute include_directories()
  libqoriq_rcw_dep = declare_dependency(
    dependencies : libqoriq_rcw_lib,
    compile_args : ['-I' + lib_src / 'include'])
else
  libqoriq_rcw_dep = dependency('libqoriq-rcw', required : true)
endif

datadir = get_option('prefix') / get_option('datadir') / 'qoriq-rcw'

cli_sources = files(
  'src/flash_db.c',
  'src/flash_dump.c',
  'src/main.c',
  'src/runtime_dump.c',
  'src/soc_db.c',
)

cli_args = [
  '-DQORIQ_RCW_DATADIR="' + datadir + '"',
  '-DQORIQ_RCW_VERSION="' + meson.project_version() + '"',
]

executable('qoriq-rcw',
  cli_sources,
  c_args : cli_args,
  dependencies : libqoriq_rcw_dep,
  install : true)

# Optional companion data - install canonical .rcwi files into
# $datadir/qoriq-rcw so `--dump` works without `--rcwi` and decompiled
# output recompiles via `#include <foo.rcwi>` without `-I`.
rcwi_files = files(
  'data/ls1028a.rcwi',
  'data/ls1088a.rcwi',
  'data/ls2088a.rcwi',
  'data/lx2160a.rcwi',
)
install_data(rcwi_files, install_dir : get_option('datadir') / 'qoriq-rcw')

pandoc = find_program('pandoc', required : true)

man1 = custom_target('qoriq-rcw.1',
  input : 'man/qoriq-rcw.1.md',
  output : 'qoriq-rcw.1',
  command : [pandoc, '-s', '-t', 'man', '@INPUT@', '-o', '@OUTPUT@'],
  install : true,
  install_dir : get_option('mandir') / 'man1')

subdir('tests')
