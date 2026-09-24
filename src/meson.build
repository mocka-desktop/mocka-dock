executable('mocka-dock-applet',
  'mocka-dock-applet.c',
  'probe.c',
  include_directories: config_inc,
  dependencies: [gtk_dep, applet_dep, wnck_dep, x11_dep, cairo_xlib_dep],
  install: true,
  install_dir: libexecdir,
)
