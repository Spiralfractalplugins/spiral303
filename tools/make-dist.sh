#!/usr/bin/env bash
# Assemble dist/: version-tagged folders and zips for macOS, Windows and Linux.
#
#   ./tools/make-dist.sh
#
# Expects the artefacts to be already built and tested:
#   macOS   - build/Spiral303_artefacts/Release/{VST3,AU,LV2,Standalone}/
#   Windows - build/windows/{Spiral303.vst3,Spiral303.lv2,Spiral303.exe} (cross-build.sh windows)
#   Linux   - build/linux/{Spiral303.vst3,Spiral303.lv2,Spiral303_Standalone}
#             (cross-build.sh linux; the Windows and Linux packages are optional)
set -euo pipefail

REPO=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
DIST="$REPO/dist"

# Single source of truth for the version is project(Spiral303 VERSION ...)
VERSION=$(sed -n 's/^project(.*VERSION \([0-9][0-9.]*\).*/\1/p' "$REPO/CMakeLists.txt" | head -1)
[ -n "$VERSION" ] || { echo "Cannot read version from CMakeLists.txt" >&2; exit 1; }

readme() {
cat <<EOF
Spiral303 $VERSION - эмуляция Roland TB-303 (синтезатор + секвенсор).

## Windows
- Скопируйте Spiral303.vst3 целиком (папку) в
  C:\\Program Files\\Common Files\\VST3\\. Рядом лежащий Spiral303.exe -
  это Standalone-версия, с ней плагин можно проверить без хоста.
- Spiral303.lv2 - в C:\\Program Files\\Common Files\\LV2\\ (или
  %APPDATA%\\LV2\\).
- Сборка не подписана: SmartScreen может предупредить при первом
  запуске Standalone - нажмите «Подробнее» - «Выполнить в любом случае».

## macOS
- Spiral303.vst3 - в ~/Library/Audio/Plug-Ins/VST3/,
  Spiral303.component - в ~/Library/Audio/Plug-Ins/Components/,
  Spiral303.lv2 - в ~/Library/Audio/Plug-Ins/LV2/,
  Spiral303.app - Standalone-версия, можно оставить на диске.
- Первое использование неподписанного плагина Gatekeeper может
  потребовать подтвердить через «Системные настройки -
  Конфиденциальность и безопасность».
EOF
}

readme_linux() {
cat <<EOF
Spiral303 $VERSION - эмуляция Roland TB-303 (синтезатор + секвенсор).
Сборка x86_64 (Ubuntu 24.04).

## Установка (Linux)
- Spiral303.vst3 - в ~/.vst3/ или /usr/local/lib/vst3/.
- Spiral303.lv2 - в ~/.lv2/.
- Spiral303_Standalone - обычный исполняемый файл, можно оставить на диске
  (chmod +x, если архив распакован без прав).
- Обновите список плагинов в хосте.
EOF
}

DISTMAC="$DIST/Spiral303-$VERSION-macOS"
DISTWIN="$DIST/Spiral303-$VERSION-Windows"
DISTLNX="$DIST/Spiral303-$VERSION-Linux"

mkzip() {
    (cd "$DIST" && zip -qr -X "$1".zip "$2" -x '*.DS_Store')
}

# ---- macOS -------------------------------------------------------------------
MAC_SRC="$REPO/build/Spiral303_artefacts/Release"
if [ ! -d "$MAC_SRC/VST3" ]; then
    echo "macOS artefacts missing - build the Release config first" >&2
else
    rm -rf "$DISTMAC" "$DISTMAC".zip
    mkdir -p "$DISTMAC"
    cp -a "$MAC_SRC/VST3/Spiral303.vst3" "$DISTMAC/"
    cp -a "$MAC_SRC/AU/Spiral303.component" "$DISTMAC/"
    cp -a "$MAC_SRC/LV2/Spiral303.lv2" "$DISTMAC/"
    cp -a "$MAC_SRC/Standalone/Spiral303.app" "$DISTMAC/"
    readme > "$DISTMAC/README.md"
    mkzip "$DISTMAC" "Spiral303-$VERSION-macOS"
fi

# ---- Windows -----------------------------------------------------------------
WIN_SRC="$REPO/build/windows"
if [ ! -d "$WIN_SRC/Spiral303.vst3" ]; then
    echo "Windows artefacts missing - run tools/windows-cross-build/cross-build.sh windows" >&2
else
    rm -rf "$DISTWIN" "$DISTWIN".zip
    mkdir -p "$DISTWIN"
    cp -a "$WIN_SRC/Spiral303.vst3" "$DISTWIN/"
    cp -a "$WIN_SRC/Spiral303.exe" "$DISTWIN/"
    [ -d "$WIN_SRC/Spiral303.lv2" ] && cp -a "$WIN_SRC/Spiral303.lv2" "$DISTWIN/" || true
    readme > "$DISTWIN/README.md"
    mkzip "$DISTWIN" "Spiral303-$VERSION-Windows"
fi

# ---- Linux -------------------------------------------------------------------
LNX_SRC="$REPO/build/linux"
if [ ! -d "$LNX_SRC/Spiral303.vst3" ]; then
    echo "Linux artefacts missing - run tools/windows-cross-build/cross-build.sh linux" >&2
else
    rm -rf "$DISTLNX" "$DISTLNX".zip
    mkdir -p "$DISTLNX"
    cp -a "$LNX_SRC/Spiral303.vst3" "$DISTLNX/"
    cp -a "$LNX_SRC/Spiral303_Standalone" "$DISTLNX/"
    [ -d "$LNX_SRC/Spiral303.lv2" ] && cp -a "$LNX_SRC/Spiral303.lv2" "$DISTLNX/" || true
    readme_linux > "$DISTLNX/README.md"
    mkzip "$DISTLNX" "Spiral303-$VERSION-Linux"
fi

ls -l "$DIST"/*.zip 2>/dev/null
