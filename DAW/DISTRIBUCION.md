# Distribuir Worship DAW (Mac y Windows)

Esta guía explica cómo pasar de "compila en mi computadora" a "cualquiera la descarga y la abre sin advertencias".

## 1. Licencias que hay que tener en regla antes de lanzar

- **JUCE 8:** Worship DAW está hecho con JUCE, que tiene licencia doble:
  - **AGPLv3:** obliga a publicar el código fuente de la app.
  - **Licencia comercial:** para una app de código cerrado.
    - **"Starter":** gratuita mientras los ingresos sean menores a 20.000 USD al año.
    - **"Indie" / "Pro":** de pago, cuando se supera ese límite.

  Regístrala en juce.com antes de distribuir. Confirma los montos actuales en su página: cambian con el tiempo.
- **ASIO (Windows):** el ASIO SDK de Steinberg tiene licencia doble: acuerdo propietario o GPLv3.
  - Para una app de código cerrado corresponde el acuerdo propietario. Léelo y acéptalo en steinberg.net/developers.
  - El SDK no se sube a este repositorio: se descarga al compilar.
  - Si usas el nombre o el logo "ASIO", sigue las pautas de marca de Steinberg.
- **VST3:** el SDK de VST3 (que JUCE usa para cargar plugins) pasó a licencia MIT en 2025.
- **Tipografía Inter:** licencia SIL OFL (en `Resources/Fonts`), compatible con apps comerciales.

## 2. ASIO en Windows

Sin ASIO la app funciona con **WASAPI** (y DirectSound), que anda con cualquier interfaz. ASIO da menor latencia con los drivers de cada fabricante (Focusrite, Universal Audio, RME, PreSonus…).

Compilar con ASIO en tu PC:

```powershell
cd DAW
.\scripts\get-asio-sdk.ps1 -AcceptLicense      # descarga el SDK a ThirdParty\asiosdk (no se sube a git)
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

Si la descarga automática falla (Steinberg cambia el enlace a veces):
1. Baja el ZIP a mano desde steinberg.net/developers.
2. Descomprímelo.
3. Pasa la carpeta a CMake: `cmake -B build -DWDAW_ASIO_SDK="C:\ruta\asiosdk" ...`.

CMake avisa en la consola si ASIO quedó activado o no.

La primera vez que se abre la app en Windows, si hay una interfaz con driver ASIO propio, la elige sola. Se saltan los drivers genéricos como ASIO4ALL o Realtek. Después se puede cambiar en Preferencias › Audio, donde también está el botón del panel de control ASIO del fabricante.

En GitHub, crea la **variable** del repositorio `ACCEPT_ASIO_LICENSE` con el valor `true`, en Settings › Secrets and variables › Actions › Variables. Hazlo solo después de aceptar la licencia.

## 3. Firma en Mac (Developer ID + notarización)

Sin firma, macOS dice "no se puede verificar el desarrollador" y hay que abrirla con clic derecho › Abrir.

1. **Apple Developer Program** (99 USD al año): developer.apple.com/programs.
2. **Certificado "Developer ID Application":** en Xcode › Settings › Accounts › Manage Certificates › "+" › Developer ID Application.
3. **Exportar el certificado:** en Acceso a Llaveros, clic derecho sobre el certificado › Exportar… › formato .p12, con contraseña.
4. **Clave de API para notarizar:** en App Store Connect › Users and Access › Integrations › App Store Connect API, crea una clave con rol "Developer". Descarga el archivo .p8 y anota el **Key ID** y el **Issuer ID**.
5. **Secretos en GitHub** (Settings › Secrets and variables › Actions › Secrets):

   | Secreto | Qué es |
   |---|---|
   | `MAC_CERT_P12_BASE64` | el .p12 en base64: `base64 -i certificado.p12 \| pbcopy` |
   | `MAC_CERT_PASSWORD` | la contraseña del .p12 |
   | `NOTARY_KEY_P8_BASE64` | el .p8 en base64: `base64 -i AuthKey_XXXX.p8 \| pbcopy` |
   | `NOTARY_KEY_ID` | el Key ID |
   | `NOTARY_ISSUER` | el Issuer ID |

En tu Mac, sin GitHub:

```bash
cd DAW
cmake -B build -G Xcode && cmake --build build --config Release
export MAC_SIGN_IDENTITY="Developer ID Application: Tu Nombre (TEAMID)"
export APPLE_ID="tu@correo" APPLE_TEAM_ID="TEAMID" APPLE_APP_PASSWORD="xxxx-xxxx-xxxx-xxxx"   # contraseña de app en appleid.apple.com
./scripts/package-mac.sh "build/WorshipDAW_artefacts/Release/Worship DAW.app"
```

El resultado es `dist/WorshipDAW-<versión>-mac.dmg`, firmado, notarizado y con el ticket "grapado" (stapled). Funciona en Mac con Apple Silicon y con Intel.

## 4. Firma en Windows

Sin firma, Windows SmartScreen muestra "Windows protegió tu PC / editor desconocido".

Opciones de certificado de firma de código:
- **OV o EV de una autoridad** (DigiCert, Sectigo, SSL.com…):
  - Desde 2023 la clave privada debe vivir en hardware (token USB o HSM en la nube).
  - Con un token USB se firma en tu PC con `WIN_CERT_SUBJECT` (el nombre del certificado).
  - Algunos proveedores dan la firma en la nube con su propia herramienta.
- **Firma en la nube de Microsoft (Azure):** suele ser la opción más barata para equipos chicos, pero tiene requisitos de país y de verificación de identidad. Revisa la disponibilidad actual en la documentación de Microsoft.
- **Archivo .pfx:** solo si tu proveedor lo entrega así. Secretos `WIN_CERT_PFX_BASE64` y `WIN_CERT_PASSWORD`.

En tu PC:

```powershell
cd DAW
choco install innosetup -y        # o desde jrsoftware.org
$env:WIN_CERT_SUBJECT = "Worship Sounds"      # o WIN_CERT_PFX + WIN_CERT_PASSWORD
.\scripts\package-windows.ps1
```

El resultado es `dist\WorshipDAW-<versión>-Windows-Setup.exe`: un instalador en español, con acceso directo y desinstalador, y con el .exe y el instalador firmados. El runtime de C++ va dentro del .exe, así que no hace falta instalar nada más.

## 5. Publicar una versión

1. Sube el número en `CMakeLists.txt` (`project(WorshipDAW VERSION x.y.z)`).
2. Crea un tag: `git tag v0.3.0 && git push origin v0.3.0`.
3. GitHub Actions compila Mac y Windows, firma (si cargaste los secretos) y crea un **borrador de Release** con el .dmg y el instalador.
4. Revisa el borrador y publícalo.

Sin secretos todo funciona igual, pero los instaladores salen sin firmar: sirven para probar, no para el público.
