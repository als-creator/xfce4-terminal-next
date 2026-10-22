# xfce4-terminal-next

A fork of [xfce4-terminal](https://gitlab.xfce.org/apps/xfce4-terminal) with
split panes inside a tab, a drop-down panel with a system shortcut, a tmux
launch, clipboard slots, session restore and layout independent accelerators.
Everything is built on top of upstream `master`; no upstream file is rewritten
from scratch.

[![License](https://img.shields.io/badge/License-GPL%20v2%2B-blue.svg)](COPYING)

## Layout independent hotkeys

**The headline change of the fork: the hotkeys work with a layout switcher.**
With `grp:ctrl_shift_toggle` or `grp:alt_shift_toggle` the switcher eats one
of the modifiers of its own chord — the one that was pressed second — and the
key press that follows arrives without it. A shortcut that needs that modifier
never fires, and it looks as if the shortcut itself was broken. The fork gives
the modifier back, so <kbd>Ctrl+Shift+<letter></kbd> works on a Russian,
Hebrew or any other layout, with or without a layout switcher.

### The problem is not terminal specific

The switcher sits below the toolkit: the key press arrives with an incomplete
modifier set, and every application that matches its shortcuts against that
set loses the same shortcuts. The desktop side needs the same treatment, and
this fork is meant to be the working example of it: the branch `pr-layout-accel`
(MR !121) is the compensation alone (`terminal/terminal-keys.c` plus a small
hook in `terminal/terminal-window.c`), kept separate so that the approach can
be reviewed and reused for the shortcut handling of Xfce and GTK themselves.

What does *not* need fixing is the layout itself: GTK3 matches an accelerator
against the physical key and resolves the keysym over all groups of the
layout, so `Ctrl+Shift+T` fires on a Russian layout as well. That was
measured on a clean upstream build, not only in the fork.

The fork ran into the same wall itself: the dialog that registers the panel's
system shortcut stored the raw keystroke, so on a Russian layout it wrote
`<Primary>0x442` into the settings — a name `gtk_accelerator_parse()` cannot
read back (it returns `0x0`). The keystroke is normalized there too now, but
the case shows how far the text form of a shortcut reaches: it is not only
the matching in one application that has to know about layouts.

<details>
<summary>Technical implementation</summary>

* the layout switch eats a modifier — <kbd>Ctrl+Shift</kbd>
  (`grp:ctrl_shift_toggle`) eats the Shift or the Control, <kbd>Alt+Shift</kbd>
  (`grp:alt_shift_toggle`) eats the Shift or the <kbd>Alt</kbd> — and the key
  press that follows arrives without it in the state. The handler asks the X
  server for the real state of the physical keys (`XQueryKeymap()`) and puts
  the modifier back, so <kbd>Ctrl+O</kbd> and <kbd>Ctrl+Shift+O</kbd> keep
  their own meanings; the query runs only for presses that carry another
  modifier and only on X11 — on Wayland the previous behaviour stays;
* the fork matches its own table of layout independent hotkeys by keysym, so
  a key press is mapped back to the latin symbol of the same physical key:
  the symbol is taken from the first group of the layout (xkb always keeps
  the us layout in group 0) at level 0 with
  `gdk_keymap_translate_keyboard_state()`, which makes it independent of the
  group the switch moved to and of the shift it swallowed
  (`terminal/terminal-keys.c`);
* every other modifier is carried over as it is, so <kbd>Alt</kbd>,
  <kbd>Ctrl+Alt</kbd>, <kbd>Super</kbd> and <kbd>AltGr</kbd> presses are
  normalized the same way; keys without a latin symbol (Tab, arrows, function
  keys) are left to the accelerators as they are, and a rebound accelerator
  from the keyboard shortcuts dialog is honoured;
* verified with the live stand `tests/keys-live-test.sh` on a private display:
  the Shift really is swallowed there and the accelerator still fires.
</details>

## What the fork adds

One item — one feature and what it does. The technical implementation of each
item is behind it.

### Split panes

A tab holds a tree of panes: <kbd>Ctrl+Shift+H</kbd> and
<kbd>Ctrl+Shift+E</kbd> split, <kbd>Ctrl+Shift+X</kbd> closes a pane,
<kbd>Alt</kbd>+arrows focus the pane in that direction, <kbd>Ctrl+Tab</kbd> /
<kbd>Ctrl+Shift+Tab</kbd> go to the next / previous pane.

<details>
<summary>Technical implementation</summary>

* a tree of `GtkPaned` in a `TerminalPanes` container
  (`terminal/terminal-panes.c`);
* the pane to focus is found geometrically, not by the position in the tree:
  every candidate is scored by its distance along the split axis and by how
  well it is aligned on the perpendicular one;
* *Move pane* shifts the divider of the split the focused pane lives in by
  10px, keeping at least 35px on both sides; it ships without an accelerator
  and is not in the menus — the divider can be dragged with the mouse, and the
  actions are there for the keyboard shortcuts dialog so that they do not
  fight with keys full screen applications use;
* the focused pane *is* the active pane: there is no second source of truth;
* closing a pane whose shell ended collapses the split and keeps the tree
  consistent instead of leaving a grey area behind.
</details>

### The split layout of tabs is remembered

Tabs come back with the same split tree and in the same directories;
*Settings → Session → Remember the pane layout of tabs* turns it off, and the
terminal then always starts with a single pane.

<details>
<summary>Technical implementation</summary>

* a pane is `.`, a split is `H50,.,.` — orientation, divider position in
  percent, then the two sides;
* the record is written when the tree changes (split, pane or tab closed) and
  once more just before the window closes, so panes whose shell ended by
  themselves do not come back;
* the saved record carries the directory of every pane after the layout, one
  escaped field per pane: the directory is the one the shell reports, asked of
  VTE without any shell integration, and the process' own directory is used
  when the shell says nothing; a directory that no longer exists is skipped,
  and a directory given on the command line or in the preferences is never
  overwritten;
* the record that is already stored is left alone when the option is off, so
  switching it back on brings the layout of the last session back.
</details>

### tmux

<kbd>Ctrl+Shift+M</kbd> (or the *Split* menu) starts the configured tmux line
in the pane — by default
`TERM=tmux-256color tmux new-session -A -s main \; set -g allow-passthrough on
\; set -g window-size latest`: 256 colours and true colour inside, the session
called `main`, and the pane sizing that leaves no scrollbars behind. The field
in *Settings → tmux Launch Options* is pre-filled with this line, so it can be
shortened or replaced. The built-in line is typed with a leading space, so
that it stays out of the shell history (bash with `HISTCONTROL=ignorespace`,
zsh with `HIST_IGNORE_SPACE`, fish out of the box); a command of your own is
typed as it is.

<details>
<summary>Technical implementation</summary>

* one field in the preferences, one line out: whatever the user wrote is used
  as it is, kept apart from the window so that it can be tested without a
  display (`terminal/terminal-tmux.c`);
* the session outlives the window: closing the terminal leaves the tmux session
  running for the next attach;
* the terminal consumes `Ctrl+Shift+<letter>` accelerators itself, so they
  never reach tmux — rebind them in the keyboard shortcuts dialog if a tmux
  binding needs those keys;
* *Settings → Command to start tmux* warns with an info bar while the command
  calls tmux and tmux is not installed.
</details>

### Drop-down panel as a separate mode

`xfce4-terminal-next --drop-down` starts (or toggles) a daemon panel; it is
shown and hidden by a system shortcut, can start hidden, and a second start
can toggle it instead of opening a new tab.

<details>
<summary>Technical implementation</summary>

* there is no setting that turns ordinary launches into a panel: an ordinary
  start — including *Open Terminal Here* — always opens an ordinary window,
  and an ordinary window never lands in the panel; the panel is a daemon with
  its own client;
* *Drop-down → Use a system shortcut to show and hide the panel* registers
  `xfce4-terminal-next --drop-down` as an application shortcut in the
  `xfce4-keyboard-shortcuts` channel — exactly what *Settings → Keyboard
  Shortcuts → Application Shortcuts* writes; *Change shortcut…* asks for the
  combination in a dialog of its own, because a Wayland client cannot grab
  keys and the key is read from the focused dialog instead of from the server;
* a combination already taken by another command is reported before it is
  replaced, and a shortcut bound by hand is recognised from the desktop
  configuration, not registered twice; the X11 key grab (`XGrabKey()`) the
  fork used before is gone — it was the only X11-only piece of the panel;
* *Drop-down → Toggle the window when started again* makes a second
  `--drop-down` start show or hide the panel; *Drop-down → Start hidden*
  creates the panel without showing it, so an autostart does not cover the
  desktop at login;
* `org.xfce.TerminalNext.ToggleDropDown` on the session bus shows or hides
  the panel of the running instance, creating it if there is none, without
  starting a new process and without comparing `DISPLAY` (under Wayland
  `DISPLAY` is not set at all);
* the shortcut is the only way to show the panel on Wayland, and on X11 it
  removes the double toggle a grab plus a system shortcut would produce.
</details>

### Tray icon on X11 and Wayland

A tray icon shows and hides the panel; with *Drop-down → Always show the status
icon* the process stays alive — when the last shell ends, the next toggle opens
a fresh tab instead of the application quitting.

<details>
<summary>Technical implementation</summary>

* a `StatusNotifierItem` (`org.kde.StatusNotifierItem` on the session bus,
  `terminal/terminal-status-notifier.c`) with the menu
  `com.canonical.dbusmenu` (`/MenuBar`) drawn by the host — a plain D-Bus
  interface, nothing KDE is linked in and the package dependencies do not
  change; the host draws the menu because a popup of our own does not exist
  under Wayland;
* `terminal_status_notifier_is_supported()` looks for
  `org.kde.StatusNotifierWatcher`; without one (GNOME without the extension, a
  bare window manager) the icon falls back to upstream's `GtkStatusIcon`, so
  the tray never simply disappears; the watcher may show up after the panel
  does, so the item registers as soon as it appears and again if a new watcher
  replaces the old one;
* left click (`Activate`) toggles the panel; *Preferences* and *Close Window*
  are in the menu; *Close Window* still quits, but the shell of the last tab
  ending does not — the panel is hidden and a fresh tab waits;
* the stand `tests/sni-test.sh` runs a `StatusNotifierWatcher` of its own on a
  private bus and checks the registration, the properties, the menu layout,
  `Activate` and a menu click.
</details>

### Clipboard slots

The system clipboard holds one string; the slots hold a few more:
<kbd>Ctrl+Alt+C</kbd> puts the selection into the current slot and moves on,
<kbd>Ctrl+Alt+V</kbd> pastes it. The slots are in *Edit* and in the context
submenu by name, the one used last is the one <kbd>Ctrl+Alt+V</kbd> pastes,
empty ones are greyed out.

<details>
<summary>Technical implementation</summary>

* the count is a setting, from 2 to 10 slots (three by default): the useful number
  is the number of places a string is carried between, and the menu entries
  are numbered by it; lowering the count drops the extra slots, raising it
  starts the new ones empty;
* what is put into a slot when copying is one setting for all slots: the whole
  selected **text**, only the **links** found in it, or its **HTML**; a copy
  that finds nothing leaves the slot as it was instead of emptying it;
* copying and pasting are enabled apart from each other, and the two
  directions can be a nested submenu instead of flat entries;
* *Keep the slots in a file* writes the contents when the last window closes
  and reads them back at the next start: a key file, one line per slot, path
  configurable, created with rights `0600` because a slot can hold a password;
* the slots belong to the application, so a string copied in one window is
  pasted in another; nothing is written to disk unless the file setting is on;
* the copy goes through the system clipboard, because VTE offers no other way
  to read the selection.
</details>

### Fixes on top of upstream

* the pane layout was stored as a `G_TYPE_STRV` built without a `NULL`
  terminator, so xfconf read past the end of the array and closing the last
  pane of the panel crashed with SIGSEGV; the array is terminated now;
* `terminal_window_setup_screen()` was called with a
  `TERMINAL_WIDGET (screen)` cast, but `TerminalScreen` derives from
  `GtkOverlay`: every split and every restored pane logged *invalid cast* and
  passed `NULL` on; the parameter is a `TerminalScreen *` now;
* the keyboard grab probe of the drop-down window only runs on X11 — on other
  backends it failed forever and kept rescheduling itself.

## FAQ

<details>
<summary>Why does <kbd>Ctrl+Shift+H</kbd> behave differently depending on the
order the two modifiers are pressed?</summary>

With the layout switcher set to `Ctrl+Shift` (`grp:ctrl_shift_toggle`), pressing
Shift first makes the XKB switcher take over the modifier panel and hand the
key back to the application **without Ctrl** — the modifier is lost in X11,
before the terminal sees the event. The terminal compensates for this: it asks
the X server about the physical Ctrl key and puts the modifier back into the
event, so on X11 the split and every other `Ctrl+Shift+<letter>` hotkey work in
any order of the two keys.

The same compensation covers the `Alt+Shift` switcher (`grp:alt_shift_toggle`):
it eats the <kbd>Alt</kbd> or the <kbd>Shift</kbd> of the chord, and whichever
one is missing is put back the same way.

On Wayland the physical keys cannot be queried this way, so there the original
behaviour stands: press Ctrl first (or press both at once), or free the pair by
switching the layout toggle to another combination, for example `Alt+Shift`:

      % setxkbmap -option grp:alt_shift_toggle
</details>

### Homepage

[Xfce4-terminal documentation](https://docs.xfce.org/apps/xfce4-terminal/start)

### Source Code Repository

[Xfce4-terminal source code](https://gitlab.xfce.org/apps/xfce4-terminal) —
the upstream this fork is built on.

## Comparing with upstream

```
git diff master...feature/split-panes
```

The branch `upstream/pr-layout-accel` is the layout independent accelerator
part alone (`terminal/terminal-keys.c` plus a small hook in
`terminal/terminal-window.c`), kept separate so that it can be reviewed on its
own.

## Building

From the source code repository:

    % meson setup build
    % meson compile -C build

## Tests

The stands are isolated (own `HOME`, own D-Bus, own display `:99`) and must
be run one after another:

    % bash tests/run-all.sh

## Packaging (Arch)

    % cd pkgbuild && PATH="$PWD/../.venv/bin:$PATH" makepkg -f --noconfirm --nodeps

The package is produced from `git archive HEAD` and kept in the repository
root; install it with `sudo pacman -U <package>`.

## Reporting Bugs

Visit the [reporting bugs](https://docs.xfce.org/apps/xfce4-terminal/bugs)
page of the upstream for bugs that also exist there; fork-specific problems
please report with `tests/run-all.sh` output attached.
