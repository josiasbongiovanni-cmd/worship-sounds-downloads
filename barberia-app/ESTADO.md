# Mathu Barber · Estado del proyecto

_Última actualización: 24/11/2026_

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
1. **Supabase** ✅ listo (24/11): proyecto "Mathu Barber" con la base completa (5 archivos SQL), confirmación de email
   desactivada para probar, y la app conectada (`.env`). Las 87 pruebas pasaron contra la base real.
2. **Celular**: en la Mac → ver `COMO_ABRIR_EN_EL_CELULAR.md`. Registrarse en la app y pasarle el email a Claude para hacerlo admin.
3. **Fase 4**: fidelidad (sumar sellos al completar, canje, historial, ajuste manual y estadísticas).
4. **Fase 5**: notificaciones push.
5. **Fase 6**: diseño final, APK, TestFlight y App Store.
