# xfce4-terminal-next

A fork of [xfce4-terminal](https://gitlab.xfce.org/apps/xfce4-terminal) with
split panes, a pane lock, clipboard slots, tmux mode and session restore.
Everything is built on top of upstream master; no upstream file is rewritten
from scratch. The full list of changes and the reasoning behind them live in
[FORK](FORK.md).

[![License](https://img.shields.io/badge/License-GPL%20v3-blue.svg)](COPYING)

## What the fork adds

* **Split panes** — a tree of `GtkPaned` inside a tab: `Ctrl+Shift+H` /
  `Ctrl+Shift+E` split, `Ctrl+Shift+X` closes, `Alt+arrows` and `Ctrl+Tab`
  move the focus; the split to focus is chosen geometrically.
* **Pane lock** (`Ctrl+Shift+L`) — the pane survives its program and comes
  back with it at the next start; with the layout saved, the whole split tree
  returns.
* **Clipboard slots** — up to ten labelled slots, filled by copy and pasted
  from the menu or the context sub-menu, optionally kept in a file.
* **tmux mode** — one setting with the command, launch from the menu, the
  session outlives the window.
* **Session** — tabs, the split layout and the active pane are restored at
  the next start.
* **Drop-down terminal** — a system hotkey (`XFCE`) to hide and show it, and
  a `StatusNotifierItem` tray icon on X11 and Wayland.

### Homepage

[Xfce4-terminal documentation](https://docs.xfce.org/apps/xfce4-terminal/start)

### Source Code Repository

[Xfce4-terminal source code](https://gitlab.xfce.org/apps/xfce4-terminal) —
the upstream this fork is built on.

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
