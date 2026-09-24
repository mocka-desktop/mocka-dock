applet_conf = configuration_data()
applet_conf.set('LIBEXECDIR', libexecdir)

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

configure_file(
  input: 'org.mate.panel.applet.MockaDockAppletFactory.service.in',
  output: 'org.mate.panel.applet.MockaDockAppletFactory.service',
  configuration: applet_conf,
  install: true,
  install_dir: datadir / 'dbus-1' / 'services',
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
