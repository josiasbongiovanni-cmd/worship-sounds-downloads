# Mathu Barber · Guía de la Fase 1

Esta guía explica cómo conectar la app con tu Supabase y probarla en tu celular con **Expo Go**.
Tarda unos 20 minutos la primera vez.

---

## Paso 1 · Crear la base de datos en Supabase

1. Entrá a <https://supabase.com/dashboard> y abrí tu proyecto (o creá uno nuevo: región **São Paulo**, que es la más cercana a Argentina).
2. En el menú de la izquierda, entrá a **SQL Editor** y tocá **New query**.
3. Abrí el archivo `supabase/migrations/20261121000001_base.sql`, copiá **todo** el contenido, pegalo y tocá **Run**.
   Tiene que aparecer *"Success. No rows returned"*.
4. Repetí lo mismo, **en orden**, con el resto de los archivos de `supabase/migrations/`:
   - `20261121000002_initial_data.sql`: crea el barbero "Mathu", los servicios y el horario (martes a sábado).
   - `20261121000003_precios_reales.sql`: precios reales (Corte $12.000, Corte y barba $15.000) y turnos cada 1 hora.
   - `20261121000004_reservas.sql`: las funciones para reservar, cancelar y reprogramar (Fase 2).
5. Para comprobarlo, entrá a **Table Editor**: tienen que aparecer las tablas `services`, `appointments`, `loyalty_cards`, etc.

> ⚠️ Cada archivo se corre **una sola vez**. Si lo corrés dos veces vas a ver un error de que algo "already exists"; no pasa nada malo.

## Paso 2 · Configurar el login por email

En **Authentication → Sign In / Providers → Email**:
- Mientras probás, **desactivá "Confirm email"**. Así podés crear cuentas de prueba sin confirmar cada email.
- Antes de publicar la app la volvemos a activar (Fase 6).

## Paso 3 · Copiar las claves del proyecto

En **Project Settings → API** (o **Data API / API Keys**) copiá:
- **Project URL**, por ejemplo `https://abcdefgh.supabase.co`
- **anon public key** o **publishable key** (una clave larga)

> La clave *anon/publishable* puede ir dentro de la app sin problema: la seguridad la dan las reglas de la base de datos.
> **Nunca** uses ni compartas la *service_role / secret key*.

## Paso 4 · Correr la app en tu computadora

Necesitás una computadora con Windows o Mac y estos dos programas (gratis):
- **Node.js LTS**: <https://nodejs.org>
- **Git**: <https://git-scm.com>

Abrí la *Terminal* (Mac) o *PowerShell* (Windows) y pegá estos comandos, uno por uno:

```bash
git clone https://github.com/josiasbongiovanni-cmd/worship-sounds-downloads.git
cd worship-sounds-downloads
git checkout claude/barber-app-loyalty-lf8g0v
cd barberia-app
npm install
```

Después, adentro de la carpeta `barberia-app`, creá un archivo llamado **`.env`** (copiando `.env.example`) y completalo con tus datos:

```
EXPO_PUBLIC_SUPABASE_URL=https://abcdefgh.supabase.co
EXPO_PUBLIC_SUPABASE_ANON_KEY=la-clave-larga-que-copiaste
```

Y arrancá la app:

```bash
npx expo start
```

Va a aparecer un **código QR** en la terminal.

## Paso 5 · Abrirla en tu celular

1. Instalá **Expo Go** en tu celular (Play Store o App Store).
2. **Android:** abrí Expo Go y tocá *Scan QR code*. **iPhone:** escaneá el QR con la cámara.
3. El celular y la computadora tienen que estar en el **mismo WiFi**. Si no conecta, cortá la app con `Ctrl + C` y corré `npx expo start --tunnel`.

## Paso 6 · Convertirte en administrador

1. En la app, tocá **Crear mi cuenta** y registrate con tu email.
2. En Supabase → **SQL Editor**, abrí `supabase/scripts/hacerme_admin.sql`, cambiá `TU-EMAIL@ejemplo.com` por tu email (aparece **dos veces**) y tocá **Run**.
3. En la app, cerrá sesión y volvé a entrar: ahora ves el **Panel de administración**.

---

## ✅ Qué probar en esta fase

| Prueba | Resultado esperado |
|---|---|
| Crear una cuenta con un celular inválido (ej: "123") | Avisa "Ingresá un celular válido" |
| Crear una cuenta de cliente | Entra a "Hola, (nombre)" con la tarjeta en **0 de 9** y los 3 servicios |
| Ir a **Mi tarjeta** | Se ven 9 sellos vacíos y el círculo "GRATIS" |
| Ir a **Perfil**, cambiar el nombre y guardar | Dice "Listo" y queda guardado |
| Cerrar la app y volver a abrirla | Seguís logueado |
| Con tu cuenta admin, ir a **Clientes** | Ves al cliente de prueba con 0 sellos |
| En Supabase, cambiar el precio de "Corte" en Table Editor → services | Al volver a la pantalla Inicio se ve el precio nuevo |

**Para probar la seguridad (opcional):** en Supabase → Table Editor → `loyalty_cards`, cambiá `stamps` de un cliente a 7. La tarjeta del cliente va a mostrar **7 de 9**. Desde la app, el cliente no tiene ninguna forma de cambiar ese número. Volvé a ponerlo en 0 después.

---

## Para el desarrollador: pruebas automáticas

```bash
npm test          # validaciones (teléfono, precios, etc.)
npm run test:db   # reglas de la base de datos (necesita PostgreSQL instalado)
npm run typecheck
```
