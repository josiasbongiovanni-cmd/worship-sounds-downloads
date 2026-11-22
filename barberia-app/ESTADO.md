# Mathu Barber · Estado del proyecto

_Última actualización: 22/11/2026_

## Hecho ✅
- **Fase 1**: base de datos, seguridad, login y registro, roles, logo e íconos.
- **Fase 2**: reservas (servicio → día → horario), cancelar y reprogramar, y corte gratis aplicado al reservar.
- **Fase 3**: panel del admin (agenda, estados de turno, servicios, días y horarios, feriados y bloqueos, reglas).
- **Pruebas automáticas**: 87 de base de datos y 27 de la app, todas OK.

## Datos cargados
- Servicios: **Corte $12.000** (1 h) y **Corte y barba $15.000** (1 h). "Barba" está desactivado.
- Turnos cada 1 hora. Cancelar hasta 2 h antes. Máximo 2 turnos por cliente.
- El horario es de ejemplo (martes a sábado, 9–13 y 15–20). **El admin lo carga desde la app.**

## Para retomar 🔜
1. **Supabase**: ya se creó el proyecto `mathu-barber`, pero el token guardado solo ve el proyecto viejo "Activa-vgg".
   - Crear un token nuevo en supabase.com/dashboard/account/tokens y guardarlo en el formulario seguro.
   - Después, Claude: corre los 5 SQL de `supabase/migrations/`, desactiva la confirmación de email, completa el `.env` y prueba todo contra el proyecto real.
   - ⚠️ **No tocar "Activa-vgg"** (es otra app, con usuarios reales).
2. **Celular**: en la Mac, `npx expo start` y escanear el QR con Expo Go (ver `GUIA_FASE_1.md`, pasos 4 a 6).
3. **Fase 4**: fidelidad (sumar sellos al completar, canje, historial, ajuste manual y estadísticas).
4. **Fase 5**: notificaciones push.
5. **Fase 6**: diseño final, APK, TestFlight y App Store.
