test_matcher = executable('test-matcher',
  'test-matcher.c',
  dependencies: core_dep,
  c_args: [
    '-DTEST_DATA_DIR="@0@"'.format(meson.project_source_root() / 'docs' / 'test-data'),
  ],
)
test('matcher', test_matcher)
