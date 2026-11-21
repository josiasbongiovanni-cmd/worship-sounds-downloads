-- =====================================================================
-- Mathu Barber - FASE 2: reservas
-- Todas las reglas se validan en el servidor. La app solo muestra y pide.
-- =====================================================================

-- ---------------------------------------------------------------------
-- (interna) Horarios libres de un barbero para un servicio en un día.
-- Tiene en cuenta: horario de atención, descansos, duración del servicio,
-- bloqueos/feriados, turnos ya tomados, anticipación mínima y ventana máxima.
-- p_ignore: un turno a ignorar (sirve para reprogramar ese mismo turno).
-- ---------------------------------------------------------------------
create or replace function public._available_slots(p_service uuid, p_barber uuid, p_date date, p_ignore uuid default null)
returns table (starts_at timestamptz, ends_at timestamptz, label text)
language plpgsql stable security definer set search_path = '' as $$
declare
  s      public.settings;
  v_dur  int;
  v_today date;
begin
  select * into s from public.settings limit 1;
  select sv.duration_min into v_dur from public.services sv where sv.id = p_service and sv.active;
  if v_dur is null then return; end if;

  if not exists (
    select 1 from public.barbers b
    join public.barber_services bs on bs.barber_id = b.id
    where b.id = p_barber and b.active and bs.service_id = p_service
  ) then return; end if;

  v_today := (now() at time zone s.timezone)::date;
  if p_date < v_today or p_date > v_today + s.booking_window_days then return; end if;

  return query
  with franjas as (
    select (extract(epoch from wh.start_time) / 60)::int as ini,
           (extract(epoch from wh.end_time) / 60)::int   as fin
    from public.working_hours wh
    where wh.barber_id = p_barber and wh.weekday = extract(dow from p_date)::int
  ),
  grilla as (
    select distinct ((p_date::timestamp + make_interval(mins => f.ini + g * s.slot_interval_min)) at time zone s.timezone) as st
    from franjas f,
         generate_series(0, (f.fin - f.ini) / s.slot_interval_min) as g
    where f.ini + g * s.slot_interval_min + v_dur <= f.fin   -- el servicio entra completo en la franja
  )
  select gr.st, gr.st + make_interval(mins => v_dur), to_char(gr.st at time zone s.timezone, 'HH24:MI')
  from grilla gr
  where gr.st >= now() + make_interval(mins => s.min_notice_min)
    and not exists (
      select 1 from public.time_off t
      where (t.barber_id is null or t.barber_id = p_barber)
        and tstzrange(t.starts_at, t.ends_at) && tstzrange(gr.st, gr.st + make_interval(mins => v_dur))
    )
    and not exists (
      select 1 from public.appointments a
      where a.barber_id = p_barber
        and a.status in ('booked', 'completed')
        and a.id is distinct from p_ignore
        and tstzrange(a.starts_at, a.ends_at) && tstzrange(gr.st, gr.st + make_interval(mins => v_dur))
    )
  order by 1;
end $$;

-- ---------------------------------------------------------------------
-- (interna) Precio para un cliente: aplica el corte gratis si lo tiene.
-- Regla: si el servicio suma para fidelidad y el cliente tiene un gratis
-- que todavía no reservó, se descuenta el precio del "Corte" (servicio base).
-- ---------------------------------------------------------------------
create or replace function public._quote(p_client uuid, p_service uuid, p_ignore uuid default null)
returns table (list_price numeric, discount numeric, is_free_reward boolean)
language plpgsql stable security definer set search_path = '' as $$
declare
  v_price numeric; v_counts boolean; v_base numeric; v_avail int; v_pending int;
begin
  select sv.price, sv.counts_for_loyalty into v_price, v_counts from public.services sv where sv.id = p_service;
  if v_price is null then return; end if;

  select coalesce(c.rewards_available, 0) into v_avail from public.loyalty_cards c where c.client_id = p_client;
  select count(*) into v_pending from public.appointments a
  where a.client_id = p_client and a.status = 'booked' and a.is_free_reward and a.id is distinct from p_ignore;

  if v_counts and coalesce(v_avail, 0) > v_pending then
    select sv.price into v_base from public.services sv where sv.is_reward_base;
    return query select v_price, least(v_price, coalesce(v_base, v_price)), true;
  else
    return query select v_price, 0::numeric, false;
  end if;
