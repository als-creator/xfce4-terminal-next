# xfce4-terminal-next

Форк [xfce4-terminal](https://gitlab.xfce.org/apps/xfce4-terminal) со
сплит-панелями внутри вкладки, выпадающей панелью, запуском tmux, ячейками
обмена, восстановлением сессии и горячими клавишами, не зависящими от
раскладки. Всё построено поверх апстрима `master`; ни один файл апстрима не
переписан с нуля.

Версия идёт за апстримом: `1.2.0`, собственная нумерация — только в `pkgrel`.

English version: [below](#xfce4-terminal-next-english).

[![Лицензия](https://img.shields.io/badge/License-GPL%20v2%2B-blue.svg)](COPYING)

## Отличия от апстрима

- **Сплит-панели в вкладке.** Вкладка — дерево `GtkPaned`: разделение по
  горизонтали и вертикали, переход по панелям, закрытие отдельной панели.
  Раскладка панелей запоминается вместе с сессией.
- **Рамка фокуса у панелей.** У активной панели две тонкие линии рамки, у
  остальных — одна; рамка рисуется в отступе и не накрывает ячейки терминала.
  Одиночная панель выглядит как в апстриме.
- **Горячие клавиши не зависят от раскладки.** `Ctrl+Shift+<буква>` работает
  на русской, иврите и любой другой раскладке, в том числе при переключателе
  `grp:ctrl_shift_toggle`, который съедает один из модификаторов.
- **Выпадающая панель отдельным режимом.** `xfce4-terminal-next --drop-down`:
  показ/скрытие системной горячей клавишей, запуск скрытой, переключение по
  повторному запуску, метод D-Bus `org.xfce.TerminalNext.ToggleDropDown`.
  Обычный запуск никогда не превращается в панель.
- **Иконка в трее на X11 и Wayland.** `StatusNotifierItem` с меню
  `com.canonical.dbusmenu`; если `StatusNotifierWatcher` нет — откат к
  `GtkStatusIcon` апстрима.
- **Запуск tmux одной настройкой.** `Ctrl+Shift+M` запускает настроенную
  строку tmux (`TERM=tmux-256color`, сессия `main`, true color, размер окна
  по панели).
- **Ячейки обмена.** Несколько буферов обмена: `Ctrl+Alt+C` копирует в
  текущую ячейку, `Ctrl+Alt+V` вставляет; число ячеек, формат (текст, HTML,
  ссылки) и хранение в файле настраиваются.
- **Окно открывается поверх.** `gtk_widget_show()` до `gtk_window_present()`,
  поэтому новое окно не остаётся за остальными.
- **Исправления поверх апстрима.** Терминатор массива раскладки панелей
  (иначе xfconf читал за концом массива и последняя панель падала с SIGSEGV),
  корректный тип `TerminalScreen` в `terminal_window_setup_screen()`, проверка
  захвата клавиш панели только на X11.

## Горячие клавиши сплитов

| Сочетание | Действие |
| --- | --- |
| `Ctrl+Shift+H` | разделить по горизонтали |
| `Ctrl+Shift+E` | разделить по вертикали |
| `Ctrl+Shift+X` | закрыть панель |
| `Alt` + стрелки | фокус на панель в эту сторону |
| `Ctrl+Tab` / `Ctrl+Shift+Tab` | следующая / предыдущая панель |

## Установка

Пакет для Arch Linux прикладывается к каждому релизу на
[GitHub](https://github.com/als-creator/xfce4-terminal-next/releases):

```bash
sudo pacman -U xfce4-terminal-next-<версия>-x86_64.pkg.tar.zst
```

Пакет ставится рядом с системным терминалом: `/usr/local/bin/xfce4-terminal-next`,
своё D-Bus-имя и свой пункт меню. Настройки общие с `xfce4-terminal`.

## Сборка из исходников

Нужны `meson`, `ninja`, `gtk3`, `vte3`, `libxfce4ui`, `xfconf`:

```bash
meson setup build
meson compile -C build
./build/terminal/xfce4-terminal
```

## Тесты

Стенды изолированы (свой `HOME`, своя шина D-Bus, свой дисплей `:99`) и
запускаются последовательно:

```bash
bash tests/run-all.sh
```

## Лицензия

GPL-2.0-or-later, как у апстрима. См. [COPYING](COPYING).

---

# xfce4-terminal-next (English)

A fork of [xfce4-terminal](https://gitlab.xfce.org/apps/xfce4-terminal) with
split panes inside a tab, a drop-down panel, tmux launch, clipboard slots,
session restore and layout independent hotkeys. Everything is built on top of
upstream `master`; no upstream file is rewritten from scratch.

The version follows upstream: `1.2.0`, the fork's own numbering lives only in
`pkgrel`.

## Differences from upstream

- **Split panes in a tab.** A tab is a `GtkPaned` tree: split horizontally or
  vertically, move the focus between panes, close a single pane. The pane
  layout is saved and restored with the session.
- **Focus border on panes.** The focused pane has two thin border lines, the
  others have one; the border is drawn in a margin and never covers terminal
  cells. A tab with a single pane looks like upstream.
- **Layout independent hotkeys.** `Ctrl+Shift+<letter>` works on a Russian,
  Hebrew or any other layout, including with the `grp:ctrl_shift_toggle`
  layout switcher that eats one of the modifiers.
- **Drop-down panel as a separate mode.** `xfce4-terminal-next --drop-down`:
  show/hide with a system shortcut, start hidden, toggle on a repeated launch,
  D-Bus method `org.xfce.TerminalNext.ToggleDropDown`. A normal launch never
  turns into the panel.
- **Tray icon on X11 and Wayland.** `StatusNotifierItem` with a
  `com.canonical.dbusmenu` menu; without a `StatusNotifierWatcher` it falls
  back to the upstream `GtkStatusIcon`.
- **tmux launch in one setting.** `Ctrl+Shift+M` runs the configured tmux
  command line (`TERM=tmux-256color`, session `main`, true color, window size
  from the pane).
- **Clipboard slots.** Several clipboards instead of one: `Ctrl+Alt+C` copies
  into the current slot, `Ctrl+Alt+V` pastes; the number of slots, the format
  (text, HTML, links) and file storage are configurable.
- **The window opens on top.** `gtk_widget_show()` before
  `gtk_window_present()`, so a new window does not stay behind the others.
- **Fixes on top of upstream.** Terminated pane layout array (xfconf read past
  the end and closing the last pane crashed with SIGSEGV), correct
  `TerminalScreen` type in `terminal_window_setup_screen()`, panel key grab
  checked on X11 only.

## Split pane hotkeys

| Shortcut | Action |
| --- | --- |
| `Ctrl+Shift+H` | split horizontally |
| `Ctrl+Shift+E` | split vertically |
| `Ctrl+Shift+X` | close the pane |
| `Alt` + arrows | focus the pane in that direction |
| `Ctrl+Tab` / `Ctrl+Shift+Tab` | next / previous pane |

## Installation

The Arch Linux package is attached to every release on
[GitHub](https://github.com/als-creator/xfce4-terminal-next/releases):

```bash
sudo pacman -U xfce4-terminal-next-<version>-x86_64.pkg.tar.zst
```

The package installs next to the system terminal:
`/usr/local/bin/xfce4-terminal-next`, its own D-Bus name and its own menu
entry. Settings are shared with `xfce4-terminal`.

## Building from source

Requires `meson`, `ninja`, `gtk3`, `vte3`, `libxfce4ui`, `xfconf`:

```bash
meson setup build
meson compile -C build
./build/terminal/xfce4-terminal
```

## Tests

The stands are isolated (own `HOME`, own D-Bus bus, own `:99` display) and run
sequentially:

```bash
bash tests/run-all.sh
```

## License

GPL-2.0-or-later, same as upstream. See [COPYING](COPYING).
