# Mocka Dock

A modern taskbar dock for the MATE and Mocka desktops.

Mocka Dock is a MATE panel applet that shows pinned and running applications
as one row of buttons, with one button per application, in the style of the
Windows taskbar. It targets X11 on FreeBSD and GhostBSD first.

Mocka Dock is an independent implementation written from its own
specification (see [SPEC.md](SPEC.md)). It shares no code with other
dock applets.

## Status

Early development. The work is split into milestones in [PLAN.md](PLAN.md).

Working today:

- One button per running application, with its icon from the icon theme,
  sharp on HiDPI screens and following icon theme changes.
- Windows matched to their application from the desktop entries, including
  Chromium web apps and MATE applications without `StartupWMClass`.
- Left click activates or minimizes a window; for several windows it shows a
  list to pick from.
- Middle click and Shift + click open a new window of the application.
- A bar under running applications and a highlight on the focused one, in the
  theme's colors.
- Minimize and restore animations go to the application's button.
- Horizontal and vertical panels of any size.

Not there yet: pinning, the right-click menus, thumbnails, and keyboard
shortcuts. Right click currently shows the panel's own menu.

## Building

Dependencies on FreeBSD and GhostBSD:

```sh
pkg install meson ninja pkgconf gettext-tools gtk3 mate-panel libwnck3 \
    libXcomposite libXdamage
```

Build, test, and install:

```sh
meson setup build
meson compile -C build
meson test -C build
sudo meson install -C build
```

Then right click a MATE panel, choose "Add to Panel", and pick Mocka Dock.

The dock runs inside mate-panel, so after installing a new build restart the
panel with `mate-panel --replace &`.

## Test data

`docs/test-data/` holds real window properties recorded on GhostBSD, which
the matcher tests run against. To record a new application, open it and run:

```sh
tools/collect-test-data.sh name expected-desktop-id
```

See [docs/test-data/README.md](docs/test-data/README.md) for the format.

## License

BSD-3-Clause. See [LICENSE](LICENSE).