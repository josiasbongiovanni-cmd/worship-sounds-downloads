# Worship DAW frente a REAPER

Objetivo: que alguien que viene de REAPER se sienta en casa. Mismo modelo mental (pistas, ítems,
cursor de edición, selección de tiempo, marcadores), mismos atajos por defecto y mismas
preferencias de audio y MIDI. La diferencia es el diseño visual y las funciones pensadas para worship.

Estado: ✅ hecho · 🟡 parcial · ⏳ pendiente

## 1. Cómo funciona REAPER (y qué copiamos)

### Transporte y cursores
| REAPER | Worship DAW | Estado |
|---|---|---|
| Cursor de edición y cursor de reproducción separados. Clic en la regla o en una pista mueve el cursor de edición. | `editCursor` y `playhead` separados. | ✅ |
| La reproducción arranca en el cursor de edición; al detener vuelve ahí. | Igual. | ✅ |
| Espacio = reproducir/detener. ⌘R (Ctrl+R) = grabar. R = repetir. | Igual. | ✅ |
| Inicio / Fin = principio / final del proyecto. | Igual. | ✅ |
| Arrastrar en la regla = selección de tiempo; el loop la sigue. Esc la quita. | Igual. | ✅ |
| Pausa (Enter), scrub, avance rápido. | — | ⏳ |

### Pistas (Track Control Panel)
| REAPER | Worship DAW | Estado |
|---|---|---|
| Volumen, paneo, Mute, Solo, armar (R). | Igual en la cabecera y en el mezclador. | ✅ |
| Clic derecho en el botón de armar: elegir entrada (mono, estéreo, ninguna). | Igual. | ✅ |
| Grabación multipista: cada pista armada graba su entrada al mismo tiempo. | Igual (se graba la interfaz completa y cada pista toma sus canales). | ✅ |
| Monitoreo de entrada (escucharte con efectos mientras grabas): apagado / encendido / automático estilo cinta. | Igual, en el menú del botón R. La entrada pasa por el EQ, los plugins y el fader de la pista. | ✅ |
| Compensación de latencia de grabación (latencia del driver + ajuste manual). | Igual, en Preferencias › Audio › Grabación. | ✅ |
| Cuenta previa antes de grabar. | 0, 1 o 2 compases; la cuenta no queda en la toma. | ✅ |
| Botón FX: cadena de plugins por pista. | Inserts por pista en el mezclador. | ✅ |
| Carpetas (folder tracks), sends/receives, buses. | — | ⏳ |
| Envolventes / automatización (volumen, paneo, parámetros de plugins). | — | ⏳ |
| ⌘T = insertar pista. | Igual. | ✅ |

### Ítems (clips) y edición
| REAPER | Worship DAW | Estado |
|---|---|---|
| Mover ítems (también entre pistas), recortar bordes, snap a la grilla. | Igual. | ✅ |
| S = dividir en el cursor (ítems seleccionados o todos los que cruza). | Igual. | ✅ |
| ⌘D duplicar, ⌫ eliminar, ⌘Z / ⇧⌘Z deshacer / rehacer. | Igual. | ✅ |
| Fades y volumen por ítem con las esquinas y el borde superior. | — | ⏳ |
| Tomas (takes) y comping. | Cada grabación crea un ítem nuevo. | 🟡 |
| Ripple editing, crossfades automáticos. | — | ⏳ |

### Marcadores y regiones
| REAPER | Worship DAW | Estado |
|---|---|---|
| M = insertar marcador; 1–9 = ir al marcador N. | Igual. | ✅ |
| Regiones con nombre. | "Secciones" (Intro, Verso, Coro…) con colores; clic para saltar, repetir sección. | ✅ |
| Gestor de regiones y marcadores, render por región. | — | ⏳ |

### Mezclador
| REAPER | Worship DAW | Estado |
|---|---|---|
| ⌘M abre el mezclador. | Igual. | ✅ |
| Fader, paneo, medidor, M/S/R, master. | Igual, más EQ de 3 bandas por canal. | ✅ |

