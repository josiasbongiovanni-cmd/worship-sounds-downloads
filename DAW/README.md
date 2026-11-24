# Worship DAW (Mac y Windows)

DAW para equipos de alabanza, con el flujo de trabajo de REAPER y una interfaz moderna.
Un solo código en C++ (JUCE 8) para macOS y Windows.

## Qué incluye

- **Arreglo:** pistas de audio, guía e instrumento (MIDI); clips con mover/recortar/fundidos, dividir (S), duplicar, copiar/pegar, selección de tiempo, secciones (Intro, Verso, Coro…), marcadores 1–9, repetición (R) y "loop de sección".
- **Buses y envíos:**
  - Cada pista sale al Master, a un bus (submezcla) o directo a una salida de la interfaz.
  - Envíos pre-fader (mezclas de monitoreo) o post-fader (reverb, delay), sin bucles.
  - El solo respeta los buses, y la latencia de los plugins se compensa en todo el ruteo.
  - Reverb y Delay incluidos.
  - Las plantillas con voces traen un bus "Reverb".
- **Automatización:**
  - Carriles debajo de cada pista para volumen, paneo, envíos y cualquier parámetro de plugin.
  - Modos Apagado, Leer, Touch y Latch.
  - Clic agrega un punto, arrastrar lo mueve, Alt+clic o doble clic lo borra.
  - Los faders siguen a la automatización durante la reproducción.
- **Plugins VST3 y AU** (inserts por pista, instrumento por pista, cadena en el master), con compensación de latencia (PDC). Los estados se guardan dentro del proyecto.
- **Interfaces de audio:** CoreAudio (Mac), WASAPI y DirectSound (Windows); entradas mono/estéreo por pista, monitoreo (apagado/encendido/automático), salidas directas para click y guía (in-ears).
- **Grabación:** punch-in, cuenta previa y compensación de latencia (de la interfaz + plugins + ajuste manual). Se graba directo al disco, en la carpeta Audio/ del proyecto.
- **MIDI:** controladores en vivo, grabación de notas, pedal sustain, pitch bend y aftertouch. Piano roll con cuantización (fuerza y duración), transponer, legato, humanizar y carril de velocidad. Aprender MIDI para faders, mute/solo y transporte.
- **Exportar** WAV/FLAC/AIFF/OGG (16/24/32 bits, normalizar, dither, cambio de frecuencia) y stems por pista.
- **Guardado seguro:** autoguardado, copia .bak y copias en Backups/. Los proyectos borrados van a la Papelera.
- **Atajos de REAPER:** espacio, Cmd/Ctrl+R, R, S, M, 1–9, [ ], Cmd/Ctrl+T, + / −…

## Compilar

Necesitas CMake 3.22 o más nuevo. JUCE se descarga solo.

**Mac** (Xcode 15 o más nuevo):

```bash
cd DAW
cmake -B build -G Xcode
cmake --build build --config Release
open "build/WorshipDAW_artefacts/Release/Worship DAW.app"
```

**Windows** (Visual Studio 2022 con "Desarrollo para el escritorio con C++"):

```bat
cd DAW
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
build\WorshipDAW_artefacts\Release\"Worship DAW.exe"
```

**Pruebas:** compila el objetivo `WorshipDAWTests` y ejecútalo. Al final debe decir "TODAS LAS PRUEBAS PASARON".

**CI** (`.github/workflows/daw-juce.yml`):

- Las pruebas corren en Linux en cada push.
- Windows y macOS se compilan a mano (Actions → Run workflow) o al crear un tag `v*`. El instalable queda como artefacto descargable.

## Pendiente antes del lanzamiento

- ASIO en Windows: requiere el SDK de Steinberg y aceptar su licencia; hoy se usa WASAPI. Activar con `JUCE_ASIO=1` y la ruta del SDK.
- Escaneo de plugins en un proceso separado. Hoy, si un plugin se cuelga al escanearlo, queda marcado y se salta la próxima vez.
- Firma y notarización en Mac (cuenta de Apple Developer) y firma de código en Windows.
- Carpetas de pistas, superficies Mackie Control, exportar a MP3 y automatización con curvas (hoy los tramos son rectos).

## Código

| Carpeta | Contenido |
|---|---|
| `Source/Model/` | Proyecto (en pulsos), edición, guardado JSON |
| `Source/Engine/` | Motor de audio, plugins, archivos, exportación |
| `Source/UI/` | Interfaz (tema, arreglo, mezclador, piano roll, diálogos) |
| `Tests/` | Pruebas del modelo y del motor |

Los textos con acentos van con el sufijo `_u` (UTF-8). `python3 lint_utf8.py` lo comprueba.
