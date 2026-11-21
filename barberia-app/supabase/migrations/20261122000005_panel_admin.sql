-- =====================================================================
-- Mathu Barber - FASE 3: panel del administrador
-- Solo admin/barbero puede usar estas funciones. El cliente no.
-- =====================================================================

-- Un servicio nuevo queda disponible automáticamente para todos los barberos activos
create or replace function public.link_new_service()
returns trigger language plpgsql security definer set search_path = '' as $$
begin
  insert into public.barber_services (barber_id, service_id)
  select b.id, new.id from public.barbers b where b.active
  on conflict do nothing;
  return new;
end $$;

drop trigger if exists services_link_barbers on public.services;
create trigger services_link_barbers after insert on public.services
  for each row execute function public.link_new_service();

-- ---------------------------------------------------------------------
-- Cambiar el estado de un turno (agenda del admin)
--   reservado -> completado / no se presentó / cancelado
--   no se presentó -> reservado / completado   (por si se marcó por error)
--   cancelado -> reservado                      (si el horario sigue libre)
-- Deshacer un "completado" llega en la Fase 4 (porque afecta la tarjeta).
-- ---------------------------------------------------------------------
create or replace function public.set_appointment_status(p_id uuid, p_status public.appointment_status, p_note text default null)
returns public.appointments
language plpgsql security definer set search_path = '' as $$
declare
  a public.appointments;
  v_uid uuid := auth.uid();
  v_ok boolean;
begin
  if not public.is_staff() then perform public._raise('Solo el administrador puede cambiar el estado de un turno.'); end if;

  select * into a from public.appointments where id = p_id for update;
  if not found then perform public._raise('No encontramos ese turno.'); end if;
  if a.status = p_status then return a; end if;

  v_ok := case a.status
    when 'booked'    then p_status in ('completed', 'no_show', 'cancelled')
    when 'no_show'   then p_status in ('booked', 'completed')
    when 'cancelled' then p_status = 'booked'
    else false
  end;
  if not v_ok then
    if a.status = 'completed' then
      perform public._raise('Este turno ya está completado. Para corregirlo, usá el ajuste de la tarjeta del cliente.');
    end if;
    perform public._raise('Ese cambio de estado no está permitido.');
  end if;

  if p_status in ('completed', 'no_show') and a.starts_at > now() + interval '12 hours' then
    perform public._raise('Todavía no llegó el día de este turno.');
  end if;

  begin
    update public.appointments
    set status = p_status,
        status_changed_at = now(),
        status_changed_by = v_uid,
        cancel_reason = case when p_status = 'cancelled' then coalesce(nullif(trim(p_note), ''), 'Cancelado por la barbería') else cancel_reason end
    where id = p_id
    returning * into a;
  exception when exclusion_violation then
    perform public._raise('No se puede reactivar: ese horario ya lo tomó otro cliente.');
  end;

  return a;
end $$;

-- ---------------------------------------------------------------------
-- Guardar el horario semanal de un barbero de una sola vez.
-- p_hours = [{"weekday":2,"start":"09:00","end":"13:00"}, ...]
-- ---------------------------------------------------------------------
create or replace function public.set_working_hours(p_barber uuid, p_hours jsonb)
returns setof public.working_hours
language plpgsql security definer set search_path = '' as $$
declare
  v_bad int;
  v_rows public.working_hours[];
begin
  if not public.is_admin() then perform public._raise('Solo el administrador puede cambiar los horarios.'); end if;
  if not exists (select 1 from public.barbers where id = p_barber) then perform public._raise('No encontramos ese barbero.'); end if;

  -- Se lee la lista una sola vez a un arreglo (sin tablas temporales)
  select coalesce(array_agg(row(null, p_barber, (h ->> 'weekday')::smallint, (h ->> 'start')::time, (h ->> 'end')::time)::public.working_hours), '{}')
  into v_rows
  from jsonb_array_elements(coalesce(p_hours, '[]'::jsonb)) h;

  if exists (select 1 from unnest(v_rows) r where r.weekday is null or r.weekday not between 0 and 6 or r.start_time is null or r.end_time is null) then
    perform public._raise('Hay un horario incompleto.');
  end if;
  if exists (select 1 from unnest(v_rows) r where r.end_time <= r.start_time) then
    perform public._raise('En cada franja, la hora de cierre tiene que ser después de la de apertura.');
  end if;
  select count(*) into v_bad from unnest(v_rows) x, unnest(v_rows) y
  where x.weekday = y.weekday and (x.start_time, x.end_time) <> (y.start_time, y.end_time)
    and x.start_time < y.end_time and y.start_time < x.end_time;
  if v_bad > 0 then perform public._raise('Hay franjas que se pisan en el mismo día.'); end if;

  delete from public.working_hours where barber_id = p_barber;
  insert into public.working_hours (barber_id, weekday, start_time, end_time)
  select distinct p_barber, r.weekday, r.start_time, r.end_time from unnest(v_rows) r;

  return query select * from public.working_hours where barber_id = p_barber order by weekday, start_time;
end $$;

-- Turnos reservados que quedarían fuera del horario o de un bloqueo (para avisar al admin)
create or replace function public.appointments_outside_hours(p_barber uuid)
returns setof public.appointments
language sql stable security definer set search_path = '' as $$
  select a.* from public.appointments a, public.settings s
  where public.is_staff()
    and a.barber_id = p_barber and a.status = 'booked' and a.starts_at > now()
    and (
      not exists (
        select 1 from public.working_hours wh
        where wh.barber_id = a.barber_id
          and wh.weekday = extract(dow from (a.starts_at at time zone s.timezone))::int
          and wh.start_time <= (a.starts_at at time zone s.timezone)::time
          and wh.end_time   >= (a.ends_at   at time zone s.timezone)::time
      )
      or exists (
        select 1 from public.time_off t
        where (t.barber_id is null or t.barber_id = a.barber_id)
          and tstzrange(t.starts_at, t.ends_at) && tstzrange(a.starts_at, a.ends_at)
      )
    )
  order by a.starts_at;
$$;

revoke execute on function public.set_appointment_status(uuid, public.appointment_status, text) from public, anon;
revoke execute on function public.set_working_hours(uuid, jsonb)                                from public, anon;
revoke execute on function public.appointments_outside_hours(uuid)                              from public, anon;
grant  execute on function public.set_appointment_status(uuid, public.appointment_status, text) to authenticated;
grant  execute on function public.set_working_hours(uuid, jsonb)                                to authenticated;
grant  execute on function public.appointments_outside_hours(uuid)                              to authenticated;
revoke execute on function public.link_new_service() from public, anon, authenticated;
