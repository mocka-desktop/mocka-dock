# Mocka Dock

A modern taskbar dock for the MATE and Mocka desktops.

Mocka Dock is a MATE panel applet that shows pinned and running applications
as one row of buttons, with one button per application, in the style of the
Windows taskbar. It targets X11 on FreeBSD and GhostBSD first.

Mocka Dock is an independent implementation written from its own
specification (see [SPEC.md](SPEC.md)). It shares no code with other
dock applets.

## Status

Alpha. The current release is 0.0.2, the second alpha for GhostBSD testers.
The work is split into milestones in [PLAN.md](PLAN.md), and the
[user guide](https://github.com/mocka-desktop/mocka-dock/wiki) explains
how to use the dock.

Working today:

- One button per application, pinned applications first, then running ones
  in the order they started. Windows are matched to their application from
  the desktop entries, including Chromium web apps, MATE applications, and
  entries made by menu editors.
- Left click activates or minimizes a window, middle click and Shift + click
  open a new one, and the button pulses while an application starts.
- Window thumbnails on hover, live while a compositor runs, with each
  window's last look kept for minimized windows. Without a compositor they
  show each window's icon and title.
- Pinning from the menu or by dragging, reordering by dragging, and Undo
  after unpinning. Several docks share the same pinned applications.
- Right-click menus: the application's actions, recent files, pinning and
  closing; the standard window menu on Shift + right click; and About with
  the panel's items on Ctrl + right click.
- Horizontal and vertical panels of any size. On an expanded panel the dock
  fills the free room, with arrows when the buttons do not fit.

Not there yet: Super + number shortcuts, window cycling with the mouse
wheel, attention blinking, the Preferences window, the trash, and
translations.

## Building

Dependencies on FreeBSD and GhostBSD:

```sh
pkg install meson ninja pkgconf gettext-tools gtk3 mate-panel libwnck3 \
    libXcomposite libXdamage libXrender
```

Build, test, and install:

```sh
meson setup build
meson compile -C build
meson test -C build
sudo meson install -C build
```

On GhostBSD it can also be installed as a package with `pkg install mocka-dock`.

Then right click a MATE panel, choose "Add to Panel", and pick Mocka Dock.

The dock runs inside mate-panel, so after installing a new build restart the
panel with `mate-panel --replace &`.

## Checks

The code follows `.clang-format`. Two scripts run the static checks over
every source file, and both exit non-zero when they find something:

```sh
tools/tidy.sh       # clang-tidy, with the checks in .clang-tidy
tools/analyze.sh    # the Clang static analyser
```

clang-tidy is not in the FreeBSD packages: build `devel/llvm19` with the
EXTRAS option.

## Test data

`docs/test-data/` holds real window properties recorded on GhostBSD, which
the matcher tests run against. To record a new application, open it and run:

```sh
tools/collect-test-data.sh name expected-desktop-id
```

See [docs/test-data/README.md](docs/test-data/README.md) for the format.

## License

BSD-3-Clause. See [LICENSE](LICENSE).