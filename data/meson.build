applet_conf = configuration_data()
applet_conf.set('APPLET_LOCATION', applet_libdir / 'libmocka-dock-applet.so')

applet_desktop_in = configure_file(
  input: 'org.mocka_desktop.DockApplet.mate-panel-applet.desktop.in',
  output: 'org.mocka_desktop.DockApplet.mate-panel-applet.desktop.in',
  configuration: applet_conf,
)

i18n.merge_file(
  input: applet_desktop_in,
  output: 'org.mocka_desktop.DockApplet.mate-panel-applet',
  type: 'desktop',
  po_dir: '../po',
  args: ['--keyword=Description'],
  install: true,
  install_dir: datadir / 'mate-panel' / 'applets',
)

install_data(
  'org.mocka_desktop.Dock.gschema.xml',
  install_dir: datadir / 'glib-2.0' / 'schemas',
)

glib_compile_schemas = find_program('glib-compile-schemas')
test('gschema',
  glib_compile_schemas,
  args: ['--strict', '--dry-run', meson.current_source_dir()],
)
