# Mocka Dock Specification

Status: draft 1
Component: `mocka-dock`
License: BSD-3-Clause

## About this document

This document describes the behavior of Mocka Dock, a taskbar-style dock for the
MATE panel. It is the reference for the implementation and was written before
any code.

It was written from the Mocka project's own design decisions, from observed
behavior and public documentation of existing docks and taskbars, and from
public specifications (freedesktop.org, EWMH, X11). It contains no code or code
structure from any other project. Mocka Dock is an independent implementation.

## 1. Overview

Mocka Dock is a MATE panel applet that shows pinned and running applications
as a single row of buttons. Its interaction model follows the Windows taskbar:
one button per application, window thumbnails on hover, and an
application-focused right-click menu.

Platform: X11, FreeBSD first. Wayland is out of scope.

## 2. Terms

- **App**: an application, identified by its desktop entry (`.desktop` file)
  when one can be found.
- **Window**: a top-level window shown by the window manager.
- **Button**: the dock item that represents one app.
- **Pinned app**: an app the user has chosen to keep in the dock when it is
  not running.
- **Running app**: an app with at least one window open.
- **Focused window**: the window that currently has input focus.

## 3. Placement and size

- The dock works on any MATE panel, on any side of the screen, at any panel
  size.
- Button orientation follows the panel: a row on horizontal panels, a column
  on vertical panels.
- The dock sizes itself to the space the panel gives it and adapts when
  applets are added, removed, or moved.
- When there is not enough space for all buttons, the dock scrolls: hovering
  the first or last button shows an arrow and scrolls in that direction.
- Spacing between buttons is configurable.
- More than one dock may be added to the same or different panels. All docks
  share one list of pinned apps.

## 4. Layout

From start to end:

1. Menu button (optional, see section 14)
2. Separator, shown only when the menu button is enabled
3. App buttons
4. Separator, shown only when the trash is enabled
5. Trash (optional, see section 13)

The menu button and the trash stay fixed at the ends. Reordering happens only
within the app section. Separators are drawn by the theme and follow the
panel's orientation.

## 5. App buttons

- Pinned apps appear in the order the user arranged them.
- Running apps that are not pinned appear after the pinned apps, in the order
  they were started.
- All windows of the same app share one button.
- When the last window of an unpinned app closes, its button is removed.
- Pinning a running app keeps its button where it is.
- Windows that ask not to appear in taskbars (skip-taskbar) are not shown.
- By default, the dock shows windows from the current workspace only. A
  setting allows showing windows from all workspaces.

## 6. Identifying apps

Each window is assigned to an app by trying these in order, stopping at the
first match:

1. The window's WM_CLASS instance name against the `StartupWMClass` of the
   known desktop entries.
2. The window's WM_CLASS class name against `StartupWMClass`.
3. The instance name, then the class name, against desktop entry IDs
   (case-insensitive).
4. The startup notification ID, for windows of apps the dock launched itself.
5. Fallback: windows with the same class name are grouped together and shown
   with the window's own icon and title.

Requirements:

- The instance name is always tried before the class name. This is required
  for web apps from Chromium-based browsers (Chrome, Chromium, Brave, and
  others), which share the browser's class but have their own instance name
  matching their desktop entry.
- If a window changes its class after appearing, it is matched again.
- The list of desktop entries is read once, cached, and refreshed only when
  installed applications change.

## 7. Mouse

### Left click

| Situation | Result |
|---|---|
| App not running | Launch it |
| One window, not focused | Activate it |
| One window, focused | Minimize it |
| Several windows, thumbnails not showing | Show the thumbnails immediately |
| Several windows, thumbnails showing | Hide the thumbnails |

### Other mouse actions

| Action | Result |
|---|---|
| Middle click | Launch a new instance |
| Shift + left click | Launch a new instance |
| Ctrl + left click | Activate the app's next window in turn, without thumbnails |
| Mouse wheel over a button | Activate the app's next or previous window, including minimized windows |
| Right click on a button | App menu (section 9.1) |
| Shift + right click on a button | Window menu (section 9.2) |
| Ctrl + right click anywhere on the dock | Dock menu (section 9.3) |
| Right click on empty dock space | Dock menu (section 9.3) |

Actions that need a running app do nothing when the app is not running,
except Shift + right click, which then shows the app menu.

### Launch feedback

After launching an app, its button pulses until the app's first window
appears, or until a timeout of 15 seconds.

## 8. Thumbnails

### Showing and hiding

- Hovering a button of a running app shows a popup with a thumbnail of each of
  its windows after a fixed delay of 400 ms.
- While a popup is showing, moving the pointer to another button switches the
  popup to that app immediately, with no delay.
- When the pointer leaves the button and the popup, the popup hides after a
  grace period of about 300 ms, so the pointer can move from the button to the
  popup.
- Clicking a button cancels a pending hover popup.
- The delays are fixed and have no setting.

### Content

- Each thumbnail shows the window's contents, scaled down, with the window
  title below it. Long titles are shortened with an ellipsis.
- Each thumbnail has a close button, visible when the pointer is over it.
- For a minimized window, the thumbnail shows the last image captured before
  it was minimized. If there is none, it shows the app icon.
- The popup is placed next to the button, on the side facing the screen, and
  stays within the monitor that contains the button.
- The popup uses the panel's colors.

### Actions

- Clicking a thumbnail activates that window and hides the popup.
- Clicking the thumbnail of the focused window minimizes it.
- Clicking a thumbnail's close button closes that window. If it was the app's
  last window, the popup hides.

