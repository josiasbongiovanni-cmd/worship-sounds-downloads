-- =====================================================================
-- Mathu Barber - datos iniciales de ejemplo
-- Precios y horarios son PROVISORIOS: se cambian desde el panel admin (Fase 3)
-- o desde el "Table Editor" de Supabase.
-- =====================================================================
do $$
declare
  v_barber uuid;
begin
  insert into public.barbers (display_name, sort_order) values ('Mathu', 1)
  returning id into v_barber;

  insert into public.services (name, description, price, duration_min, counts_for_loyalty, is_reward_base, sort_order) values
    ('Corte',          'Corte de pelo clásico o moderno', 8000,  30, true,  true,  1),
    ('Corte + barba',  'Corte completo y perfilado de barba', 11000, 45, true,  false, 2),
    ('Barba',          'Perfilado y arreglo de barba',     5000,  20, false, false, 3);

  -- El barbero hace todos los servicios
  insert into public.barber_services (barber_id, service_id)
  select v_barber, id from public.services;

  -- Martes a sábado: 09:00-13:00 y 15:00-20:00 (descanso 13 a 15)
  insert into public.working_hours (barber_id, weekday, start_time, end_time)
  select v_barber, d, t.s, t.e
  from generate_series(2, 6) as d,
       (values (time '09:00', time '13:00'), (time '15:00', time '20:00')) as t(s, e);
end $$;
