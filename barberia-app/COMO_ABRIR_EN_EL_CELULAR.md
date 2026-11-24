# Cómo abrir Mathu Barber en tu celular (desde la Mac)

Supabase ya está configurado y la app ya está conectada. Solo seguí estos pasos.

## La primera vez (unos 10 minutos)

1. Instalá **Node.js** (versión LTS) desde <https://nodejs.org>.
2. Instalá **Expo Go** en tu celular (App Store o Play Store).
3. Abrí la app **Terminal** de la Mac (Cmd + Espacio, escribí "Terminal" y Enter) y pegá estos comandos, **uno por uno**:

```bash
git clone https://github.com/josiasbongiovanni-cmd/worship-sounds-downloads.git
cd worship-sounds-downloads
git checkout claude/barber-app-loyalty-lf8g0v
cd barberia-app
npm install
npx expo start
```

> Si la Mac pregunta por instalar "herramientas de desarrollo" al usar `git`, aceptá y volvé a pegar el comando.

4. Aparece un **código QR**. Escanealo con la **cámara del iPhone** (o con la app Expo Go en Android).
   La Mac y el celular tienen que estar en el **mismo WiFi**.
   Si no conecta: cortá con `Ctrl + C` y usá `npx expo start --tunnel`.

## Las veces siguientes

```bash
cd worship-sounds-downloads/barberia-app
git pull
npm install
npx expo start
```

## Convertirte en administrador

1. En la app tocá **Crear mi cuenta** y registrate.
2. Pasale tu email a Claude: él te convierte en admin.
3. Cerrá sesión y volvé a entrar: vas a ver la **Agenda** y **Gestión**.
4. Lo primero: **Gestión → Días y horarios** y cargá tu horario real.
