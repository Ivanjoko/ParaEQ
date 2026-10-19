ParaEQ v2 - Ecualizador parametrico de 6 bandas para macOS (VST3 + AU)
Funciona en FL Studio para Mac (Apple Silicon e Intel) y en cualquier DAW con VST3 o AU.

INCLUYE
- 6 bandas: Campana, Shelf graves, Shelf agudos o Notch (frecuencia, ganancia y Q)
- Filtros de corte HPF y LPF, 12/24/36/48 dB/oct (Butterworth)
- Entrada y salida (+-24 dB) con medidores, y Bypass
- Analizador de espectro de la salida (FFT de 4096 puntos)
- Rango de la grafica ajustable (+-6, 12, 18 o 24 dB) para ver detalles finos

PRECISION
- Procesado interno en doble precision (64 bit), tambien si el DAW envia audio en 32 bit
- Resolucion de parametros: 0.01 dB, 0.01 Hz y 0.01 de Q. Puedes escribir valores exactos
  (doble clic en un valor; admite "2.5k")
- Shift mientras arrastras un punto de la grafica = ajuste 10 veces mas fino
- Cambios de parametros suavizados (20 ms) y activar/desactivar con fundido (sin clics)
- La curva dibujada se calcula con los mismos coeficientes que usa el audio

COMPILAR (en tu Mac)
1) Herramientas de Xcode:  xcode-select --install
2) CMake:                  brew install cmake   (o desde cmake.org)
3) En la carpeta del proyecto:  ./build_mac.sh
   (descarga JUCE, compila un binario universal, instala y firma el plugin)

Se instala en:
  ~/Library/Audio/Plug-Ins/VST3/ParaEQ.vst3
  ~/Library/Audio/Plug-Ins/Components/ParaEQ.component

EN FL STUDIO
Options > Manage plugins > Find installed plugins. Aparece en la lista de efectos como "ParaEQ".
Si no lo encuentra, copialo a las carpetas del sistema y vuelve a buscar:
  sudo cp -R ~/Library/Audio/Plug-Ins/VST3/ParaEQ.vst3 /Library/Audio/Plug-Ins/VST3/
  sudo cp -R ~/Library/Audio/Plug-Ins/Components/ParaEQ.component /Library/Audio/Plug-Ins/Components/
o anade la carpeta en "Plugin search paths" del Plugin Manager.
