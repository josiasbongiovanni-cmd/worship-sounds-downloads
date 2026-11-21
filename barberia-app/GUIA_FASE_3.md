# Mathu Barber · Guía de la Fase 3 (panel del administrador)

Todo esto lo puede hacer **solo el administrador**. El servidor bloquea cualquier intento de un cliente,
aunque use una app modificada.

## Pestañas del admin
| Pestaña | Para qué |
|---|---|
| **Agenda** | Turnos de la semana. Tocás un día y ves sus turnos, con total del día. Botones: **Completado**, **No vino**, **Cancelar**. Tocando el teléfono abrís **WhatsApp** con el cliente. |
| **Clientes** | Lista de clientes y sus sellos (se amplía en la Fase 4). |
| **Gestión** | Servicios, días y horarios, feriados y bloqueos, reglas de reserva. |
| **Perfil** | Tus datos y cerrar sesión. |

## Primera vez: cargá tu horario real
1. **Gestión → Días y horarios**
2. Activá los días que abrís (el interruptor de cada día).
3. Tocá cada hora para cambiarla. Si no cerrás al mediodía, tocá la ❌ roja de la segunda franja.
4. Usá **"Copiar a los demás días"** para no cargar día por día.
5. **Guardar horario**. Si algún turno ya reservado queda afuera, la app te avisa.

## Reglas de los estados de turno
- **Completado** y **No vino** se habilitan el mismo día del turno (no antes).
- **No vino** se puede corregir a "Completado" o "Reservado".
- Un turno **Cancelado** se puede **reactivar** si el horario sigue libre.
- Un turno **Completado** no se puede deshacer desde la agenda (porque suma a la tarjeta).
  Los errores se corrigen con el ajuste manual de la tarjeta (Fase 4).
- Queda registrado **quién** cambió cada estado y **cuándo**.

## Cómo actualizar Supabase
En **SQL Editor**, corré: `supabase/migrations/20261122000005_panel_admin.sql`
(Si todavía no corriste los anteriores, corré todos en orden, del 1 al 5.)

Después: `git pull` y `npx expo start`.

## ✅ Qué probar
| Prueba | Resultado esperado |
|---|---|
| Gestión → Días y horarios: cargar tu horario y guardar | Con una cuenta de cliente, al reservar se ven los horarios nuevos |
| Cerrar un día que tiene un turno reservado | Te avisa que hay 1 turno fuera de horario (y aparece en rojo en la Agenda) |
| Gestión → Servicios → cambiar el precio del Corte | El cliente ve el precio nuevo; los turnos viejos mantienen su precio |
| Crear un servicio nuevo | Aparece para reservar enseguida |
| Desactivar un servicio | Los clientes dejan de verlo |
| Feriados: bloquear un día completo | Ese día desaparece para los clientes |
| Feriados: bloquear unas horas | Solo esas horas desaparecen |
| Agenda: en un turno de hoy, tocar **Completado** | Pide confirmación y queda en verde |
| Agenda: tocar el teléfono del cliente | Abre WhatsApp con un mensaje armado |
| Reglas: cambiar "Cancelar hasta" a 4 h | El cliente ya no puede cancelar con menos de 4 h |