### Requirements

- Thumbnails work with any window manager, with or without a compositor.
- Thumbnails are captured and updated only while the popup is showing.

## 9. Menus

All menus are standard menus drawn by the theme.

### 9.1 App menu (right click)

From top to bottom:

1. The actions listed in the app's desktop entry, all of them.
2. Recent files for this app, if there are any.
3. The app's name, which launches a new instance.
4. Pin to dock, or Unpin from dock.
5. Close window, or Close all windows when the app has several windows. Not
   shown when the app is not running.

### 9.2 Window menu (Shift + right click)

The standard window menu for the app's most recently active window: minimize,
maximize, move, resize, always on top, workspace options, and close. Items
reflect the window's current state and what the window allows.

### 9.3 Dock menu (Ctrl + right click, or right click on empty space)

- Preferences
- About
- The panel's standard items: Move, Lock to Panel, Remove from Panel

## 10. Pinning

### Ways to pin

- Pin to dock, from the app menu of a running app.
- Pin to dock, from the right-click menu of an app in Mocka Menu.
- Dragging an app from a menu, the desktop, or the file manager onto the dock.
  It is inserted where it is dropped.

### Reordering

- Dragging a button within the app section moves it.
- Dragging a running app that is not pinned into the pinned area pins it.

### Unpinning

- Unpin from dock, from the app menu.
- After unpinning, the dock shows a small popup next to where the button was,
  reading "<App> unpinned" with an Undo button. It disappears after 5 seconds
  or when the user clicks elsewhere.
- Undo pins the app again at its previous position.

### Storage

- Pinned apps are stored as an ordered list of desktop entry IDs in a shared
  setting that other Mocka components can write to.
- Changes made by other components appear in the dock immediately.

## 11. Keyboard

| Shortcut | App |
|---|---|
| Super + 1 to 9, 0 | Apps 1 to 10 in the dock |
| Super + Alt + 1 to 9, 0 | Apps 11 to 20 in the dock |

Positions count app buttons only, not the menu button or the trash.

For the app at that position:

- Not running: launch it.
- One window: activate it, or minimize it if it is focused.
- Several windows: activate the next window in turn on each press.

If a shortcut is already taken by another program, the dock works without it.

## 12. Appearance and status

### Indicators

- Running apps show an indicator. Styles: bar (default), dots, or none.
- Optionally, one mark per open window, up to three.
- Indicators use the theme's highlight color.

### Active app

- The button of the app with the focused window is highlighted with the
  theme's highlight color.

### Attention

- When a window requests attention, its app's button shows a badge. The badge
  clears when the window is activated.

### Themes and scaling

- Icons follow icon theme changes immediately.
- Icons are sharp on HiDPI displays.

## 13. Trash (optional)

- Shown at the end of the dock when enabled. Off by default.
- The icon shows whether the trash is empty or full, and updates as files are
  added or removed.
- Dropping files on it moves them to the trash. Other dropped data is
  refused.
- Clicking it opens the trash in the file manager.
- Right click shows Open and Empty Trash. Empty Trash asks for confirmation.
- Works without GVfs, following the freedesktop.org Trash specification for
  the user's home trash.

## 14. Menu button (optional)

- Shown at the start of the dock when enabled. Off by default.
- Clicking it opens the Mocka Menu application menu.
- Available once the Mocka Menu shared library exists. Until then, the
  setting is hidden.

## 15. Drag and drop onto running apps

- Dragging data (files, text, and so on) over a running app's button and
  holding it there activates the app's window, so the data can be dropped into
  it.

## 16. Settings

### Shared, fixed path (`org.mocka_desktop.Dock`)

| Key | Meaning | Default |
|---|---|---|
| `pinned-apps` | Ordered list of pinned desktop entry IDs | GhostBSD's default set |

### Per dock (`org.mocka_desktop.Dock.Instance`, relocatable)

| Key | Meaning | Default |
|---|---|---|
| `indicator-style` | `bar`, `dots`, or `none` | `bar` |
| `indicator-per-window` | One mark per window, up to three | false |
| `icon-spacing` | Extra space between buttons | 0 |
| `show-all-workspaces` | Show windows from all workspaces | false |
| `show-trash` | Show the trash | false |
| `show-menu-button` | Show the menu button | false |

Preferences are edited in a separate preferences window, opened from the dock
menu. Changes apply immediately.

## 17. Performance requirements

- No periodic polling. The dock only does work in response to events.
- Zero CPU use when nothing on the desktop changes.
- The desktop entry list and window-to-app matches are cached.
- Icons are cached per size.
- Thumbnails are captured only while the thumbnail popup is showing.
- No interpreter runs in the applet process.

## 18. Dependencies and exclusions

The dock depends only on GTK 3, GLib/GIO, the MATE panel applet library,
libwnck, and X11 libraries (including the Composite and Damage extensions).

It does not use or provide any D-Bus service of its own, and does not depend on
BAMF, Keybinder, libunity, GVfs, or Python.

## 19. Not included

- Progress bars and counts on icons.
- Compositor-specific window previews.
- Changing panel colors.
- Popups with app actions on hover.
- Preset visual themes.

## 20. Planned for later

- Pinning apps to a specific workspace.
- Trash folders on other mounted volumes.
- A styled right-click popup in place of the standard menu.

## 21. Open questions

- Default for `show-all-workspaces`: current workspace only (as proposed) or
  all workspaces.
- GhostBSD's default `pinned-apps` set.
