#!/bin/bash
# Compila ParaEQ para macOS (Apple Silicon + Intel) y lo instala para FL Studio.
set -e
cd "$(dirname "$0")"

if ! command -v cmake >/dev/null; then
  echo "Falta CMake. Instalalo con:  brew install cmake   (o desde https://cmake.org/download/)"
  exit 1
fi
if ! xcode-select -p >/dev/null 2>&1; then
  echo "Faltan las herramientas de Xcode. Instalalas con:  xcode-select --install"
  exit 1
fi

cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release -j "$(sysctl -n hw.ncpu)"

VST3="$HOME/Library/Audio/Plug-Ins/VST3/ParaEQ.vst3"
AU="$HOME/Library/Audio/Plug-Ins/Components/ParaEQ.component"

# Firma ad-hoc (necesaria en Macs con Apple Silicon)
for p in "$VST3" "$AU"; do
  if [ -e "$p" ]; then codesign --force --deep -s - "$p" && echo "Firmado: $p"; fi
done

# Refresca la cache de Audio Units
killall -9 AudioComponentRegistrar 2>/dev/null || true

echo
echo "Listo. Plugins instalados en:"
echo "  $VST3"
echo "  $AU"
echo
echo "En FL Studio: Options > Manage plugins > Find installed plugins."
