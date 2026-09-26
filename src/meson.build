# Logic without GTK, shared by the applet and the unit tests.
core_lib = static_library('mocka-dock-core',
  'app-index.c',
  'matcher.c',
  'dock-model.c',
  'pinned-list.c',
  'desktop-import.c',
  'startup-message.c',
  include_directories: config_inc,
  dependencies: [gio_dep],
)

core_dep = declare_dependency(
  link_with: core_lib,
  include_directories: include_directories('.'),
  dependencies: [gio_dep],
)

shared_module('mocka-dock-applet',
  'mocka-dock-applet.c',
  'dock-button.c',
  'undo-popup.c',
  'window-tracker.c',
  include_directories: config_inc,
  dependencies: [core_dep, gtk_dep, applet_dep, wnck_dep, x11_dep, cairo_xlib_dep,
                 math_dep],
  install: true,
  install_dir: applet_libdir,
)