### Render
| REAPER | Worship DAW | Estado |
|---|---|---|
| ⌥⌘R (Ctrl+Alt+R) renderizar el master a WAV. | WAV 24-bit / 48 kHz, render offline. | ✅ |
| Render de stems, regiones, MP3, normalización. | — | ⏳ |

## 2. Preferencias › Audio (interfaz de audio)

REAPER en Mac usa Core Audio: eliges dispositivo de entrada y salida, pides frecuencia de muestreo
y tamaño de buffer, y ves la latencia resultante. Lo mismo aquí:

- **Mac:** lista de todas las interfaces Core Audio con su número de entradas y salidas.
  Entrada y salida separadas, frecuencia de muestreo, buffer (32–2048) y latencia calculada.
  Igual que REAPER, por defecto **no** cambia la frecuencia ni el buffer del dispositivo
  ("La del dispositivo"), salvo que lo pidas.
- **iPhone/iPad:** interfaces USB-C / Lightning, micrófono interno o auriculares (AVAudioSession).
  Pide todas las entradas disponibles para grabar multicanal desde la interfaz.

## 3. Preferencias › MIDI (controladores)

- Detecta todos los controladores con CoreMIDI (USB, Bluetooth, red) al conectarlos, y puedes
  desactivar los que no uses (como "Enable input" en REAPER).
- **Aprender MIDI (Learn):** clic derecho en un canal del mezclador › Aprender MIDI › Volumen,
  Paneo, Mute, Solo o Armar, y mueve el control. El transporte (reproducir, grabar, repetir,
  marcadores, inicio) se aprende en Preferencias › MIDI. Las asignaciones se guardan en la app.
- ⏳ Superficies de control Mackie Control / HUI (faders motorizados) y OSC.
- ✅ Pistas de instrumento (MIDI) con cualquier instrumento AU (pianos, pads, sintes, samplers). Por defecto, el
  piano General MIDI de Apple en Mac y el sampler de Apple en iPhone. Insertar con ⇧⌘T o el menú "+ Pista".
- ✅ Tocar en vivo: el teclado suena en las pistas de instrumento armadas (o en la seleccionada si no hay ninguna armada),
  directo desde el hilo de CoreMIDI para mínima latencia. Notas, sustain, pitch bend y CC pasan al instrumento.
- ✅ Grabar MIDI (⌘R) en las pistas de instrumento armadas, compensando la latencia de salida, y reproducirlo
  sincronizado con el audio; también se incluye al renderizar.
- ⏳ Editor piano roll (mover, alargar y dibujar notas), cuantizar.

## 4. Plugins: que tus plugins abran igual que en REAPER

REAPER carga VST, VST3, AU y CLAP. Worship DAW usa **Audio Units**, el formato nativo de Apple:

| Plataforma | Formato | Tus plugins Worship Sounds |
|---|---|---|
| Mac | AU v2 y AUv3 | ✅ Ya funcionan: los instaladores incluyen AU. Aparecen primero en la lista y abren con su propia interfaz. |
| iPhone/iPad | Solo AUv3 (extensión de app) | Hay que compilar la versión AUv3 para iOS. JUCE la genera activando el formato "AUv3" en el proyecto. |

- El estado de cada plugin (presets, perillas) se guarda dentro del proyecto, como en REAPER.
- ⏳ VST3 en Mac: requiere integrar el SDK de Steinberg (C++). Tus plugins ya traen AU, así que no
  hace falta para ellos; se necesitaría para plugins de terceros que solo existen en VST3.

## 5. Próximas fases sugeridas

1. ~~Monitoreo de entrada con efectos y compensación de latencia al grabar.~~ ✅
2. Fades, volumen por ítem y crossfades.
3. Envolventes de automatización (volumen, paneo, parámetros de plugins), con escritura por MIDI.
4. Carpetas, sends y buses (click y guía a salidas separadas para in-ears).
5. Mackie Control / HUI para controladores con faders motorizados.
6. ~~Pistas MIDI con instrumentos AU.~~ ✅ (falta el editor piano roll)
7. Editor de acciones y atajos personalizables (como la lista de acciones de REAPER, F4).
