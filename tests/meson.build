test_matcher = executable('test-matcher',
  'test-matcher.c',
  dependencies: core_dep,
  c_args: [
    '-DTEST_DATA_DIR="@0@"'.format(meson.project_source_root() / 'docs' / 'test-data'),
  ],
)
test('matcher', test_matcher)

test_dock_model = executable('test-dock-model',
  'test-dock-model.c',
  dependencies: core_dep,
)
test('dock-model', test_dock_model)