#!/usr/bin/env bash
# Firma, notariza y empaqueta Worship DAW para Mac (.dmg), listo para descargar sin advertencias de Gatekeeper.
#
#   scripts/package-mac.sh "build/WorshipDAW_artefacts/Release/Worship DAW.app" [salida.dmg]
#
# Variables de entorno (sin ellas, se hace una firma "ad-hoc": sirve para probar, pero macOS avisa al abrir):
#   MAC_SIGN_IDENTITY   "Developer ID Application: Tu Nombre (TEAMID)" (certificado en el llavero)
#   Notarización, una de estas dos opciones:
#     a) Clave de API de App Store Connect: NOTARY_KEY_PATH (.p8), NOTARY_KEY_ID, NOTARY_ISSUER
#     b) Apple ID:                          APPLE_ID, APPLE_TEAM_ID, APPLE_APP_PASSWORD (contraseña de app)
set -euo pipefail

APP="${1:?Uso: package-mac.sh <ruta a Worship DAW.app> [salida.dmg]}"
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/.." && pwd)"
VERSION="$(/usr/libexec/PlistBuddy -c 'Print :CFBundleShortVersionString' "$APP/Contents/Info.plist" 2>/dev/null || echo 0.0.0)"
DMG="${2:-$ROOT/dist/WorshipDAW-$VERSION-mac.dmg}"
ENTITLEMENTS="$ROOT/Resources/WorshipDAW.entitlements"
IDENTITY="${MAC_SIGN_IDENTITY:-}"

[ -d "$APP" ] || { echo "No existe $APP" >&2; exit 1; }
mkdir -p "$(dirname "$DMG")"

echo "== Firmando $APP"
if [ -n "$IDENTITY" ]; then
    # Hardened Runtime + sello de tiempo: requisitos de la notarización
    codesign --force --timestamp --options runtime --entitlements "$ENTITLEMENTS" --sign "$IDENTITY" "$APP"
else
    echo "   (sin MAC_SIGN_IDENTITY: firma ad-hoc; macOS pedirá permiso para abrirla la primera vez)"
    codesign --force --options runtime --entitlements "$ENTITLEMENTS" --sign - "$APP"
fi
codesign --verify --strict --verbose=2 "$APP"

echo "== Creando $DMG"
STAGE="$(mktemp -d)"
trap 'rm -rf "$STAGE"' EXIT
cp -R "$APP" "$STAGE/"
ln -s /Applications "$STAGE/Aplicaciones"
rm -f "$DMG"
hdiutil create -volname "Worship DAW" -srcfolder "$STAGE" -fs HFS+ -format UDZO -ov "$DMG" >/dev/null

if [ -z "$IDENTITY" ]; then
    echo "Listo (sin firma de Developer ID): $DMG"
    exit 0
fi
codesign --force --timestamp --sign "$IDENTITY" "$DMG"

echo "== Notarizando (puede tardar unos minutos)"
if [ -n "${NOTARY_KEY_PATH:-}" ]; then
    AUTH=(--key "$NOTARY_KEY_PATH" --key-id "${NOTARY_KEY_ID:?}" --issuer "${NOTARY_ISSUER:?}")
elif [ -n "${APPLE_ID:-}" ]; then
    AUTH=(--apple-id "$APPLE_ID" --team-id "${APPLE_TEAM_ID:?}" --password "${APPLE_APP_PASSWORD:?}")
else
    echo "Firmado pero SIN notarizar (faltan las credenciales de notarización): $DMG"
    exit 0
fi
RESULT="$(xcrun notarytool submit "$DMG" "${AUTH[@]}" --wait --output-format json)"
echo "$RESULT"
if ! echo "$RESULT" | grep -q '"status" *: *"Accepted"'; then
    ID="$(echo "$RESULT" | sed -n 's/.*"id" *: *"\([^"]*\)".*/\1/p' | head -1)"
    [ -n "$ID" ] && xcrun notarytool log "$ID" "${AUTH[@]}" || true
    echo "Apple rechazó la notarización (el registro de arriba dice por qué)." >&2
    exit 1
fi
xcrun stapler staple "$DMG"
spctl --assess --type open --context context:primary-signature --verbose=2 "$DMG" || true
echo "Listo, firmado y notarizado: $DMG"
