cmocka_dep = dependency('cmocka', required : false)

if cmocka_dep.found()
  test_inc = include_directories('../src')

  test_soc_db = executable('test_soc_db',
    ['test_soc_db.c', '../src/soc_db.c'],
    include_directories : test_inc,
    dependencies : [cmocka_dep])
  test('test_soc_db', test_soc_db, suite : 'unit')

  test_runtime_dump = executable('test_runtime_dump',
    ['test_runtime_dump.c', '../src/runtime_dump.c', '../src/soc_db.c'],
    include_directories : test_inc,
    dependencies : [cmocka_dep])
  test('test_runtime_dump', test_runtime_dump, suite : 'unit')
endif

python3 = find_program('python3', required : false)

if python3.found()
  test_env = environment()
  rcw_ref_dir = get_option('rcw-ref-dir')
  if rcw_ref_dir != ''
    test_env.set('RCW_REF_DIR', rcw_ref_dir)
  endif

  test('cross-validate',
    find_program('compare_with_rcwpy.sh'),
    workdir : meson.project_source_root(),
    env : test_env,
    timeout : 300,
    is_parallel : false,
    suite : 'cross-validation')
endif
