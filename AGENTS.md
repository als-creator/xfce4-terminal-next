# xfce4-terminal-local — рабочий процесс

Форк xfce4-terminal: нативные сплиты, guake-резидентность, tmux, выпадающий
терминал. Ветка форка — `feature/split-panes`, ветки для возможных PR —
`upstream/*` (считаются от чистого `master`, который совпадает с апстримом).

## Сборка

```
cd /mnt/Data/Projects/xfce4-terminal
PATH=/tmp/opencode/venv/bin:$PATH meson compile -C build
```

`meson` и `ninja` есть только в `/tmp/opencode/venv/bin`. Полная пересборка с
нуля: `meson setup /tmp/opencode/build-pr .` и `meson compile -C /tmp/opencode/build-pr`.

## Тесты

| Что | Как |
| --- | --- |
| Нормализация раскладки | `gcc -o /tmp/opencode/test-keys tests/test-keys.c terminal/terminal-keys.c -Iterminal $(pkg-config --cflags --libs gtk+-3.0)` и `DISPLAY=:0.0 /tmp/opencode/test-keys` — 22 проверки |
| Панели | `python3 tests/build-test.py` и `DISPLAY=:0.0 /tmp/opencode/test-panes` — 67 проверок |
| Переводы | `msgfmt -c --statistics -o /dev/null po/ru.po` |

Пересобрать стенд панелей нужно после изменений в `terminal-panes.c`/`.h`.

## Шаблон перевода

Файл `po/xfce4-terminal.pot` **не отслеживается**. Цель meson проверяет
актуальность и молча ничего не делает, поэтому шаблон сначала удаляется:

```
rm -f po/xfce4-terminal.pot
PATH=/tmp/opencode/venv/bin:$PATH meson compile -C build xfce4-terminal-pot
msgmerge --no-fuzzy-matching --update po/ru.po po/xfce4-terminal.pot
```

Новые пользовательские строки обязательно переводим в `po/ru.po`.

## Arch-пакет

```
cd /tmp/opencode/pkgbuild && rm -rf src && mkdir -p src
git -C /mnt/Data/Projects/xfce4-terminal archive HEAD | tar -C src -xf -
cp /mnt/Data/Projects/xfce4-terminal/PKGBUILD .
cp /mnt/Data/Projects/xfce4-terminal/xfce4-terminal-local.install .
PATH=/tmp/opencode/venv/bin:$PATH makepkg -f --noconfirm --nodeps
```

`pkgrel` поднимать на единицу при каждой пересборке; готовый пакет копируется в
корень репозитория, предыдущий удаляется. Установка в `/usr/local`, бинарник
`xfce4-terminal-local`, D-Bus `org.xfce.Terminal5Local`.

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
