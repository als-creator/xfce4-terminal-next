#!/bin/bash
# Сборка AppImage для xfce4-terminal
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="$SCRIPT_DIR/build"
APPDIR="$SCRIPT_DIR/xfce4-terminal.AppDir"
OUTPUT_DIR="$HOME"
LINUXDEPLOY_DIR="/tmp/opencode/appimage-tools/squashfs-root"
APPIMAGETOOL="/tmp/opencode/appimage-tools/appimagetool-x86_64.AppImage"

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

echo -e "${YELLOW}=== Сборка AppImage для xfce4-terminal ===${NC}"

# 1. Проверки
echo -e "\n${YELLOW}[1/3] Проверка зависимостей...${NC}"
if [ ! -f "$BUILD_DIR/terminal/xfce4-terminal" ]; then
    echo -e "${RED}Ошибка: бинарник не найден. Сначала выполните meson compile -C build${NC}"
    exit 1
fi

if [ ! -x "$LINUXDEPLOY_DIR/usr/bin/linuxdeploy" ]; then
    echo -e "${RED}Ошибка: linuxdeploy не найден. Сначала распакуйте его${NC}"
    exit 1
fi

if [ ! -f "$APPIMAGETOOL" ]; then
    echo -e "${RED}Ошибка: appimagetool не найден${NC}"
    exit 1
fi

# 2. Создание AppDir через linuxdeploy
echo -e "\n${YELLOW}[2/3] Создание AppDir через linuxdeploy...${NC}"
rm -rf "$APPDIR"

# linuxdeploy с использованием системного strip
"$LINUXDEPLOY_DIR/usr/bin/linuxdeploy" \
    --executable="$BUILD_DIR/terminal/xfce4-terminal" \
    --desktop-file="$BUILD_DIR/xfce4-terminal.desktop" \
    --icon-file="$SCRIPT_DIR/icons/128x128/org.xfce.terminal.png" \
    --appdir="$APPDIR" \
    --output=appimage \
    2>&1 | tee /tmp/opencode/linuxdeploy.log

if [ ! -f "$APPDIR/AppRun" ]; then
    echo -e "${RED}Ошибка: linuxdeploy не создал AppRun${NC}"
    exit 1
fi

# Заменяем AppRun на собственный скрипт (linuxdeploy создаёт симлинк)
rm -f "$APPDIR/AppRun"
cat > "$APPDIR/AppRun" << 'EOF'
#!/bin/bash
HERE="$(dirname "$(readlink -f "$0")")"
export LD_LIBRARY_PATH="$HERE/usr/lib:$LD_LIBRARY_PATH"
export PATH="$HERE/usr/bin:$PATH"
export XDG_DATA_DIRS="$HERE/usr/share:$XDG_DATA_DIRS"
exec "$HERE/usr/bin/xfce4-terminal" "$@"
EOF
chmod +x "$APPDIR/AppRun"

echo -e "${GREEN}AppDir создан: $APPDIR ($(du -sh $APPDIR | cut -f1))${NC}"

# 3. Упаковка в AppImage
echo -e "\n${YELLOW}[3/3] Упаковка в AppImage...${NC}"
mkdir -p "$OUTPUT_DIR"

VERSION=$(git describe --tags --always 2>/dev/null || echo 'unknown')
OUTPUT_FILE="$OUTPUT_DIR/xfce4-terminal-${VERSION}-x86_64.AppImage"

"$APPIMAGETOOL" \
    --appimage-extract-and-run \
    --yes \
    "$APPDIR" \
    "$OUTPUT_FILE" \
    2>&1 | tee /tmp/opencode/appimagetool.log

# Проверка
if [ -f "$OUTPUT_FILE" ]; then
    SIZE=$(du -h "$OUTPUT_FILE" | cut -f1)
    echo -e "\n${GREEN}✓ AppImage успешно создан!${NC}"
    echo -e "${GREEN}  Файл: $OUTPUT_FILE${NC}"
    echo -e "${GREEN}  Размер: $SIZE${NC}"
else
    echo -e "${RED}Ошибка: AppImage не создан${NC}"
    exit 1
fi

echo -e "\n${GREEN}=== Готово! ===${NC}"
echo "  chmod +x $OUTPUT_FILE"
echo "  $OUTPUT_FILE"
echo ""
echo "Примечание: если при запуске возникает ошибка appimagelauncher,"
echo "используйте: $OUTPUT_FILE --appimage-extract-and-run"
echo "или распакуйте: $OUTPUT_FILE --appimage-extract"
