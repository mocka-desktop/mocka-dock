project('mocka-dock', 'c',
  version: '0.0.1',
  license: 'BSD-3-Clause',
  meson_version: '>= 0.60.0',
  default_options: [
    'c_std=c11',
    'warning_level=2',
  ],
)

gnome = import('gnome')
i18n = import('i18n')

prefix = get_option('prefix')
datadir = prefix / get_option('datadir')
libexecdir = prefix / get_option('libexecdir')
localedir = prefix / get_option('localedir')

gettext_package = meson.project_name()

cc = meson.get_compiler('c')
add_project_arguments(
  cc.get_supported_arguments([
    '-Wno-unused-parameter',
    '-Wno-missing-field-initializers',
  ]),
  language: 'c',
)

gtk_dep = dependency('gtk+-3.0', version: '>= 3.22')
applet_dep = dependency('libmatepanelapplet-4.0', version: '>= 1.26')
wnck_dep = dependency('libwnck-3.0', version: '>= 40')
gio_dep = dependency('gio-unix-2.0')
x11_dep = dependency('x11')
xcomposite_dep = dependency('xcomposite')
xdamage_dep = dependency('xdamage')
cairo_xlib_dep = dependency('cairo-xlib')

conf = configuration_data()
conf.set_quoted('PACKAGE_VERSION', meson.project_version())
conf.set_quoted('GETTEXT_PACKAGE', gettext_package)
conf.set_quoted('MATELOCALEDIR', localedir)
conf.set('ENABLE_NLS', 1)
configure_file(output: 'config.h', configuration: conf)

config_inc = include_directories('.')

subdir('data')
subdir('src')
subdir('tests')
subdir('po')

gnome.post_install(glib_compile_schemas: true)
