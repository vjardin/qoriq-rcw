project('qoriq-rcw', 'c',
  version : '0.1.0',
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

executable('qoriq-rcw',
  'src/main.c',
  dependencies : libqoriq_rcw_dep,
  install : true)

pandoc = find_program('pandoc', required : true)

man1 = custom_target('qoriq-rcw.1',
  input : 'man/qoriq-rcw.1.md',
  output : 'qoriq-rcw.1',
  command : [pandoc, '-s', '-t', 'man', '@INPUT@', '-o', '@OUTPUT@'],
  install : true,
  install_dir : get_option('mandir') / 'man1')

subdir('tests')
