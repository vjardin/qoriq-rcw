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
