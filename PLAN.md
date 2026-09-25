# Mocka Dock: implementation plan

Behavior is defined in `SPEC.md`. This file covers how it is built and in what
order. Tick items as they are completed.

## Dependencies

`gtk+-3.0`, `libmatepanelapplet-4.0`, `libwnck-3.0`, `gio-unix-2.0`, `x11`,
`xcomposite`, `xdamage`, `cairo-xlib`.

## Module layout

| Module | Role |
|---|---|
| `src/mocka-dock-applet.c` | Applet factory, panel orientation and size, dock menu |
| `src/app-index.c` | Desktop entry index, lookup by ID and `StartupWMClass`, refresh on app changes |
| `src/matcher.c` | Window-to-app matching chain (SPEC section 6) |
| `src/window-tracker.c` | libwnck signals, assigns windows to apps |
| `src/dock-model.c` | Ordered app list (pinned and running), synced with `pinned-apps` |
| `src/dock-button.c` | App button: icon, indicators, badge, launch pulse, input handling |
| `src/app-menu.c` | App menu and window menu |
| `src/thumbnails.c` | Composite capture, snapshot cache, thumbnail popup |
| `src/keybindings.c` | Super + number shortcuts, grab ownership across docks through an X manager selection |
| `src/trash.c` | Trash item |
| `src/undo-popup.c` | Unpin popup with Undo |
| `data/` | GSettings schemas, `.mate-panel-applet` file, applet service file |
| `tests/` | Unit tests |
| `docs/test-data/` | Real `WM_CLASS` samples from GhostBSD |

Schemas: `org.mocka_desktop.Dock` (fixed path, shared) and
`org.mocka_desktop.Dock.Instance` (relocatable, per dock).

The applet runs out of process, so a crash in the X11 capture or grab code
does not take the panel down with it.

## M0: Skeleton and risk checks

- [x] Meson build, `data/` files, both schemas
- [x] gettext setup from the start: `po/` directory, all user-visible strings wrapped in `_()`
- [x] Minimal out-of-process applet that appears in "Add to Panel" as Mocka Dock
- [x] Verify right click on a button is consumed and Ctrl + right click reaches the panel's applet menu
- [x] Verify Shift + right click can show a `WnckActionMenu`
- [x] Verify one window can be captured through Composite from the applet process with marco's compositor on, and that a missing compositor can be detected
- [x] Verify Super + number can be grabbed alongside marco and the Super key menu shortcut, with NumLock and CapsLock on or off
- [x] `docs/test-data/` with `xprop WM_CLASS` output for Firefox, Chromium plus a web app, LibreOffice, Caja, a terminal, an Electron app, a Wine app, a Java app, and a Qt app
- [x] `docs/test-data/` also covers a window that changes its class after mapping (LibreOffice) and a GTK app launched with startup notification. No recorded app changed its class (LibreOffice with GTK3 keeps `soffice`), so M1 tests rematching with made-up class changes
- [x] Port skeleton in the GhostBSD ports overlay (draft only, kept in a `ghostbsd-ports` stash until the M3 alpha)
- [x] Manual test by maintainer

## M1: Core taskbar

- [x] App index and matcher, with unit tests against `docs/test-data/`
- [x] Rematch a window when its class changes
- [x] Window tracker and dock model for running apps, grouped per app, unpinned apps in start order
- [x] Skip-taskbar windows are not shown
- [x] Horizontal and vertical panels, following orientation and size changes
- [x] Buttons with icons, following icon theme changes and HiDPI (HiDPI checked at GDK_SCALE=2, not on a HiDPI screen)
- [x] Left click behavior, with a plain window list for several windows
- [x] Icon geometry on each window, so minimize and restore animations go to its button (SPEC section 12)
- [x] Middle click and Shift + click for a new instance
- [x] Bar indicator and active app highlight in theme colors
- [x] Current-workspace filtering and `show-all-workspaces`, with unit tests for the model
- [x] Manual test by maintainer

## M2: Pinning

- [x] `pinned-apps` storage and live updates
- [x] Pinned apps that are not running, launching them
- [ ] Startup notification ID matching (SPEC section 6, step 5)
- [ ] Pinned app with windows only on other workspaces: click switches to its window (SPEC section 5)
- [ ] Pin and unpin from the app menu, undo popup
- [ ] Drag to reorder, drag to pin, dropped `.desktop` files copied to the user's applications folder, with unit tests for the copy
- [ ] Two docks share the pinned list and show the same apps in the same order
- [ ] Launch pulse with startup notification
- [ ] Choose GhostBSD's default pinned set
- [ ] Manual test by maintainer

## M3: Menus (first alpha)

- [ ] App menu: desktop actions, recent files, new instance, pin, close
- [ ] Window menu on Shift + right click
- [ ] Dock menu on Ctrl + right click and empty space. Preferences stays hidden until the window exists (M7); settings are changed with `gsettings` meanwhile
- [ ] Auto-resize and overflow arrows
- [ ] Manual test by maintainer
- [ ] Add `x11/mocka-dock` to ghostbsd-ports from the stashed draft, pointing at the alpha
- [ ] Alpha release for GhostBSD testers

## M4: Thumbnails

- [ ] Thumbnail popup with hover timings from SPEC section 8
- [ ] Composite capture with a compositor, icon and title fallback without one
- [ ] Snapshots on focus loss and before the dock minimizes a window
- [ ] Damage updates only while the popup is showing
- [ ] Thumbnail actions: switch, minimize focused, close
- [ ] Manual test with the compositor on and off
- [ ] Collect feedback on the multiple-window click behavior

## M5: Remaining core features

- [ ] Super + number shortcuts, with unit tests for mapping a shortcut to a dock position. Also accept synthetic Super + number presses on the root window: a menu bound to Super alone (Brisk Menu) holds the keyboard while Super is down and re-sends other keys that way (found in M0)
- [ ] Shortcut ownership across docks: the owner holds an X manager selection, other docks watch it and take over when the owner goes away
- [ ] Mouse wheel and Ctrl + click window cycling, Ctrl + click launching an app that is not running
- [ ] Attention: three blinks in the highlight color, then a badge until the window is activated
- [ ] Drag data onto a running app activates it
- [ ] Icon spacing
- [ ] Dot indicators and per-window indicators (up to four marks, on by default: change the `indicator-per-window` schema default to true)
- [ ] Manual test by maintainer

## M6: Optional items

- [ ] Trash item and its separator: freedesktop.org Trash spec without GVfs, icon updated through a file monitor, confirmation before emptying
- [ ] Separator logic for the menu button (the button itself waits for the Mocka Menu library)
- [ ] Manual test by maintainer

## M7: Release 0.1

- [ ] Preferences window
- [ ] Translations: English and French (gettext set up in M0)
- [ ] Man page
- [ ] README update
- [ ] Performance check: RSS, idle CPU, 20+ windows
- [ ] Tag 0.1, update the GhostBSD port, submit `x11/mocka-dock` to FreeBSD ports
