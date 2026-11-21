-- Fase 3: panel del administrador
begin;

do $$
declare
  ana uuid; admin uuid; v_barber uuid; v_corte uuid; v_new uuid;
  tz text := 'America/Argentina/Buenos_Aires';
  v_tue date; v_appt public.appointments; v_past public.appointments;
  v_labels text; v_msg text; failed boolean; v_n int;
begin
  ana   := tests.create_user('ana@test.com', 'Ana');
  admin := tests.create_user('admin@test.com', 'Admin', 'admin');
  select id into v_barber from public.barbers where display_name = 'Mathu';
  select id into v_corte from public.services where name = 'Corte';
  v_tue := (now() at time zone tz)::date + 3;
  while extract(dow from v_tue) <> 2 loop v_tue := v_tue + 1; end loop;

  -- ===== El cliente NO puede tocar nada del panel =====
  perform tests.login(ana);
  v_appt := public.book_appointment(v_corte, v_barber, (v_tue + time '10:00') at time zone tz);

  failed := false;
  begin perform public.set_appointment_status(v_appt.id, 'completed');
  exception when others then failed := true; v_msg := sqlerrm; end;
  perform tests.ok(failed and v_msg like 'Solo el administrador%', 'el cliente NO puede marcar su turno como completado');

  failed := false;
  begin perform public.set_working_hours(v_barber, '[]'::jsonb);
  exception when others then failed := true; end;
  perform tests.ok(failed, 'el cliente NO puede cambiar los horarios');

  update public.services set duration_min = 5, price = 1 where id = v_corte;
  perform tests.ok((select price from public.services where id = v_corte) = 12000, 'el cliente NO puede cambiar precio ni duración');

  failed := false;
  begin insert into public.time_off (starts_at, ends_at) values (now(), now() + interval '1 day');
  exception when others then failed := true; end;
  perform tests.ok(failed, 'el cliente NO puede crear bloqueos');

  update public.settings set cancel_limit_hours = 0;
  perform tests.ok((select cancel_limit_hours from public.settings) = 2, 'el cliente NO puede cambiar las reglas');
  perform tests.ok((select count(*) from public.appointments_outside_hours(v_barber)) = 0, 'el cliente NO ve el control de turnos del admin');
  perform tests.logout();

  -- ===== Admin: servicios =====
  perform tests.login(admin);
  update public.services set price = 13000, duration_min = 45 where id = v_corte;
  perform tests.ok((select price from public.services where id = v_corte) = 13000, 'el admin cambia el precio del Corte');
  insert into public.services (name, price, duration_min, counts_for_loyalty) values ('Corte niño', 9000, 60, true) returning id into v_new;
  perform tests.ok(exists (select 1 from public.barber_services where service_id = v_new and barber_id = v_barber),
    'un servicio nuevo queda asignado automáticamente al barbero');
  perform tests.ok((select count(*) from public.get_available_slots(v_new, v_barber, v_tue)) > 0, 'el servicio nuevo ya se puede reservar');
  update public.services set price = 12000, duration_min = 60 where id = v_corte;

  -- ===== Admin: estados de turno =====
  failed := false;
  begin perform public.set_appointment_status(v_appt.id, 'completed');
  exception when others then failed := true; v_msg := sqlerrm; end;
  perform tests.ok(failed and v_msg like 'Todavía no llegó%', 'no se puede completar un turno de un día futuro');

  perform tests.logout();  -- (preparación de la prueba: turno de hace 2 horas)
  insert into public.appointments (client_id, barber_id, service_id, starts_at, ends_at, list_price, counts_for_loyalty)
  values (ana, v_barber, v_corte, date_trunc('hour', now()) - interval '2 hours', date_trunc('hour', now()) - interval '1 hour', 12000, true)
  returning * into v_past;
  perform tests.login(admin);

  v_past := public.set_appointment_status(v_past.id, 'no_show');
  perform tests.ok(v_past.status = 'no_show' and v_past.status_changed_by = admin, 'el admin marca "no se presentó" (queda registrado quién)');
  v_past := public.set_appointment_status(v_past.id, 'completed');
  perform tests.ok(v_past.status = 'completed', 'el admin corrige "no se presentó" -> completado');
  failed := false;
  begin perform public.set_appointment_status(v_past.id, 'booked');
  exception when others then failed := true; v_msg := sqlerrm; end;
  perform tests.ok(failed and v_msg like 'Este turno ya está completado%', 'un completado no se puede volver atrás desde la agenda');

  v_appt := public.set_appointment_status(v_appt.id, 'cancelled', 'Cerramos por lluvia');
  perform tests.ok(v_appt.status = 'cancelled' and v_appt.cancel_reason = 'Cerramos por lluvia', 'el admin cancela con motivo');
  v_appt := public.set_appointment_status(v_appt.id, 'booked');
  perform tests.ok(v_appt.status = 'booked', 'el admin puede reactivar un turno cancelado si el horario sigue libre');

  -- ===== Admin: horarios =====
  failed := false;
  begin perform public.set_working_hours(v_barber, '[{"weekday":2,"start":"13:00","end":"09:00"}]');
  exception when others then failed := true; v_msg := sqlerrm; end;
  perform tests.ok(failed and v_msg like 'En cada franja%', 'no acepta una franja que cierra antes de abrir');

  failed := false;
  begin perform public.set_working_hours(v_barber, '[{"weekday":2,"start":"09:00","end":"13:00"},{"weekday":2,"start":"12:00","end":"18:00"}]');
  exception when others then failed := true; v_msg := sqlerrm; end;
  perform tests.ok(failed and v_msg like 'Hay franjas que se pisan%', 'no acepta franjas superpuestas el mismo día');

  -- Nuevo horario: martes 10 a 14 corrido; lunes 16 a 20
  perform public.set_working_hours(v_barber,
    '[{"weekday":2,"start":"10:00","end":"14:00"},{"weekday":1,"start":"16:00","end":"20:00"}]');
  perform tests.ok((select count(*) from public.working_hours where barber_id = v_barber) = 2, 'el admin guarda el horario nuevo (reemplaza el viejo)');
  select string_agg(label, ',' order by starts_at) into v_labels from public.get_available_slots(v_corte, v_barber, v_tue);
  perform tests.ok(v_labels = '11:00,12:00,13:00', 'el horario nuevo se refleja en los turnos libres (10:00 ocupado) -> ' || coalesce(v_labels, '-'));

  -- El turno de las 10 sigue dentro del horario; si cierro el martes, queda afuera
  perform tests.ok((select count(*) from public.appointments_outside_hours(v_barber)) = 0, 'turno de las 10 sigue dentro del horario');
  perform public.set_working_hours(v_barber, '[{"weekday":1,"start":"16:00","end":"20:00"}]');
  perform tests.ok((select count(*) from public.appointments_outside_hours(v_barber)) = 1,
    'si cierro el martes, el panel avisa que hay 1 turno reservado fuera de horario');

  -- ===== Admin: bloqueos y reglas =====
  perform public.set_working_hours(v_barber, '[{"weekday":2,"start":"09:00","end":"20:00"}]');
  insert into public.time_off (barber_id, starts_at, ends_at, reason)
  values (null, (v_tue + time '00:00') at time zone tz, (v_tue + 1 + time '00:00') at time zone tz, 'Feriado');
  perform tests.ok((select count(*) from public.get_available_slots(v_corte, v_barber, v_tue)) = 0, 'un feriado de día completo deja el día sin turnos');
  perform tests.ok((select count(*) from public.appointments_outside_hours(v_barber)) = 1, 'el panel avisa del turno reservado en el feriado');

  update public.settings set cancel_limit_hours = 4, max_active_bookings = 3;
  perform tests.ok((select cancel_limit_hours from public.settings) = 4, 'el admin cambia las reglas (cancelar hasta 4 h antes)');
  perform tests.logout();
end $$;

rollback;
