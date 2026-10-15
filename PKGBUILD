# Maintainer: local build
#
# Локальная сборка xfce4-terminal с нативными сплитами.
#
# Особенности (в отличие от системного пакета xfce4-terminal):
#   * нативные сплиты: Ctrl+Shift+H / Ctrl+Shift+E, закрытие Ctrl+Shift+X
#   * навигация по панелям: Alt+стрелки, Ctrl+Tab / Ctrl+Shift+Tab
#   * выпадающий терминал: апстримный значок в трее, при выходе шелла
#     последней вкладки (Ctrl+D) окно скрывается, вкладка пересоздаётсяёт
#   * tmux-команда (Ctrl+Shift+M), редактируется в настройках
#   * вкладки запоминаются вместе с рабочими каталогами панелей
#     (отключается галочкой Настройки -> Сессия)
#
# Изоляция от системного терминала:
#   * бинарник называется xfce4-terminal-local -> НЕ перекрывает /usr/bin
#   * своё D-Bus-имя org.xfce.Terminal5Local -> не отправляет запросы
#     системному терминалу и не конфликтует с ним по шине
#   * свой .desktop-файл -> отдельный пункт в меню
#
# Установка:  sudo pacman -U xfce4-terminal-local-*.pkg.tar.zst
# Удаление:   sudo pacman -Rns xfce4-terminal-local
pkgname=xfce4-terminal-local
pkgver=1.2.0
pkgrel=41
pkgdesc="Xfce Terminal with native split panes and tmux integration (isolated local build)"
arch=('x86_64')
url="https://gitlab.xfce.org/apps/xfce4-terminal"
license=('GPL2')
depends=('gtk3' 'libxfce4ui' 'vte3' 'xfconf')
makedepends=('meson' 'ninja' 'pkgconf')
install=xfce4-terminal-local.install
options=('!strip' '!debug')
source=()

build() {
    cd "$startdir/src"

    # meson/ninja живут только в локальном venv
    export PATH="/mnt/Data/Projects/xfce4-terminal/.venv/bin:$PATH"

    # Уникальное D-Bus-имя: иначе бинарник проксирует вызовы
    # запущенному системному терминалу вместо собственного запуска
    sed -i "s/terminal_version_dbus = '5'/terminal_version_dbus = '5Local'/" meson.build

    meson setup build \
        --prefix=/usr/local \
        --sysconfdir=/etc \
        --localstatedir=/var \
        --buildtype=release \
        -Ddoc=false

    ninja -C build
}

package() {
    cd "$startdir/src"
    export PATH="/mnt/Data/Projects/xfce4-terminal/.venv/bin:$PATH"

    DESTDIR="$pkgdir" ninja -C build install

    # Переименовываем бинарник, чтобы не перекрывать /usr/bin/xfce4-terminal
    mv "$pkgdir/usr/local/bin/xfce4-terminal" \
       "$pkgdir/usr/local/bin/xfce4-terminal-local"

    # Системные .desktop-файлы не ставим: свой вариант с уникальным именем
    rm -f "$pkgdir/usr/local/share/applications/xfce4-terminal.desktop"
    rm -f "$pkgdir/usr/local/share/applications/xfce4-terminal-settings.desktop"

    cat > "$pkgdir/usr/local/share/applications/xfce4-terminal-local.desktop" << 'EOF'
[Desktop Entry]
Name=Xfce4 Terminal (Split Panes)
GenericName=Terminal Emulator
Comment=Terminal with native split panes and tmux integration
Exec=/usr/local/bin/xfce4-terminal-local %F
Icon=org.xfce.terminal
Type=Application
Terminal=false
Categories=System;TerminalEmulator;
Keywords=shell;prompt;commandline;command;split;pane;tmux;
StartupNotify=true
EOF

    # Документация
    mkdir -p "$pkgdir/usr/local/share/doc/xfce4-terminal-local"
    cat > "$pkgdir/usr/local/share/doc/xfce4-terminal-local/README" << 'EOF'
xfce4-terminal-local (локальная сборка)
====================================

Сплиты (правый клик в терминале -> Split, или меню Terminal -> Split):
  Split Horizontally        Ctrl+Shift+H     панели рядом
  Split Vertically          Ctrl+Shift+E     панели друг над другом
  Close Pane                Ctrl+Shift+X

Навигация по панелям (только хоткеи, в меню не выведены):
  Focus Pane Up/Down/Left/Right   Alt+стрелки
  Next / Previous Pane            Ctrl+Tab / Ctrl+Shift+Tab

Восстановление сессии:
  Терминал запоминает, на какие панели разбита каждая вкладка, вместе с
  рабочим каталогом каждой панели, и восстанавливает их при следующем
  запуске. Запись делается при каждом изменении и перед закрытием окна.

  Отключается галочкой:
    Правки -> Настройки -> вкладка "Общие", раздел "Сессия" ->
    галочка "Запоминать раскладку панелей вкладок"
  (галочка снята -> ничего не запоминается и не восстанавливается, терминал
  открывается одной панелью в домашнем каталоге; уже сохранённая раскладка
  не стирается и вернётся, если галочку снова поставить).

tmux (правый клик -> Start tmux Session, или меню Terminal -> Split):
  Start tmux Session   Ctrl+Shift+M

  Выполняет команду из настроек: Правки -> Настройки -> tmux.
  По умолчанию: tmux new-session -A -s main
  Флаг -A = attach-or-create, поэтому повторный запуск после обрыва SSH
  просто подхватывает уже существующую сессию.

  Свою команду можно задать, например:
    tmux new-session -A -s work
    tmux attach -t deploy
  xfconf-query -c xfce4-terminal -p /misc-tmux-command -s "tmux attach -t X"

выпадающий терминал:
  Это отдельный режим, включается только явным ключом:
    xfce4-terminal-local --drop-down
  Обычный запуск (меню, Thunar "Открыть терминал здесь", ярлык) всегда
  открывает обычное окно и в панель не попадает. Настройки, которая
  делала бы любой запуск выпадающим, в форке больше нет.

  Панель удобно поднимать из автозапуска сессии:
    ~/.config/autostart/xfce4-terminal-local-dropdown.desktop
  с Exec=xfce4-terminal-local --drop-down
  Тогда F1 (настройка dropdown-toggle-key) показывает и скрывает панель.

  Настройка dropdown-keep-alive "Всегда показывать значок в трее
  (не завершать процесс)" (включена по умолчанию, только X11) заменяет
  апстримную "Show status icon in notification area". Значок апстримный:
  клик показывает/скрывает панель, меню значка - "Настройки" и "Закрыть окно".

  При включённой настройке панель живёт как в Guake: когда шелл последней
  вкладки завершается (Ctrl+D), вкладка пересоздаётся, а само окно
  скрывается - процесс и значок остаются, следующее нажатие F1 показывает
  свежую вкладку. Явное закрытие окна (пункт "Закрыть окно" в меню значка
  или кнопка закрытия) по-прежнему завершает программу. При выключенной
  настройке - поведение апстрима.

Запуск:   xfce4-terminal-local
Меню:     "Xfce4 Terminal (Split Panes)"
Путь:     /usr/local/bin/xfce4-terminal-local

Изоляция от системного пакета xfce4-terminal:
  - своё имя бинарника (нет перекрытия в PATH)
  - своё D-Bus-имя org.xfce.Terminal5Local
  - настройки общие с системным терминалом (тот же канал xfce4-terminal)

Удаление:  sudo pacman -Rns xfce4-terminal-local
EOF
}