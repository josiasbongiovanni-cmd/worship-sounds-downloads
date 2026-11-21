-- =====================================================================
-- Mathu Barber - precios y reglas reales (pedido del dueño)
-- =====================================================================
update public.services set price = 12000, duration_min = 60, description = 'Corte de pelo' where name = 'Corte';
update public.services set name = 'Corte y barba', price = 15000, duration_min = 60, description = 'Corte completo y arreglo de barba'
  where name in ('Corte + barba', 'Corte y barba');
update public.services set active = false where name = 'Barba';

-- Turnos cada 1 hora
update public.settings set slot_interval_min = 60;

-- Máximo de turnos futuros que un cliente puede tener reservados a la vez (anti-abuso)
alter table public.settings
  add column if not exists max_active_bookings int not null default 2 check (max_active_bookings between 1 and 10);

-- Los servicios se pueden leer siempre (aunque estén desactivados), así el historial
-- de un cliente sigue mostrando el nombre de un servicio viejo.
drop policy if exists services_read on public.services;
create policy services_read on public.services for select to anon, authenticated using (true);
