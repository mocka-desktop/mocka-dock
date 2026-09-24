# Matcher test data

Real window properties recorded on GhostBSD, used by the matcher unit tests
(SPEC section 6). Record new samples with `tools/collect-test-data.sh`.

## Samples (`*.txt`)

One file per window. The first line is the desktop entry ID the matcher must
return, or `none` when the window must use the class fallback (step 6). The
rest is `xprop -notype` output for `WM_CLASS`, `_NET_STARTUP_ID`,
`WM_WINDOW_ROLE`, and `_NET_WM_WINDOW_TYPE`. A `leader` line holds the
`_NET_STARTUP_ID` of the window's `WM_CLIENT_LEADER`, where GTK apps put it
(`pluma.txt`).

When a sample was recorded with `-s`, the lines after `# WM_CLASS changes:`
list each `WM_CLASS` value the window had, in order. The matcher must follow
the last one.

## Desktop entries (`applications/`)

The desktop entries the matcher indexes during the tests, keeping only the
fields it reads. They include decoys found on real systems:

- `userapp-*`: hidden entries created by "Open With". They must never win
  over the real entry.
- LibreOffice with its GTK3 plugin (`libreoffice-gtk3.txt`) keeps the class
  `soffice` for every module, so it expects `none`. With the gen plugin
  (`libreoffice-gen.txt`) the class is `libreoffice-writer` and step 2
  matches. The fix belongs in LibreOffice, not in the dock; update the
  expectation when LibreOffice is fixed.
- `menulibre-pycharm.desktop`: a hand-made entry without `StartupWMClass`.
  PyCharm's window (`jetbrains-pycharm`) does not match it by SPEC section 6,
  so `pycharm.txt` expects `none`.

Home directories and host names are replaced with `/home/user` and `host`.