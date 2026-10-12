# Maintainer: local build
#
# Локальная сборка xfce4-terminal с нативными сплитами.
#
# Особенности (в отличие от системного пакета xfce4-terminal):
#   * нативные сплиты: Ctrl+Shift+O / Ctrl+Shift+E, закрытие Ctrl+Shift+F4
#   * навигация по панелям: Alt+стрелки, Ctrl+Tab / Ctrl+Shift+Tab
#   * guake-режим: иконка в трее, закрытие окна скрывает его вместо выхода
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
pkgrel=2
pkgdesc="Xfce Terminal with native split panes and tray mode (isolated local build)"
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
    export PATH="/tmp/opencode/venv/bin:$PATH"

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
    export PATH="/tmp/opencode/venv/bin:$PATH"

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
Comment=Terminal with native split panes and tray mode
Exec=/usr/local/bin/xfce4-terminal-local %F
Icon=org.xfce.terminal
Type=Application
Terminal=false
Categories=System;TerminalEmulator;
Keywords=shell;prompt;commandline;command;split;pane;
StartupNotify=true
EOF

    # Документация
    mkdir -p "$pkgdir/usr/local/share/doc/xfce4-terminal-local"
    cat > "$pkgdir/usr/local/share/doc/xfce4-terminal-local/README" << 'EOF'
xfce4-terminal-local (локальная сборка)
====================================

Отличия от системного xfce4-terminal:
  * нативные сплиты:        Ctrl+Shift+O (вбок), Ctrl+Shift+E (сверху/снизу)
  * закрыть панель:          Ctrl+Shift+F4
  * фокус по панелям:        Alt+стрелки, Ctrl+Tab / Ctrl+Shift+Tab
  * guake-режим:             Правки -> Прочее -> Показать значок в трее;
                             закрытие окна скрывает его вместо выхода

Запуск:   xfce4-terminal-local
Меню:     "Xfce Terminal (Split Panes)"
Путь:     /usr/local/bin/xfce4-terminal-local

Изоляция от системного пакета xfce4-terminal:
  - своё имя бинарника (нет перекрытия в PATH)
  - своё D-Bus-имя org.xfce.Terminal5Local
  - свои настройки: канал xfconf "xfce4-terminal-local" НЕ используется,
    настройки общие с системным терминалом (тот же канал xfce4-terminal)

Удаление:  sudo pacman -Rns xfce4-terminal-local
EOF
}