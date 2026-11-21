-- Fase 1: la base de datos nunca permite turnos superpuestos para el mismo barbero
begin;

do $$
declare
  ana uuid; beto uuid; v_barber uuid; v_barber2 uuid; v_service uuid;
  t0 timestamptz := date_trunc('day', now()) + interval '3 days 12 hours';
  failed boolean;
begin
  ana  := tests.create_user('ana@test.com', 'Ana');
  beto := tests.create_user('beto@test.com', 'Beto');
  select id into v_barber from public.barbers where display_name = 'Mathu';
  select id into v_service from public.services where name = 'Corte';
  insert into public.barbers (display_name) values ('Segundo barbero') returning id into v_barber2;

  insert into public.appointments (client_id, barber_id, service_id, starts_at, ends_at, list_price, counts_for_loyalty)
  values (ana, v_barber, v_service, t0, t0 + interval '30 min', 8000, true);

  -- Mismo horario exacto
  failed := false;
  begin
    insert into public.appointments (client_id, barber_id, service_id, starts_at, ends_at, list_price, counts_for_loyalty)
    values (beto, v_barber, v_service, t0, t0 + interval '30 min', 8000, true);
  exception when exclusion_violation then failed := true; end;
  perform tests.ok(failed, 'no se puede reservar el mismo horario dos veces');

  -- Se pisa parcialmente (empieza 15 min después)
  failed := false;
  begin
    insert into public.appointments (client_id, barber_id, service_id, starts_at, ends_at, list_price, counts_for_loyalty)
    values (beto, v_barber, v_service, t0 + interval '15 min', t0 + interval '45 min', 8000, true);
  exception when exclusion_violation then failed := true; end;
  perform tests.ok(failed, 'no se puede reservar un turno que se pisa parcialmente');

  -- Uno largo que "envuelve" al existente
  failed := false;
  begin
    insert into public.appointments (client_id, barber_id, service_id, starts_at, ends_at, list_price, counts_for_loyalty)
    values (beto, v_barber, v_service, t0 - interval '10 min', t0 + interval '1 hour', 8000, true);
  exception when exclusion_violation then failed := true; end;
  perform tests.ok(failed, 'no se puede reservar un turno que envuelve a otro');

  -- Pegado justo después: SÍ se puede
  insert into public.appointments (client_id, barber_id, service_id, starts_at, ends_at, list_price, counts_for_loyalty)
  values (beto, v_barber, v_service, t0 + interval '30 min', t0 + interval '60 min', 8000, true);
  perform tests.ok(true, 'un turno que empieza justo cuando termina el anterior SÍ se puede');

  -- Otro barbero, mismo horario: SÍ se puede
  insert into public.appointments (client_id, barber_id, service_id, starts_at, ends_at, list_price, counts_for_loyalty)
  values (beto, v_barber2, v_service, t0, t0 + interval '30 min', 8000, true);
  perform tests.ok(true, 'otro barbero puede atender en el mismo horario');

  -- Un turno cancelado libera el horario
  update public.appointments set status = 'cancelled' where client_id = ana;
  insert into public.appointments (client_id, barber_id, service_id, starts_at, ends_at, list_price, counts_for_loyalty)
  values (beto, v_barber, v_service, t0, t0 + interval '30 min', 8000, true);
  perform tests.ok(true, 'un turno cancelado libera el horario para otro cliente');
end $$;

rollback;