end $$;

create or replace function public._raise(p_msg text) returns void language plpgsql as $$
begin
  raise exception using message = p_msg, errcode = 'P0001';
end $$;

-- =====================================================================
-- Funciones que usa la app
-- =====================================================================

-- Horarios libres de un día
create or replace function public.get_available_slots(p_service uuid, p_barber uuid, p_date date)
returns table (starts_at timestamptz, ends_at timestamptz, label text)
language sql stable security definer set search_path = '' as $$
  select * from public._available_slots(p_service, p_barber, p_date, null);
$$;

-- Días que tienen al menos un horario libre (para no mostrar días vacíos)
create or replace function public.get_available_days(p_service uuid, p_barber uuid)
returns table (day date, free_slots int)
language sql stable security definer set search_path = '' as $$
  select d::date, x.n
  from public.settings s,
       generate_series((now() at time zone s.timezone)::date,
                       (now() at time zone s.timezone)::date + s.booking_window_days, interval '1 day') as d,
       lateral (select count(*)::int as n from public._available_slots(p_service, p_barber, d::date, null)) x
  where x.n > 0
  order by 1;
$$;

-- Precio que pagaría el cliente (muestra si se aplica el corte gratis)
create or replace function public.get_booking_quote(p_service uuid)
returns table (list_price numeric, discount numeric, is_free_reward boolean)
language sql stable security definer set search_path = '' as $$
  select * from public._quote(auth.uid(), p_service, null);
$$;

-- Reservar un turno
create or replace function public.book_appointment(p_service uuid, p_barber uuid, p_starts_at timestamptz, p_notes text default null)
returns public.appointments
language plpgsql security definer set search_path = '' as $$
declare
  v_uid   uuid := auth.uid();
  s       public.settings;
  v_slot  record;
  v_quote record;
  v_count int;
  v_row   public.appointments;
begin
  if v_uid is null then perform public._raise('Tenés que iniciar sesión para reservar.'); end if;
  select * into s from public.settings limit 1;

  -- Bloquea la tarjeta del cliente: dos reservas simultáneas del mismo cliente
  -- se hacen de a una (así nunca usa el corte gratis dos veces).
  perform 1 from public.loyalty_cards where client_id = v_uid for update;

  select count(*) into v_count from public.appointments
  where client_id = v_uid and status = 'booked' and starts_at > now();
  if v_count >= s.max_active_bookings then
    perform public._raise(format('Ya tenés %s turnos reservados. Podés reservar otro cuando pase alguno, o cancelar uno.', v_count));
  end if;

  select * into v_slot
  from public._available_slots(p_service, p_barber, (p_starts_at at time zone s.timezone)::date, null) sl
  where sl.starts_at = p_starts_at;
  if not found then
    perform public._raise('Ese horario ya no está disponible. Elegí otro, por favor.');
  end if;

  select * into v_quote from public._quote(v_uid, p_service, null);

  begin
    insert into public.appointments (client_id, barber_id, service_id, starts_at, ends_at,
                                     list_price, discount, is_free_reward, counts_for_loyalty, notes)
    select v_uid, p_barber, p_service, v_slot.starts_at, v_slot.ends_at,
           v_quote.list_price, v_quote.discount, v_quote.is_free_reward, sv.counts_for_loyalty,
           nullif(trim(p_notes), '')
    from public.services sv where sv.id = p_service
    returning * into v_row;
  exception when exclusion_violation then
    -- Otra persona lo reservó en el mismo instante
    perform public._raise('Ese horario ya no está disponible. Elegí otro, por favor.');
  end;

  return v_row;
end $$;

-- Cancelar un turno (cliente: hasta X horas antes; staff: siempre)
create or replace function public.cancel_appointment(p_id uuid, p_reason text default null)
returns public.appointments
language plpgsql security definer set search_path = '' as $$
declare
  v_uid uuid := auth.uid();
  s     public.settings;
  a     public.appointments;
  v_staff boolean := public.is_staff();
