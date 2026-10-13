# xfce4-terminal-local

A fork of [xfce4-terminal](https://gitlab.xfce.org/apps/xfce4-terminal) with
split panes, guake-style residency and a few fixes. Everything is built on top
of upstream master; no upstream file is rewritten from scratch.

This document exists so that a reviewer can see what the fork does without
installing it. The changes worth discussing upstream live in separate branches,
see [Upstream-ready branches](#upstream-ready-branches).

## What the fork adds

### Split panes

Native split panes inside a tab, built as a tree of `GtkPaned` in a
`TerminalPanes` container (`terminal/terminal-panes.c`).

| Action | Accelerator |
| --- | --- |
| Split horizontally | <kbd>Ctrl+Shift+H</kbd> |
| Split vertically | <kbd>Ctrl+Shift+E</kbd> |
| Close pane | <kbd>Ctrl+Shift+X</kbd> |
| Focus pane up / down / left / right | <kbd>Alt+↑</kbd> <kbd>Alt+↓</kbd> <kbd>Alt+←</kbd> <kbd>Alt+→</kbd> |
| Next / previous pane | <kbd>Ctrl+Tab</kbd> <kbd>Ctrl+Shift+Tab</kbd> |
| Move pane up / down / left / right | unbound, see below |

Ported from Guake, which does the same thing:

* the split to focus is found geometrically, not by tree position: every
  candidate is scored by its distance along the axis and by how well it is
  aligned on the perpendicular one;
* `Move pane` shifts the divider of the split the focused pane lives in by
  10px, keeping at least 35px on both sides (`SplitMover` in Guake);
* the focused pane *is* the active pane, there is no second source of truth.

The four `Move pane` actions ship without an accelerator on purpose: they are
in the *Split* menu and can be bound in the keyboard shortcuts dialog, so they
do not fight with keys that full screen applications use.

### Pane layout is remembered

The pane tree of every tab is serialized and restored on the next start, so the
panes come back the way they were left. A pane is `.`, a split is `H50,.,.`
(orientation, divider position in percent, then the two sides).

### tmux

* <kbd>Ctrl+Shift+M</kbd> runs the configured start command in the pane
  (default `tmux new-session -A -s main`), also available in the *Split* menu;
* the command is configurable in *Settings → tmux Launch Options*.

Note that the accelerators above are consumed by the terminal and therefore do
not reach tmux. Rebind them in the keyboard shortcuts dialog if a tmux binding
needs those keys.

### Status icon and guake-style residency

* *Settings → Status Icon → Show a status icon in the notification area*
  (on by default in the fork). With it enabled the process stays alive when the
  last window is closed, closing a window hides it, and the application is quit
  from the tray menu.
* `GtkStatusIcon` only exists on X11, so the whole tray mode switches itself off
  on Wayland — otherwise the application could hide its windows with no way to
  reach it again.

### Drop-down terminal as a setting

* *Settings → Status Icon → Use the drop-down terminal* makes the application
  start as a drop-down panel, like `xfce4-terminal --drop-down` does. An
  explicit `--drop-down` on the command line still wins.
* *Drop-down → Toggle the window when started again* (`dropdown-parameters-once`)
  makes a second start show or hide the panel instead of opening a new tab in
  it, which is what a system shortcut needs.
* <kbd>F1</kbd> shows and hides a drop-down window (a normal window keeps the
  upstream binding and opens the help).

### Accelerators under non-latin keyboard layouts

Accelerators are stored as latin keysyms, but on a russian keyboard
<kbd>Ctrl+Shift+O</kbd> produces a cyrillic keysym, so none of them match and
the hotkeys look dead. A key press is mapped back to the latin symbol of the
same physical key, like Guake does in `Keybindings.activate()`, which works for
every layout because xkb always keeps the us layout in group 0.

Only key presses that really are layout dependent are remapped, so nothing
changes on a latin layout, and a rebound accelerator from the keyboard
shortcuts dialog is honoured.

### Fixes on top of upstream

* closing a pane whose child process exits collapses the split and keeps the
  tree consistent, instead of leaving a grey area behind;
* the last tab of a window is never dropped from the notebook: a fresh tab is
  created in an idle, so the window is never left empty;
* the status icon keeps the application alive, see above;
* the keyboard grab probe of the drop-down window is only run on X11, on other
  backends it failed forever and kept rescheduling itself.

## Upstream-ready branches

The fork is not a proposal to merge everything. These branches are meant to be
reviewed on their own:

| Branch | What it is |
| --- | --- |
| `upstream/pr-layout-accel` | only `terminal-keys.c`, plus the small hook in `terminal-window.c`. Layout independent accelerators, ~280 lines, no dependency on any other fork feature. |
| `master` | plain upstream, so that `master...feature/split-panes` is a readable diff of the whole fork |

Compare the fork with upstream:

```
git diff master...feature/split-panes
```

or on GitHub: <https://github.com/als-creator/xfce4-terminal-local/compare/master...feature/split-panes>

## Building

Arch, with the package in this repository's root:

```
sudo pacman -U xfce4-terminal-local-<version>-x86_64.pkg.tar.zst
```

The package installs to `/usr/local`, the binary is `xfce4-terminal-local`, the
D-Bus name is `org.xfce.Terminal5Local`, and removing the package removes
everything again.

From source:

```
meson setup build
meson compile -C build
```

## Translations

Russian is complete (444 messages, no fuzzy), since the fork is used with
`LANG=ru_RU.UTF-8`.
