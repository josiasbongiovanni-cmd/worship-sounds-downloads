# Mathu Barber · Guía de la Fase 2 (reservas)

## Qué hay de nuevo
- El botón **Reservar turno** ya funciona: servicio → día → horario → confirmar.
  El paso "barbero" aparece solo cuando haya más de un barbero.
- Se muestran **solo los días y horarios realmente libres**: respeta tu horario, el descanso del mediodía, los bloqueos y los turnos ya tomados.
- **Mis turnos**: próximos turnos con **Reprogramar** y **Cancelar** (hasta 2 horas antes) e historial.
- **Corte gratis**: si el cliente lo tiene, se aplica solo al reservar. En "Corte y barba" se descuenta el corte ($12.000) y paga $3.000.

## Reglas configuradas (se pueden cambiar en Supabase → Table Editor → `settings`)
| Regla | Valor | Columna |
|---|---|---|
| Turnos cada | 1 hora | `slot_interval_min` = 60 |
| Reservar con anticipación mínima | 1 hora | `min_notice_min` = 60 |
| Reservar hasta | 30 días adelante | `booking_window_days` = 30 |
| Cancelar/reprogramar hasta | 2 horas antes | `cancel_limit_hours` = 2 |
| Turnos futuros por cliente | máximo 2 | `max_active_bookings` = 2 |

## Cómo actualizar Supabase
Si ya corriste los archivos 1 y 2 en la Fase 1, ahora corré en **SQL Editor**, en orden:
1. `supabase/migrations/20261121000003_precios_reales.sql`
2. `supabase/migrations/20261121000004_reservas.sql`

Si todavía no corriste ninguno, corré los 4 en orden (ver GUIA_FASE_1.md).

Después, en la computadora: `git pull` y `npx expo start`.

## ✅ Qué probar en el celular
| Prueba | Resultado esperado |
|---|---|
| Reservar un Corte para un martes | Muestra 09:00 a 12:00 y 15:00 a 19:00 (no 13 ni 14, por el descanso) |
| Elegir un domingo o lunes | No aparecen: están cerrados |
| Con **otra cuenta** de cliente, mirar el mismo día | El horario que reservaste **ya no aparece** |
| Reservar un 3er turno | Avisa que ya tenés 2 turnos reservados |
| Mis turnos → Reprogramar | Elegís otro día/hora y el turno cambia |
| Mis turnos → Cancelar | Pide confirmación y el turno pasa al historial como "Cancelado" |
| En Supabase, cargar un bloqueo en `time_off` (ej: un martes de 15 a 17, con `barber_id` vacío) | Esos horarios desaparecen |
| En Supabase, en `loyalty_cards` poné `rewards_available` = 1 a tu cliente y reservá | Aparece "¡Usás tu corte GRATIS!" y el precio baja. Después volvelo a 0 |

> Cargar horarios, bloqueos y feriados desde la app (sin entrar a Supabase) llega en la **Fase 3**.
