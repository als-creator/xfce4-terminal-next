# xfce4-terminal-next

A fork of [xfce4-terminal](https://gitlab.xfce.org/apps/xfce4-terminal) with
split panes and a few fixes. Everything is built on top
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
| Move pane up / down / left / right | unbound, divider can also be dragged with the mouse |

The rules the pane behaviour follows:

* the split to focus is found geometrically, not by tree position: every
  candidate is scored by its distance along the axis and by how well it is
  aligned on the perpendicular one;
* `Move pane` shifts the divider of the split the focused pane lives in by
  10px, keeping at least 35px on both sides;
* the focused pane *is* the active pane, there is no second source of truth.

The four `Move pane` actions ship without an accelerator and are not in the
menus: the divider can be dragged with the mouse, and the actions are there for
the keyboard shortcuts dialog, so that they do not fight with keys that full
screen applications use.

### Pane layout is remembered

The pane tree of every tab is serialized and restored on the next start, so the
panes come back the way they were left. A pane is `.`, a split is `H50,.,.`
(orientation, divider position in percent, then the two sides).

The record is written when the tree changes (split, pane or tab closed) and once
more just before the window is closed, so panes whose shell ended by itself do
not come back. *Settings → Session → Remember the pane layout of tabs* turns the
whole thing off: nothing is written and nothing is restored, so the terminal
always starts with a single pane in the home directory. The record that is
already stored is left alone, so switching the option back on brings the layout
of the last session back. The option is on by default.

### Tabs come back in their directories

The saved record of a tab carries the directory of every pane after the layout,
one escaped field per pane, so a restored tab is not just shaped the same way
but sits in the same directories. The directory of the first pane is applied
before its shell is started; the others when their pane is created.

The directory is the one the shell reports, so no shell integration is needed:
VTE is asked for the current directory of the pane, and the process' own
directory is used when the shell says nothing. A directory that no longer
exists is skipped, and a directory given on the command line or in the
preferences is never overwritten.

### tmux

* <kbd>Ctrl+Shift+M</kbd> runs the configured start command in the pane
  (default `tmux new-session -A -s main`), also available in the *Split* menu;
* the command is configurable in *Settings → tmux Launch Options*.

Note that the accelerators above are consumed by the terminal and therefore do
not reach tmux. Rebind them in the keyboard shortcuts dialog if a tmux binding
needs those keys.

### Drop-down status icon and residency

* The fork replaces upstream's *Show status icon in notification area* setting
  with *Drop-down → Always show the status icon (do not quit the process)*
  (`dropdown-keep-alive`, on by default). The icon is a StatusNotifierItem
  instead of upstream's `GtkStatusIcon`, so it works in a Wayland session too,
  and it stays as long as the process lives.
* With it enabled the panel is kept alive: when the shell of its
  last tab ends (e.g. <kbd>Ctrl+D</kbd>), a fresh tab is opened and the panel is
  hidden instead of the window (and the icon) being destroyed, so the process
  stays; the next toggle (the panel shortcut, see below) shows the fresh tab.
  Closing the window on purpose (*Close Window* in the icon menu, the window's
  own close button) still quits the application.
* With the setting off the behaviour is exactly upstream's: no icon, and the
  window and the application go away with their last tab. A normal (non
  drop-down) window always keeps upstream behaviour.

### Tray icon: StatusNotifierItem with a GtkStatusIcon fallback

The icon is a StatusNotifierItem (`org.kde.StatusNotifierItem` on the session
bus, `terminal-status-notifier.c`), which is what Xfce, KDE and the GNOME
extension of AppIndicator understand, in an X11 session as well as in a
Wayland one. It is a plain D-Bus interface, so nothing KDE is linked in: the
dependencies of the package do not change.

* left click (`Activate`) shows or hides the panel, the menu is
  `com.canonical.dbusmenu` (`/MenuBar`) with *Preferences* and *Close Window*
  and is drawn by the host — a menu of our own would need a popup that does
  not exist under Wayland;
* `terminal_status_notifier_is_supported()` looks for
  `org.kde.StatusNotifierWatcher` on the bus. Without one (GNOME without the
  extension, a bare window manager) and in an X11 session the icon falls back
  to upstream's `GtkStatusIcon`, so the tray never simply disappears;
* the watcher may show up after the panel does (a panel plugin, a session
  start), so the item registers itself as soon as it appears, and registers
  again if a new watcher replaces the old one.

The stand `tests/sni-test.sh` runs a `StatusNotifierWatcher` of its own on a
private bus and checks the registration, the item properties, the menu layout,
`Activate` and a menu click.

### Drop-down terminal as a separate mode

* The panel is a mode of its own, started explicitly with
  `xfce4-terminal --drop-down` (from a session autostart, a launcher, or a
  system shortcut). There is no setting that turns ordinary launches into a
  panel: an ordinary start — including *Open Terminal Here* in Thunar, which
  calls the terminal without any flags — always opens an ordinary window, and
  an ordinary window never lands in the panel: the panel is a daemon with its own
  client, and its window is never the "normal" terminal of the desktop.
* *Drop-down → Toggle the window when started again* (`dropdown-parameters-once`)
  makes a second `--drop-down` start show or hide the panel instead of opening
  a new tab in it, which is what a system shortcut needs.

### Panel shortcut: a system shortcut, no key grab

Upstream has no global shortcut for the panel at all: it is shown by running
`--drop-down` again or by its status icon. The fork used to grab a key on the X
server root window (`XGrabKey()`, default <kbd>F1</kbd>), which is X11 only — a
Wayland client never sees the keys of other windows — and it was the only X11
piece the fork added to the panel. The grab is gone:

* *Drop-down → Use a system shortcut to show and hide the panel* registers
  `xfce4-terminal-next --drop-down` as an application shortcut in the
  `xfce4-keyboard-shortcuts` channel, exactly what *Settings → Keyboard
  Shortcuts → Application Shortcuts* writes, and *Change shortcut…* asks for
  the combination in a dialog of its own (a Wayland client cannot grab keys,
  so the key is read from the focused dialog instead of from the server);
* unchecking the box removes the entry again, a combination that is already
  taken by another command is reported before it is replaced;
* the checkbox and the shown combination are read back from the desktop
  configuration, so a shortcut bound by hand is recognised and reported as one,
  not registered twice.

The shortcut is the only way to show the panel on Wayland, and on X11 it also
removes the double toggle a grab plus a system shortcut would produce.

### Panel starts out of sight

* *Drop-down → Start hidden* (`dropdown-start-hidden`, off by default) creates
  the panel without showing it, so a session autostart does not cover the
  desktop at login; the shortcut shows it afterwards. Reusing an existing panel
  is a request to use the panel and always shows it.

### Showing and hiding the panel over D-Bus

* `org.xfce.TerminalNextLocal.ToggleDropDown` (new) shows or hides the panel in
  the running instance, creating it if it does not exist yet, without starting
  a new process. It is what a compositor or desktop shortcut should call, where
  running the binary again would be the alternative. Unlike `Launch` it does
  not compare `DISPLAY`: the method is local to the instance that owns the
  service, and under Wayland `DISPLAY` is not set at all.
* The panel is looked up by type only, without the screen comparison the launch
  path does for bug #9957 — on Wayland there are no screens to compare.

### Accelerators under non-latin keyboard layouts

Accelerators are stored as latin keysyms, but on a russian keyboard
<kbd>Ctrl+Shift+O</kbd> produces a cyrillic keysym, so none of them match and
the hotkeys look dead. A key press is mapped back to the latin symbol of the
same physical key: the symbol is
taken from the first group of the layout with `gdk_keymap_translate_keyboard_state()`
(xkb always keeps the us layout in group 0) at level 0, so it neither depends on
the group the switch moved to nor on the shift it swallowed.

The remap carries every other modifier over as it is, so key presses with
<kbd>Alt</kbd>, <kbd>Ctrl+Alt</kbd>, <kbd>Super</kbd> or <kbd>AltGr</kbd> are
normalized the same way as the ones with <kbd>Ctrl</kbd> — only the latin symbol
is looked up, never the modifier combination.

Only key presses that really are layout dependent are remapped, so nothing
changes on a latin layout, and a rebound accelerator from the keyboard
shortcuts dialog is honoured. Keys without a latin symbol (tab, arrows, function
keys) are left to the accelerators as they are.

When <kbd>Ctrl+Shift</kbd> is the layout switch itself
(<kbd>grp:ctrl_shift_toggle</kbd>), xkb consumes the shift: it changes the group
and never reaches the event, so <kbd>Ctrl+Shift+O</kbd> looks exactly like
<kbd>Ctrl+O</kbd> and the accelerator that wants shift misses. The handler asks
the X server for the real state of the physical keys (`XQueryKeymap()`) and
restores the shift, so <kbd>Ctrl+O</kbd> and <kbd>Ctrl+Shift+O</kbd> keep their
own meanings. Depending on the direction of the switch the key press carries
either the cyrillic symbol (the group went to the non-latin one) or the latin
one (the group went back); both are handled. Keys without a latin symbol
(<kbd>Ctrl+Shift+Tab</kbd>, <kbd>Ctrl+Shift+Up</kbd>) only miss the modifier,
so there the key press is kept and the modifier is put back into the event
itself — this keeps the tab handler, the menu bindings and the terminal widget
consistent as well. The query runs only for key presses that carry another
modifier (the modifier keys themselves and the group switch keysym they turn
into, `ISO_Next_Group`, stay untouched), and only on X11; on Wayland the
previous behaviour stays. Verified with a live stand
(`tests/keys-live-test.sh`) on a private display: the shift is really swallowed
there and the accelerator still fires.

### Clipboard slots

The system clipboard holds one string. Copying a second thing before pasting
the first one loses the first one, and that happens a lot when a path from one
pane is needed while pasting a line from another. The fork keeps a few slots of
its own next to the system clipboard:

* <kbd>Ctrl+Alt+C</kbd> puts the **selection** into the current slot and moves
  on to the next slot, in turns. With nothing selected nothing is copied and the
  slot number does not move.
* <kbd>Ctrl+Alt+V</kbd> pastes what is in the current slot. The cursor stays
  where it is, so the text lands in the command line the way an ordinary paste
  puts it there.

The point of the slots is to take one string, go to another tab for a second one
and come back: <kbd>Ctrl+Alt+C</kbd> puts the first into slot 1,
<kbd>Ctrl+Alt+C</kbd> in the other tab puts the second into slot 2, and
<kbd>Ctrl+Alt+V</kbd> back in the first tab brings up the first string while a
second press brings up the second.

Everything about them sits on «General» → «Clipboard», behind one checkbox:

* **how many** slots there are — from 2 to 6, three by default. The count is a
  setting and not a constant, because the useful number is the number of places
  a string is carried between; the entries in the menu are numbered by it. Lower
  it and the extra slots are dropped, raise it and the new ones start empty;
* **what is put into a slot when copying**: the whole selected **text**, only
  the **links** found in it, or its **HTML** as the application offered it. The
  choice is one for all slots, because a slot holds one string and not a
  document; a copy that finds nothing (no link in the selection, for instance)
  leaves the slot as it was instead of emptying it;
* **copying** and **pasting** are enabled apart from each other, so a shortcut
  can be given to one direction only;
* **a nested submenu**: with it off the slots are flat entries, one per slot,
  with the count in the label; with it on the two directions become entries with
  a submenu of their own, «Slot 1», «Slot 2» and so on;
* **keep the slots in a file**: with it on the contents are written when the
  last window is closed and read back at the next start, so the slots outlive
  the session. The path is a field of its own, empty meaning a file under the
  configuration directory; the file is a key file, one line per slot, so a
  string with newlines in it comes back whole.

The same two things are in the menu by name — «Copy to Clipboard Slot 1/2/3» and
«Paste from Clipboard Slot 1/2/3» — in «Edit» and in the context menu, so a slot
can be picked without counting presses. The current slot is marked in the
«Edit» list, and a slot that holds nothing is greyed out there.

The slots belong to the application and not to a window: a string copied in one
window is pasted in another. Nothing is written to disk unless the file setting
is on.

The copy goes through the system clipboard, because Vte offers no other way to
read the selection. A trailing dot or bracket that belongs to the sentence
rather than to a URL is not part of a link either — that only matters for the
link list of «Copy As...».

### Fixes on top of upstream

* the pane layout was stored as a `G_TYPE_STRV` built from a `GPtrArray`
  without a `NULL` terminator, so xfconf copied it with `g_strdupv()` and read
  past the end of the array: closing the last pane of the panel crashed with
  SIGSEGV in `terminal_window_save_panes_layouts()`. The array is terminated
  now, like `terminal_panes_session_cdirs()` right above it;
* `terminal_window_setup_screen()` took a `TerminalWidget *` and was called
  with `TERMINAL_WIDGET (screen)`, but `TerminalScreen` derives from
  `GtkOverlay`, not from `TerminalWidget`: every split and every restored pane
  logged `invalid cast from 'TerminalScreen' to 'TerminalWidget'` and passed
  `NULL` on. The parameter is a `TerminalScreen *` now;
* closing a pane whose child process exits collapses the split and keeps the
  tree consistent, instead of leaving a grey area behind;
* the last tab of the drop-down window is replaced by a fresh one and the panel
  is hidden instead of the window (and the status icon) being destroyed, see
  above;
* the keyboard grab probe of the drop-down window is only run on X11, on other
  backends it failed forever and kept rescheduling itself;
* <kbd>Ctrl+Shift+&lt;key&gt;</kbd> keeps working on a non-latin layout when
  <kbd>Ctrl+Shift</kbd> is the layout switch: xkb swallows the shift there, and
  the layout normalization of the fork now looks up the real state of the
  physical key instead of trusting the event (upstream has no layout support at
  all, so its latin accelerators simply stay dead on such layouts).

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

or on GitHub: <https://github.com/als-creator/xfce4-terminal-next/compare/master...feature/split-panes>

## Building

Arch, with the package in this repository's root:

```
sudo pacman -U xfce4-terminal-next-<version>-x86_64.pkg.tar.zst
```

The package installs to `/usr/local`, the binary is `xfce4-terminal-next`, the
D-Bus name is `org.xfce.TerminalNextLocal`, and removing the package removes
everything again.

From source:

```
meson setup build
meson compile -C build
```

## Translations

Russian is complete (460 messages, no fuzzy), since the fork is used with
`LANG=ru_RU.UTF-8`.
