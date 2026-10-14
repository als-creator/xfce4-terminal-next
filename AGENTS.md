# xfce4-terminal-local — рабочий процесс

Форк xfce4-terminal: нативные сплиты, guake-резидентность, tmux, выпадающий
терминал. Ветка форка — `feature/split-panes`, ветки для возможных PR —
`upstream/*` (считаются от чистого `master`, который совпадает с апстримом).

## Что где лежит

Общее правило — в глобальном `AGENTS.md` (артефакты проекта не в `/tmp`), здесь
конкретика:

| Что | Где |
| --- | --- |
| Инструменты сборки (`meson`, `ninja`) | `.venv/` в папке проекта (в `.gitignore`) |
| Каталог сборки meson | `build/` в папке проекта (в `.gitignore`) |
| Стенды и их исходники, тестовые бинарники | `tests/` (`tests/bin/` для бинарников; каталог в `.gitignore`) |
| Переводы | `po/ru.po`, шаблон `po/xfce4-terminal.pot` (не отслеживается) |
| Каталог сборки Arch-пакета | домашний каталог пользователя, корень (`~`) |
| Готовый пакет | корень репозитория (`*.pkg.tar.zst`, в `.gitignore`) |
| Логи и `HOME` стендов на время прогона | `/tmp/opencode/xtest` (короткоживущее, можно терять) |

## Сборка

```
cd /mnt/Data/Projects/xfce4-terminal
PATH=$PWD/.venv/bin:$PATH meson compile -C build
```

Если `.venv` пропал:

```
python3 -m venv .venv && .venv/bin/pip install meson ninja
PATH=$PWD/.venv/bin:$PATH meson setup --reconfigure build
```

Переконфигурация обязательна: в `build.ninja` вбиты абсолютные пути к `meson` и
`ninja`, и после пересоздания `.venv` старые пути мертвы.

## После правок

Порядок один и тот же для любой задачи, добавленной функции или исправления:
**сначала собрать, потом тест, и только затем отчёт.** Непроверенную правку за
готовую не выдавать.

1. Сборка: `PATH=$PWD/.venv/bin:$PATH meson compile -C build`.
2. Стенды, которых касается правка:
   - нормализация раскладки — `tests/bin/test-keys` (48 проверок);
   - панели — `python3 tests/build-test.py` и `tests/bin/test-panes` (67 проверок);
   - выпадающее окно и разделение режимов — `tests/stand-wm.sh`, затем
     `dbus-run-session -- tests/split-test.sh` (и `guake-*.sh`/`plain-test.sh`).
3. Новые строки интерфейса — перевести в `po/ru.po` и проверить
   `msgfmt -c --statistics -o /dev/null po/ru.po`.
4. Если стенд не запускается в окружении (например, нет `Xvfb` или дисплея) —
   сказать об этом прямо, а не считать фичу проверенной.

## Тесты

Исходники стендов и сами скрипты лежат в `tests/` (каталог в `.gitignore`, в
дифф форка не попадает). Рабочие каталоги, `HOME` и логи стендов пишутся в
`/tmp/opencode/xtest` — этот каталог временный и может исчезнуть, поэтому сами
скрипты храним только в `tests/`.

Стенды с окнами работают только на своём дисплее — `:99` (поднимает
`tests/stand-wm.sh`, он и `keys-live-test.sh` отказываются работать на `:0`).
`DISPLAY` из окружения для них брать нельзя: там может оказаться сессия
пользователя, и тогда окно стенда перехватит фокус, а `setxkbmap` сменит
раскладку пользователю. Это правило уже стоило вытеснения пользовательского
`xfwm4` на `:0`: стенды ставили `DISPLAY="${DISPLAY:-:99}"` и запускали
`xfwm4 --replace` там, где оказывался дисплей агента. Теперь `DISPLAY`
задаётся жёстко — `export DISPLAY=":99"`, с отказом при `:0`.
Синтетические стенды без окна (`test-keys`, `test-panes`) дисплей не трогают.

Общая часть стендов выпадающего окна — `tests/stand-lib.sh`:
подъём Xvfb и `xfwm4` (с `--replace` и одной повторной попыткой), выбор
окна по роли (`WM_WINDOW_ROLE`: `xfce4-terminal-dropdown` у панели,
`xfce4-terminal-*` у обычного окна; служебное окно 10×10 роли не имеет),
отправка клавиш с ожиданием эффекта до 12 с (панель сворачивается с
анимацией) и с повтором. Предикат эффекта исполняется через `eval`.

Панель переключается **не клавишей F1**, а вызовом D-Bus
`org.xfce.Terminal5.ToggleDropDown`: собственный перехват клавиши у
терминала удалён (он X11-only и в апстриме его нет), а на Wayland клавишу
всё равно обрабатывает рабочий стол. Стенды зовут `stand_toggle_panel`.

