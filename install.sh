#!/bin/sh
set -eu

APP_NAME="Hue Browser"
PREFIX="${PREFIX:-$HOME/.local}"
SOURCE_ARCHIVE_URL="${1:-}"
TEMP_DIR=""

cleanup() {
    if [ -n "$TEMP_DIR" ] && [ -d "$TEMP_DIR" ]; then
        rm -rf -- "$TEMP_DIR"
    fi
}
trap cleanup EXIT
trap 'exit 129' HUP
trap 'exit 130' INT
trap 'exit 143' TERM

fail() {
    printf '%s\n' "Erro: $*" >&2
    exit 1
}

install_packages() {
    if [ "$(id -u)" -eq 0 ]; then
        apt-get update
        DEBIAN_FRONTEND=noninteractive apt-get install -y build-essential cmake pkg-config libgtk-3-dev libwebkit2gtk-4.1-dev
    elif command -v sudo >/dev/null 2>&1; then
        sudo apt-get update
        sudo env DEBIAN_FRONTEND=noninteractive apt-get install -y build-essential cmake pkg-config libgtk-3-dev libwebkit2gtk-4.1-dev
    else
        fail "faltam dependências de compilação. Instale build-essential cmake pkg-config libgtk-3-dev e libwebkit2gtk-4.1-dev."
    fi
}

if [ "$(uname -s)" != "Linux" ]; then
    fail "este instalador suporta Linux Debian/Ubuntu."
fi
command -v apt-get >/dev/null 2>&1 || fail "apt-get não encontrado; este instalador suporta Debian/Ubuntu."

DEPS_OK=1
command -v cmake >/dev/null 2>&1 || DEPS_OK=0
command -v cc >/dev/null 2>&1 || DEPS_OK=0
command -v pkg-config >/dev/null 2>&1 || DEPS_OK=0
if [ "$DEPS_OK" -eq 1 ]; then
    pkg-config --exists gtk+-3.0 webkit2gtk-4.1 || DEPS_OK=0
fi
if [ "$DEPS_OK" -eq 0 ]; then
    printf '%s\n' "Instalando dependências do GTK/WebKitGTK..."
    install_packages
fi

if [ -f CMakeLists.txt ] && [ -f src/main.c ]; then
    SOURCE_DIR=$(pwd)
elif [ -f "$(dirname "$0")/CMakeLists.txt" ] && [ -f "$(dirname "$0")/src/main.c" ]; then
    SOURCE_DIR=$(CDPATH= cd -- "$(dirname "$0")" && pwd)
else
    [ -n "$SOURCE_ARCHIVE_URL" ] || fail "execute no checkout do projeto ou passe a URL do arquivo .tar.gz do código-fonte."
    command -v curl >/dev/null 2>&1 || fail "curl é necessário para baixar o código-fonte."
    command -v tar >/dev/null 2>&1 || fail "tar é necessário para extrair o código-fonte."
    TEMP_DIR=$(mktemp -d)
    mkdir -p "$TEMP_DIR/source"
    printf 'Baixando código-fonte de %s\n' "$SOURCE_ARCHIVE_URL"
    curl -fsSL "$SOURCE_ARCHIVE_URL" -o "$TEMP_DIR/source.tar.gz"
    tar -xzf "$TEMP_DIR/source.tar.gz" --strip-components=1 -C "$TEMP_DIR/source"
    SOURCE_DIR="$TEMP_DIR/source"
fi

BUILD_DIR="$SOURCE_DIR/build-install"
printf 'Compilando %s...\n' "$APP_NAME"
cmake -S "$SOURCE_DIR" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$PREFIX"
cmake --build "$BUILD_DIR" --parallel "${JOBS:-2}"
cmake --install "$BUILD_DIR"

printf '\nInstalação concluída: %s/bin/hue-browser\n' "$PREFIX"
case ":${PATH}:" in
    *":$PREFIX/bin:"*) ;;
    *) printf 'Adicione ao PATH se necessário: export PATH="%s/bin:$PATH"\n' "$PREFIX" ;;
esac
