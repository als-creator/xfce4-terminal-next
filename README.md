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
* **Clipboard slots** — up to ten labelled slots, filled by copy and pasted
  from the menu or the context sub-menu, optionally kept in a file.
* **tmux mode** — one setting with the command, launch from the menu, the
  session outlives the window.
* **Session** — tabs, the split layout, the active pane and the command of
  every pane are restored at the next start: each split comes back running
  what it ran before.
* **Drop-down terminal** — a system hotkey (`XFCE`) to hide and show it, and
  a `StatusNotifierItem` tray icon on X11 and Wayland.

## FAQ

<details>
<summary>Why does <code>Ctrl+Shift+H</code> behave differently depending on
the order the two modifiers are pressed?</summary>

With the layout switcher set to `Ctrl+Shift` (`grp:ctrl_shift_toggle`), pressing
Shift first makes the XKB switcher take over the modifier panel and hand the
key back to the application **without Ctrl** — the modifier is lost in X11,
before the terminal sees the event. The terminal compensates for this: it asks
the X server about the physical Ctrl key and puts the modifier back into the
event, so on X11 the split and every other `Ctrl+Shift+<letter>` hotkey work in
any order of the two keys.

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

---

# xfce4-terminal-next (русская версия)

Форк [xfce4-terminal](https://gitlab.xfce.org/apps/xfce4-terminal) со
сплит-панелями, локом панели, ячейками обмена, tmux-режимом и восстановлением
сессии. Всё построено поверх апстрима `master`; ни один файл апстрима не
переписан с нуля. Полный список изменений и обоснования — в [FORK](FORK.md).

## Что добавляет форк

* **Сплит-панели** — дерево `GtkPaned` внутри вкладки: `Ctrl+Shift+H` /
  `Ctrl+Shift+E` — разделение, `Ctrl+Shift+X` — закрыть, `Alt+стрелки` и
  `Ctrl+Tab` — переход фокуса; сплит выбирается геометрически.
* **Ячейки обмена** — до десяти именованных ячеек: заполняются копированием,
  вставляются из меню или контекстного подменю, опционально хранятся в файле.
* **tmux-режим** — одна настройка с командой запуска из меню, сессия
  переживает окно.
* **Сессия** — вкладки, раскладка сплитов, активная панель и команда каждой
  панели восстанавливаются при следующем запуске: каждый сплит возвращается
  с той же командой, с которой работал.
* **Выпадающий терминал** — системная горячая клавиша (`XFCE`) для скрытия и
  появления, иконка в трее (`StatusNotifierItem`) на X11 и Wayland.

## Часто задаваемые вопросы

<details>
<summary>Почему <code>Ctrl+Shift+H</code> вёл себя иначе в зависимости от
порядка нажатия модификаторов?</summary>

При переключателе раскладки `Ctrl+Shift` (`grp:ctrl_shift_toggle`), если
зажать Shift раньше Ctrl, XKB-переключатель забирает панель модификаторов и
возвращает клавишу приложению **уже без Ctrl** — модификатор теряется ещё в
X11, до того как событие дойдёт до терминала. Терминал это компенсирует:
он спрашивает X-сервер о физическом состоянии Ctrl и возвращает модификатор в
событие, поэтому на X11 сплит и любая другая комбинация `Ctrl+Shift+<буква>`
работают при любом порядке нажатия этих двух клавиш.

На Wayland физическое состояние клавиш так не опросить, поэтому там остаётся
исходное поведение: зажимать Ctrl первым (или обе клавиши одновременно) либо
освободить пару, сменив переключатель раскладки на другую комбинацию,
например `Alt+Shift`:

      % setxkbmap -option grp:alt_shift_toggle
</details>

### Главная страница

[Документация xfce4-terminal](https://docs.xfce.org/apps/xfce4-terminal/start)

### Исходный код

[Исходный код xfce4-terminal](https://gitlab.xfce.org/apps/xfce4-terminal) —
апстрим, поверх которого построен форк.

## Сборка

Из репозитория исходного кода:

    % meson setup build
    % meson compile -C build

## Тесты

Стенды изолированы (свой `HOME`, своя D-Bus, свой дисплей `:99`) и должны
запускаться последовательно:

    % bash tests/run-all.sh

## Пакетирование (Arch)

    % cd pkgbuild && PATH="$PWD/../.venv/bin:$PATH" makepkg -f --noconfirm --nodeps

Пакет собирается из `git archive HEAD` и лежит в корне репозитория;
установка: `sudo pacman -U <пакет>`.

## Сообщение об ошибках

Ошибки, которые есть и в апстриме, — на странице
[reporting bugs](https://docs.xfce.org/apps/xfce4-terminal/bugs) апстрима;
ошибки, специфичные для форка, присылайте с выводом `tests/run-all.sh`.
