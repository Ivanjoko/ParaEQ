#!/bin/bash
# Doble clic para instalar ParaEQ (VST3 + AU) en tu Mac.
cd "$(dirname "$0")"
mkdir -p ~/Library/Audio/Plug-Ins/VST3 ~/Library/Audio/Plug-Ins/Components
rm -rf ~/Library/Audio/Plug-Ins/VST3/ParaEQ.vst3 ~/Library/Audio/Plug-Ins/Components/ParaEQ.component
cp -R ParaEQ.vst3 ~/Library/Audio/Plug-Ins/VST3/
cp -R ParaEQ.component ~/Library/Audio/Plug-Ins/Components/
# Quita el bloqueo de macOS a archivos descargados
xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/VST3/ParaEQ.vst3 ~/Library/Audio/Plug-Ins/Components/ParaEQ.component
killall -9 AudioComponentRegistrar 2>/dev/null
echo "ParaEQ instalado. En FL Studio: Options > Manage plugins > Find installed plugins."