begin
  select * into s from public.settings limit 1;
  select * into a from public.appointments where id = p_id for update;
  if not found or (a.client_id is distinct from v_uid and not v_staff) then
    perform public._raise('No encontramos ese turno.');
  end if;
  if a.status <> 'booked' then perform public._raise('Este turno ya no se puede cancelar.'); end if;
  if not v_staff and a.starts_at - now() < make_interval(hours => s.cancel_limit_hours) then
    perform public._raise(format('Los turnos se pueden cancelar hasta %s horas antes. Escribinos por WhatsApp.', s.cancel_limit_hours));
  end if;

  update public.appointments
  set status = 'cancelled', cancel_reason = nullif(trim(p_reason), ''),
      status_changed_at = now(), status_changed_by = v_uid
  where id = p_id
  returning * into a;
  return a;
end $$;

-- Reprogramar un turno (mismo servicio y barbero, otro horario)
create or replace function public.reschedule_appointment(p_id uuid, p_new_starts_at timestamptz)
returns public.appointments
language plpgsql security definer set search_path = '' as $$
declare
  v_uid  uuid := auth.uid();
  s      public.settings;
  a      public.appointments;
  v_slot record;
  v_staff boolean := public.is_staff();
begin
  select * into s from public.settings limit 1;
  select * into a from public.appointments where id = p_id for update;
  if not found or (a.client_id is distinct from v_uid and not v_staff) then
    perform public._raise('No encontramos ese turno.');
  end if;
  if a.status <> 'booked' then perform public._raise('Este turno ya no se puede reprogramar.'); end if;
  if not v_staff and a.starts_at - now() < make_interval(hours => s.cancel_limit_hours) then
    perform public._raise(format('Los turnos se pueden reprogramar hasta %s horas antes. Escribinos por WhatsApp.', s.cancel_limit_hours));
  end if;

  select * into v_slot
  from public._available_slots(a.service_id, a.barber_id, (p_new_starts_at at time zone s.timezone)::date, a.id) sl
  where sl.starts_at = p_new_starts_at;
  if not found then
    perform public._raise('Ese horario ya no está disponible. Elegí otro, por favor.');
  end if;

  begin
    update public.appointments
    set starts_at = v_slot.starts_at, ends_at = v_slot.ends_at
    where id = p_id
    returning * into a;
  exception when exclusion_violation then
    perform public._raise('Ese horario ya no está disponible. Elegí otro, por favor.');
  end;
  return a;
end $$;

-- ---------------------------------------------------------------------
-- Permisos: las funciones internas (_xxx) NO se pueden llamar desde la app.
-- ---------------------------------------------------------------------
revoke execute on function public._available_slots(uuid, uuid, date, uuid) from public, anon, authenticated;
revoke execute on function public._quote(uuid, uuid, uuid)                 from public, anon, authenticated;
revoke execute on function public._raise(text)                             from public, anon, authenticated;

revoke execute on function public.get_available_slots(uuid, uuid, date)                 from public, anon;
revoke execute on function public.get_available_days(uuid, uuid)                        from public, anon;
revoke execute on function public.get_booking_quote(uuid)                               from public, anon;
revoke execute on function public.book_appointment(uuid, uuid, timestamptz, text)       from public, anon;
revoke execute on function public.cancel_appointment(uuid, text)                        from public, anon;
revoke execute on function public.reschedule_appointment(uuid, timestamptz)             from public, anon;

grant execute on function public.get_available_slots(uuid, uuid, date)           to authenticated;
grant execute on function public.get_available_days(uuid, uuid)                  to authenticated;
grant execute on function public.get_booking_quote(uuid)                         to authenticated;
grant execute on function public.book_appointment(uuid, uuid, timestamptz, text) to authenticated;
grant execute on function public.cancel_appointment(uuid, text)                  to authenticated;
grant execute on function public.reschedule_appointment(uuid, timestamptz)       to authenticated;
