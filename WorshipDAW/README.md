# Worship DAW

Un DAW pensado para equipos de alabanza: tan directo como REAPER, con una interfaz oscura y moderna,
y **una sola base de código para Mac y iPhone/iPad** (SwiftUI + AVAudioEngine).

## Qué hace hoy (v0.1)

| Área | Funciones |
|---|---|
| **Proyectos** | Plantillas worship (Banda completa, Acústico, Voces, Multitracks en vivo), BPM, tonalidad, compás. Autoguardado. Deshacer/Rehacer. |
| **Arreglo** | Línea de tiempo con regla de compases, grilla con *snap*, zoom (pinch / ⌘+ / ⌘−), seguir cursor. |
| **Estructura** | Secciones de canción (Intro, Verso, Pre‑Coro, Coro, Puente, Interludio, Tag, Vamp, Final) con colores; clic para saltar, "Repetir esta sección" para ensayar. |
| **Clips** | Importar (wav, aiff, mp3, m4a, caf…) desde archivo o arrastrando; mover (también entre pistas), recortar bordes, dividir (S), duplicar, eliminar. Formas de onda en caché. Varios archivos a la vez = una pista por stem. |
| **Grabación** | Graba en las pistas armadas (R) desde micrófono o interfaz. |
| **Mezcla** | Fader con medidor, paneo, EQ de 3 bandas por canal, Mute/Solo/Rec, master. |
| **Plugins** | Inserta Audio Units (AUv2 en Mac, AUv3 en Mac y iPhone) con su interfaz nativa. Los plugins **Worship Sounds** aparecen primero. El estado de cada plugin se guarda en el proyecto. |
| **Click** | Metrónomo con acento, encendido con K. |
| **En vivo** (iPhone) | Pads grandes por sección para navegar la canción en el ensayo o el domingo. |
| **Exportar** | Mezcla a WAV 24‑bit / 48 kHz (render offline) y compartir. |

### Atajos (estilo REAPER)

| Tecla | Acción |
|---|---|
| Espacio | Reproducir / detener |
| R | Grabar |
| S | Dividir en el cursor (clip seleccionado o todos los que cruza) |
| ⌫ | Eliminar clip |
| L | Loop on/off (si no hay loop, usa la sección actual) |
| K | Click |
| [ / ] | Sección anterior / siguiente |
| ← / → | Un compás atrás / adelante |
| Inicio | Volver al inicio |
| M / I | Mezclador / Inspector |
| + / − | Zoom |
| ⌘Z / ⇧⌘Z | Deshacer / Rehacer |
| ⌘I / ⌘E | Importar audio / Exportar mezcla |

## Cómo compilar

Requisitos: Xcode 16 o superior, macOS 15+ para el destino Mac, iOS 18+ para iPhone/iPad.

```bash
brew install xcodegen
cd WorshipDAW
xcodegen generate
open WorshipDAW.xcodeproj
```

Elige el esquema **WorshipDAW-macOS** o **WorshipDAW-iOS**, selecciona tu equipo de firma
(Signing & Capabilities) y ejecuta. Los tests del núcleo corren con:

```bash
swift test --package-path Packages/WorshipDAWCore
```

El workflow `.github/workflows/worship-daw.yml` compila ambas plataformas y corre los tests en cada push.

## Arquitectura

```
WorshipDAW/
├── project.yml                  # XcodeGen: targets iOS y macOS que comparten App/
├── Packages/WorshipDAWCore/     # Modelo puro en Swift (sin audio), con tests
│   ├── Models.swift             # Project, Track, Clip, SongSection, PluginRef…
│   ├── Timeline.swift           # segundos ⇄ compás/tiempo, snap, timecode
│   ├── Editing.swift            # dividir, recortar, mover, secciones, solo/mute
│   ├── Templates.swift          # plantillas worship
│   └── ProjectStore.swift       # carpeta .worship (project.json + Audio/ + Peaks/)
├── App/
│   ├── Audio/                   # AVAudioEngine: reproducción, grabación, click, AU, exportación
│   ├── State/                   # AppModel (biblioteca) y Session (proyecto abierto, deshacer)
│   └── UI/                      # SwiftUI: transporte, arreglo, mezclador, inspector, en vivo
└── Support/                     # entitlements de macOS
```

**Cadena de audio por pista:** `AVAudioPlayerNode → EQ 3 bandas → Inserts AU → Fader (vol/pan) → Master`.
Todo el audio importado o grabado se convierte a 48 kHz estéreo float para que cualquier clip se
reproduzca sin conversión en tiempo real y con sincronía de muestra entre pistas.

**Formato de proyecto:** una carpeta `Nombre.worship/` con `project.json` legible, `Audio/` y `Peaks/`.
En Mac vive en `~/Documents/Worship DAW/`; en iPhone en *Archivos › En mi iPhone › Worship DAW › Proyectos*.

## Próximos pasos sugeridos

- Ruteo de click y guía a salidas separadas (in‑ears) con interfaces multicanal.
- Compensación de latencia de grabación y count‑in.
- Fades y ganancia por clip; crossfades automáticos.
- Automatización de volumen/paneo.
- Mapa de tempo y cambios de compás.
- Transposición / time‑stretch de stems (AVAudioUnitTimePitch).
- Sincronización de proyectos entre Mac y iPhone (iCloud Drive).
- Instrumentos AU/MIDI.