| Что | Как |
| --- | --- |
| Нормализация раскладки | `mkdir -p tests/bin && gcc -o tests/bin/test-keys tests/test-keys.c terminal/terminal-keys.c -Iterminal $(pkg-config --cflags --libs gtk+-3.0 gdk-x11-3.0 x11)` и `DISPLAY=:99 tests/bin/test-keys` — 48 проверок |
| Панели | `python3 tests/build-test.py` и `DISPLAY=:99 tests/bin/test-panes` — 67 проверок |
| Переводы | `msgfmt -c --statistics -o /dev/null po/ru.po` |
| Разделение режимов | `dbus-run-session -- bash tests/split-test.sh` — 5 проверок |
| Выпадающее окно | `dbus-run-session -- bash tests/guake-test.sh` — 5 проверок (Ctrl+D прячет, ToggleDropDown возвращает, закрытие окна завершает) |
| Панель с keep-open | `dbus-run-session -- bash tests/guake-hide-test.sh` — 6 проверок |
| Панель без keep-alive | `dbus-run-session -- bash tests/guake-off-test.sh` — 2 проверки, строгое поведение апстрима |
| Обычное окно | `dbus-run-session -- bash tests/plain-test.sh` — 2 проверки |
| Системная горячая клавиша | `dbus-run-session -- bash tests/shortcuts-test.sh` — 13 проверок |
| Значок и меню трея | `bash tests/sni-test.sh` — 14 проверок, свой StatusNotifierWatcher на изолированной шине |
| D-Bus переключение панели | `bash tests/dropdown-dbus-test.sh` |
| Скрытый старт панели | `bash tests/dropdown-hidden-test.sh` |
| Падение при закрытии панели | `bash tests/crash-check.sh` — ASan-сборка, сплиты и закрытие вкладок |
| Хоткеи на нелатинской раскладке | `tests/keys-live-test.sh` на `:99` с `grp:ctrl_shift_toggle`: `Ctrl+Shift+H` и `Ctrl+Shift+Tab` срабатывают, `Ctrl+H`/`Ctrl+Tab` — нет; печатает `KEY … h=1/0 tab=1/0` |

Стенды выпадающего окна и разделения режимов работают на изолированном
дисплее `:99` и не перехватывают фокус у пользователя: их поднимает
`tests/stand-wm.sh` (нужен пакет `xorg-server-xvfb`), оконный менеджер
(`xfwm4 --sm-client-disable --daemon`) — сами скрипты. Запускать внутри
`dbus-run-session`, чтобы у стенда была своя шина.

Пересобрать стенд панелей нужно после изменений в `terminal-panes.c`/`.h`.

## Шаблон перевода

Файл `po/xfce4-terminal.pot` **не отслеживается**. Цель meson проверяет
актуальность и молча ничего не делает, поэтому шаблон сначала удаляется:

```
rm -f po/xfce4-terminal.pot
PATH=$PWD/.venv/bin:$PATH meson compile -C build xfce4-terminal-pot
msgmerge --no-fuzzy-matching --update po/ru.po po/xfce4-terminal.pot
```

Новые пользовательские строки обязательно переводим в `po/ru.po`.

## Arch-пакет

Каталог сборки — **домашний каталог пользователя, прямо в корне** (`~`): там
лежат `src/`, `PKGBUILD` и готовый `*.pkg.tar.zst`. В `/tmp` не собираем: это
tmpfs, его содержимое теряется при перезагрузке вместе с исходниками и пакетом.

```
cd ~ && rm -rf src && mkdir -p src
git -C /mnt/Data/Projects/xfce4-terminal archive HEAD | tar -C src -xf -
cp /mnt/Data/Projects/xfce4-terminal/PKGBUILD ~/
cp /mnt/Data/Projects/xfce4-terminal/xfce4-terminal-local.install ~/
PATH=/mnt/Data/Projects/xfce4-terminal/.venv/bin:$PATH makepkg -f --noconfirm --nodeps
```

`pkgrel` поднимать на единицу при каждой пересборке; готовый пакет копируется в
корень репозитория (`/mnt/Data/Projects/xfce4-terminal/`), предыдущий удаляется.
Установка в `/usr/local`, бинарник `xfce4-terminal-local`, D-Bus
`org.xfce.Terminal5Local`.

## Стиль кода XFCE

- Шапка файла: `/*-`, строка `Copyright (c) <год> <автор>`, GPL с
  `If not, see <http://www.gnu.org/licenses/>` — как в существующих файлах;
- отступ 2 пробела, без табов, без висячих пробелов;
- gtk-doc комментарии для публичных функций, `g_return_if_fail` для проверок;
- новые строки — не длиннее 100 колонок (в апстриме ограничения нет, но в
  диффе форка лучше придерживаться);
- имена: `terminal_<модуль>_<действие>`, типы `Terminal<Что>`, поля в
  `struct _TerminalXxxPrivate`.

## Проверка акселераторов

Таблица действий — `action_entries[]` в `terminal/terminal-window.c`. Перед тем
как занять сочетание, проверить, что оно свободно и не занято оболочкой:

```
sed -n '/^static XfceGtkActionEntry action_entries\[\]/,/^};/p' terminal/terminal-window.c
bash -c 'bind -p | grep -E "backward-delete-char|quoted-insert"'
```

Голые буквы и `Ctrl+H`/`Ctrl+V`/`Ctrl+Q` использовать нельзя: в readline это
`backward-delete-char` и `quoted-insert`. `Super`-сочетания не использовать:
их перехватывает оконный менеджер или композитор.
