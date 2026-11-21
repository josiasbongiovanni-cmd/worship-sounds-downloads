-- Fase 2: reservas, horarios libres, cancelar y reprogramar
begin;

do $$
declare
  ana uuid; beto uuid; caro uuid; admin uuid;
  v_barber uuid; v_corte uuid; v_cyb uuid; v_barba uuid;
  tz text := 'America/Argentina/Buenos_Aires';
  v_tue date; v_mon date;
  v_labels text;
  v_appt public.appointments; v_appt2 public.appointments;
  v_msg text; failed boolean;
  v_q record;
  function_failed boolean;
begin
  ana   := tests.create_user('ana@test.com', 'Ana');
  beto  := tests.create_user('beto@test.com', 'Beto');
  caro  := tests.create_user('caro@test.com', 'Caro');
  admin := tests.create_user('admin@test.com', 'Admin', 'admin');
  select id into v_barber from public.barbers where display_name = 'Mathu';
  select id into v_corte from public.services where name = 'Corte';
  select id into v_cyb   from public.services where name = 'Corte y barba';
  select id into v_barba from public.services where name = 'Barba';

  -- Próximo martes (al menos 3 días adelante) y el lunes anterior
  v_tue := (now() at time zone tz)::date + 3;
  while extract(dow from v_tue) <> 2 loop v_tue := v_tue + 1; end loop;
  v_mon := v_tue - 1;

  perform tests.login(ana);

  -- ===== Horarios libres =====
  select string_agg(label, ',' order by starts_at) into v_labels
  from public.get_available_slots(v_corte, v_barber, v_tue);
  perform tests.ok(v_labels = '09:00,10:00,11:00,12:00,15:00,16:00,17:00,18:00,19:00',
    'martes: turnos cada 1 hora de 9 a 13 y de 15 a 20 (sin el descanso) -> ' || coalesce(v_labels, 'ninguno'));

  perform tests.ok((select count(*) from public.get_available_slots(v_corte, v_barber, v_mon)) = 0,
    'lunes (cerrado): no hay horarios');
  perform tests.ok((select count(*) from public.get_available_slots(v_barba, v_barber, v_tue)) = 0,
    'un servicio desactivado no se puede reservar');
  perform tests.ok((select count(*) from public.get_available_slots(v_corte, v_barber, (now() at time zone tz)::date + 40)) = 0,
    'no se puede reservar a más de 30 días');
  perform tests.ok((select count(*) from public.get_available_slots(v_corte, v_barber, (now() at time zone tz)::date - 1)) = 0,
    'no se puede reservar en el pasado');
  perform tests.ok(exists (select 1 from public.get_available_days(v_corte, v_barber) where day = v_tue and free_slots = 9),
    'el martes aparece en la lista de días con 9 horarios libres');
  perform tests.ok(not exists (select 1 from public.get_available_days(v_corte, v_barber) where day = v_mon),
    'el lunes NO aparece en la lista de días');

  -- ===== Reservar =====
  select * into v_q from public.get_booking_quote(v_corte);
  perform tests.ok(v_q.list_price = 12000 and v_q.discount = 0 and not v_q.is_free_reward, 'sin corte gratis: paga $12.000');

  v_appt := public.book_appointment(v_corte, v_barber, ((v_tue + time '10:00') at time zone tz));
  perform tests.ok(v_appt.status = 'booked' and v_appt.client_id = ana and v_appt.price_charged = 12000,
    'Ana reserva el martes 10:00');
  perform tests.ok(v_appt.ends_at - v_appt.starts_at = interval '1 hour', 'el turno dura lo que dura el servicio (1 hora)');
  perform tests.ok(v_appt.counts_for_loyalty, 'el turno de corte queda marcado como "suma para fidelidad"');

  perform tests.ok(not exists (select 1 from public.get_available_slots(v_corte, v_barber, v_tue) where label = '10:00'),
    'el horario de las 10:00 desaparece de los libres');
  perform tests.logout();

  perform tests.login(beto);
  failed := false;
  begin perform public.book_appointment(v_corte, v_barber, ((v_tue + time '10:00') at time zone tz));
  exception when others then failed := true; v_msg := sqlerrm; end;
  perform tests.ok(failed and v_msg like 'Ese horario ya no está disponible%', 'Beto NO puede reservar las 10:00 (ya está tomado)');

  failed := false;
  begin perform public.book_appointment(v_corte, v_barber, ((v_tue + time '10:30') at time zone tz));
  exception when others then failed := true; v_msg := sqlerrm; end;
  perform tests.ok(failed, 'no se puede reservar fuera de la grilla (10:30)');

  failed := false;
  begin perform public.book_appointment(v_corte, v_barber, ((v_tue + time '13:00') at time zone tz));
  exception when others then failed := true; end;
  perform tests.ok(failed, 'no se puede reservar en el descanso (13:00)');

  failed := false;
  begin perform public.book_appointment(v_corte, v_barber, ((v_mon + time '10:00') at time zone tz));
  exception when others then failed := true; end;
  perform tests.ok(failed, 'no se puede reservar un día cerrado');

  -- Ana no puede ver el turno de nadie más; Beto no ve el de Ana
  perform tests.ok((select count(*) from public.appointments) = 0, 'Beto no ve los turnos de Ana');
  perform tests.logout();

  -- ===== Bloqueo / feriado =====
  insert into public.time_off (barber_id, starts_at, ends_at, reason)
  values (null, (v_tue + time '15:00') at time zone tz, (v_tue + time '17:00') at time zone tz, 'trámite');
  perform tests.login(beto);
  select string_agg(label, ',' order by starts_at) into v_labels from public.get_available_slots(v_corte, v_barber, v_tue);
  perform tests.ok(v_labels = '09:00,11:00,12:00,17:00,18:00,19:00', 'un bloqueo de 15 a 17 saca esos horarios -> ' || v_labels);

  -- ===== Límite de turnos activos (2) =====
  perform public.book_appointment(v_corte, v_barber, ((v_tue + time '09:00') at time zone tz));
  perform public.book_appointment(v_corte, v_barber, ((v_tue + time '11:00') at time zone tz));
  failed := false;
  begin perform public.book_appointment(v_corte, v_barber, ((v_tue + time '12:00') at time zone tz));
  exception when others then failed := true; v_msg := sqlerrm; end;
  perform tests.ok(failed and v_msg like 'Ya tenés 2 turnos%', 'un cliente no puede tener más de 2 turnos futuros reservados');

  -- ===== Cancelar =====
  failed := false;
  begin perform public.cancel_appointment(v_appt.id);
  exception when others then failed := true; v_msg := sqlerrm; end;
  perform tests.ok(failed and v_msg = 'No encontramos ese turno.', 'Beto NO puede cancelar el turno de Ana');
  perform tests.logout();

  perform tests.login(ana);
  v_appt := public.cancel_appointment(v_appt.id, 'no llego');
  perform tests.ok(v_appt.status = 'cancelled' and v_appt.status_changed_by = ana, 'Ana cancela su turno');
  perform tests.ok(exists (select 1 from public.get_available_slots(v_corte, v_barber, v_tue) where label = '10:00'),
    'al cancelar, las 10:00 vuelven a estar libres');
  failed := false;
  begin perform public.cancel_appointment(v_appt.id);
  exception when others then failed := true; end;
  perform tests.ok(failed, 'no se puede cancelar dos veces');
  perform tests.logout();

  -- Turno que empieza en 1 hora (creado directo para la prueba)
  insert into public.appointments (client_id, barber_id, service_id, starts_at, ends_at, list_price, counts_for_loyalty)
  values (caro, v_barber, v_corte, date_trunc('hour', now()) + interval '1 hour', date_trunc('hour', now()) + interval '2 hours', 10000, true)
  returning * into v_appt2;
  perform tests.login(caro);
  failed := false;
  begin perform public.cancel_appointment(v_appt2.id);
  exception when others then failed := true; v_msg := sqlerrm; end;
  perform tests.ok(failed and v_msg like 'Los turnos se pueden cancelar hasta 2 horas antes%', 'el cliente NO puede cancelar con menos de 2 horas');
  failed := false;
  begin perform public.reschedule_appointment(v_appt2.id, (v_tue + time '12:00') at time zone tz);
  exception when others then failed := true; v_msg := sqlerrm; end;
  perform tests.ok(failed and v_msg like 'Los turnos se pueden reprogramar hasta 2 horas antes%', 'el cliente NO puede reprogramar con menos de 2 horas');
  perform tests.logout();

  perform tests.login(admin);
  v_appt2 := public.cancel_appointment(v_appt2.id, 'avisó por WhatsApp');
  perform tests.ok(v_appt2.status = 'cancelled', 'el admin SÍ puede cancelar aunque falte menos de 2 horas');
  perform tests.logout();

  -- ===== Reprogramar =====
  perform tests.login(caro);
  v_appt := public.book_appointment(v_cyb, v_barber, ((v_tue + time '12:00') at time zone tz));
  perform tests.ok(v_appt.price_charged = 15000, 'Caro reserva Corte y barba a $15.000');
  failed := false;
  begin perform public.reschedule_appointment(v_appt.id, (v_tue + time '09:00') at time zone tz);
  exception when others then failed := true; v_msg := sqlerrm; end;
  perform tests.ok(failed and v_msg like 'Ese horario ya no está disponible%', 'no puede reprogramar a un horario ocupado');
  v_appt := public.reschedule_appointment(v_appt.id, (v_tue + time '18:00') at time zone tz);
  perform tests.ok(to_char(v_appt.starts_at at time zone tz, 'HH24:MI') = '18:00' and v_appt.status = 'booked',
    'Caro reprograma a las 18:00');
  perform tests.ok(exists (select 1 from public.get_available_slots(v_corte, v_barber, v_tue) where label = '12:00'),
    'el horario viejo (12:00) queda libre');
  perform tests.logout();

  -- ===== Corte gratis al reservar =====
  update public.loyalty_cards set rewards_available = 1 where client_id = ana;  -- (simula que llegó a 9)
  perform tests.login(ana);
  select * into v_q from public.get_booking_quote(v_cyb);
  perform tests.ok(v_q.is_free_reward and v_q.discount = 12000 and v_q.list_price - v_q.discount = 3000,
    'con corte gratis, Corte y barba cuesta $3.000 (se descuenta el corte)');
  select * into v_q from public.get_booking_quote(v_corte);
  perform tests.ok(v_q.is_free_reward and v_q.list_price - v_q.discount = 0, 'con corte gratis, el Corte cuesta $0');

  v_appt := public.book_appointment(v_corte, v_barber, ((v_tue + time '19:00') at time zone tz));
  perform tests.ok(v_appt.is_free_reward and v_appt.price_charged = 0, 'la reserva aplica el gratis automáticamente ($0)');
  select * into v_q from public.get_booking_quote(v_corte);
  perform tests.ok(not v_q.is_free_reward, 'el mismo gratis NO se puede usar en una segunda reserva');
  perform tests.ok((select rewards_available from public.loyalty_cards where client_id = ana) = 1,
    'reservar NO consume el gratis (se consume recién al completar el corte)');

  perform public.cancel_appointment(v_appt.id);
  select * into v_q from public.get_booking_quote(v_corte);
  perform tests.ok(v_q.is_free_reward, 'si cancela el turno gratis, el gratis vuelve a estar disponible');
  perform tests.logout();

  -- ===== Funciones internas protegidas =====
  perform tests.login(ana);
  failed := false;
  begin perform * from public._available_slots(v_corte, v_barber, v_tue, null);
  exception when insufficient_privilege then failed := true; end;
  perform tests.ok(failed, 'la app NO puede llamar funciones internas');
  failed := false;
  begin perform * from public._quote(beto, v_corte, null);
  exception when insufficient_privilege then failed := true; end;
  perform tests.ok(failed, 'un cliente NO puede consultar el precio/premio de otro');
  perform tests.logout();

  -- Sin sesión no se puede reservar
  execute 'set local role anon';
  failed := false;
  begin perform public.book_appointment(v_corte, v_barber, ((v_tue + time '17:00') at time zone tz));
  exception when insufficient_privilege then failed := true; end;
  perform tests.ok(failed, 'sin iniciar sesión no se puede reservar');
  execute 'reset role';
end $$;

rollback;
