-- =====================================================================
-- Mathu Barber - FASE 1: estructura base de la base de datos
-- =====================================================================
-- Este archivo crea todas las tablas, la seguridad (RLS) y las reglas
-- básicas. Las funciones de reservar (Fase 2), panel admin (Fase 3)
-- y fidelidad (Fase 4) se agregan en archivos siguientes.
-- =====================================================================

create schema if not exists extensions;
create extension if not exists btree_gist with schema extensions;

-- ---------------------------------------------------------------------
-- Tipos
-- ---------------------------------------------------------------------
create type public.user_role as enum ('client', 'barber', 'admin');
create type public.appointment_status as enum ('booked', 'completed', 'no_show', 'cancelled');
create type public.loyalty_event_type as enum ('earn', 'redeem', 'manual_adjust', 'reversal');

-- Actualiza la columna updated_at automáticamente
create or replace function public.touch_updated_at()
returns trigger language plpgsql set search_path = '' as $$
begin
  new.updated_at := now();
  return new;
end $$;

-- ---------------------------------------------------------------------
-- Perfiles (1 por usuario registrado)
-- ---------------------------------------------------------------------
create table public.profiles (
  id          uuid primary key references auth.users (id) on delete cascade,
  full_name   text not null default '',
  phone       text,                       -- WhatsApp
  email       text,
  role        public.user_role not null default 'client',
  push_token  text,                       -- token de Expo Notifications
  created_at  timestamptz not null default now(),
  updated_at  timestamptz not null default now()
);
create trigger profiles_touch before update on public.profiles
  for each row execute function public.touch_updated_at();

-- Funciones de ayuda para saber el rol de quien está usando la app.
-- security definer = se ejecutan con permisos del servidor (evita bucles de RLS).
create or replace function public.is_admin()
returns boolean language sql stable security definer set search_path = '' as $$
  select exists (select 1 from public.profiles where id = auth.uid() and role = 'admin');
$$;

create or replace function public.is_staff()
returns boolean language sql stable security definer set search_path = '' as $$
  select exists (select 1 from public.profiles where id = auth.uid() and role in ('admin', 'barber'));
$$;

-- ---------------------------------------------------------------------
-- Barberos (hoy 1, preparado para varios)
-- ---------------------------------------------------------------------
create table public.barbers (
  id            uuid primary key default gen_random_uuid(),
  profile_id    uuid unique references public.profiles (id) on delete set null,
  display_name  text not null,
  photo_url     text,
  active        boolean not null default true,
  sort_order    int not null default 0,
  created_at    timestamptz not null default now()
);

-- ---------------------------------------------------------------------
-- Servicios
-- ---------------------------------------------------------------------
create table public.services (
  id                  uuid primary key default gen_random_uuid(),
  name                text not null,
  description         text,
  price               numeric(10, 2) not null check (price >= 0),
  duration_min        int not null check (duration_min between 5 and 480),
  counts_for_loyalty  boolean not null default false,  -- ¿suma para la tarjeta?
  is_reward_base      boolean not null default false,  -- el "Corte" cuyo precio se descuenta en el corte gratis
  active              boolean not null default true,
  sort_order          int not null default 0,
  created_at          timestamptz not null default now(),
  updated_at          timestamptz not null default now()
);
-- Solo puede haber UN servicio de referencia para el corte gratis
create unique index services_one_reward_base on public.services (is_reward_base) where is_reward_base;
create trigger services_touch before update on public.services
  for each row execute function public.touch_updated_at();

-- Qué servicios hace cada barbero (para cuando haya varios)
create table public.barber_services (
  barber_id   uuid not null references public.barbers (id) on delete cascade,
  service_id  uuid not null references public.services (id) on delete cascade,
  primary key (barber_id, service_id)
);

-- ---------------------------------------------------------------------
-- Horarios de atención: varias franjas por día = descansos.
-- Ej: lunes 09:00-13:00 y 15:00-20:00 (descanso de 13 a 15)
-- weekday: 0 = domingo, 1 = lunes, ... 6 = sábado
-- ---------------------------------------------------------------------
create table public.working_hours (
  id          uuid primary key default gen_random_uuid(),
  barber_id   uuid not null references public.barbers (id) on delete cascade,
  weekday     smallint not null check (weekday between 0 and 6),
  start_time  time not null,
  end_time    time not null,
  check (end_time > start_time)
);
create index working_hours_barber_day on public.working_hours (barber_id, weekday);

-- ---------------------------------------------------------------------
-- Bloqueos: feriados, vacaciones, días cerrados, trámites, etc.
-- barber_id NULL = bloquea a toda la barbería
-- ---------------------------------------------------------------------
create table public.time_off (
  id          uuid primary key default gen_random_uuid(),
  barber_id   uuid references public.barbers (id) on delete cascade,
  starts_at   timestamptz not null,
  ends_at     timestamptz not null,
  reason      text,
  created_by  uuid references public.profiles (id) on delete set null,
  created_at  timestamptz not null default now(),
  check (ends_at > starts_at)
);
create index time_off_range on public.time_off using gist (tstzrange(starts_at, ends_at));

-- ---------------------------------------------------------------------
-- Configuración general (una sola fila)
-- ---------------------------------------------------------------------
create table public.settings (
  id                     boolean primary key default true check (id),
  business_name          text not null default 'Mathu Barber',
  timezone               text not null default 'America/Argentina/Buenos_Aires',
  slot_interval_min      int  not null default 15 check (slot_interval_min between 5 and 120),
  cancel_limit_hours     int  not null default 2  check (cancel_limit_hours >= 0),
  booking_window_days    int  not null default 30 check (booking_window_days between 1 and 365),
  min_notice_min         int  not null default 60 check (min_notice_min >= 0),
  reminder_hours_before  int  not null default 3  check (reminder_hours_before >= 0),
  loyalty_goal           int  not null default 9  check (loyalty_goal between 1 and 50),
  updated_at             timestamptz not null default now()
);
create trigger settings_touch before update on public.settings
  for each row execute function public.touch_updated_at();
insert into public.settings default values;

-- ---------------------------------------------------------------------
-- Turnos
-- ---------------------------------------------------------------------
create table public.appointments (
  id                  uuid primary key default gen_random_uuid(),
  client_id           uuid not null references public.profiles (id) on delete cascade,
  barber_id           uuid not null references public.barbers (id),
  service_id          uuid not null references public.services (id),
  starts_at           timestamptz not null,
  ends_at             timestamptz not null,
  status              public.appointment_status not null default 'booked',
  list_price          numeric(10, 2) not null check (list_price >= 0),
  discount            numeric(10, 2) not null default 0 check (discount >= 0),
  price_charged       numeric(10, 2) generated always as (list_price - discount) stored,
  is_free_reward      boolean not null default false,  -- se usa el corte gratis
  counts_for_loyalty  boolean not null,                -- copia del servicio al reservar
  loyalty_applied     boolean not null default false,  -- ya impactó en la tarjeta
  notes               text,
  cancel_reason       text,
  status_changed_at   timestamptz,
  status_changed_by   uuid references public.profiles (id) on delete set null,
  created_at          timestamptz not null default now(),
  updated_at          timestamptz not null default now(),
  check (ends_at > starts_at),
  check (discount <= list_price),
  -- REGLA CLAVE: un mismo barbero nunca puede tener dos turnos activos que se pisen.
  -- La base de datos lo rechaza aunque dos personas reserven en el mismo segundo.
  constraint appointments_no_overlap exclude using gist (
    barber_id with =,
    tstzrange(starts_at, ends_at, '[)') with &&
  ) where (status in ('booked', 'completed'))
);
create index appointments_client on public.appointments (client_id, starts_at desc);
create index appointments_barber_day on public.appointments (barber_id, starts_at);
create trigger appointments_touch before update on public.appointments
  for each row execute function public.touch_updated_at();

-- ---------------------------------------------------------------------
-- Tarjeta de fidelidad (1 por cliente)
--   stamps            = sellos de la tarjeta actual (0 a meta-1)
--   rewards_available = cortes gratis ganados y todavía no usados
-- Al llegar a la meta (9): rewards_available + 1 y la tarjeta nueva arranca en 0.
-- Así, si hace otro corte pago antes de usar el gratis, suma para la próxima.
-- ---------------------------------------------------------------------
create table public.loyalty_cards (
  client_id          uuid primary key references public.profiles (id) on delete cascade,
  stamps             int not null default 0 check (stamps >= 0),
  rewards_available  int not null default 0 check (rewards_available >= 0),
  total_earned       int not null default 0 check (total_earned >= 0),
  total_redeemed     int not null default 0 check (total_redeemed >= 0),
  updated_at         timestamptz not null default now()
);
create trigger loyalty_cards_touch before update on public.loyalty_cards
  for each row execute function public.touch_updated_at();

-- Historial de movimientos de fidelidad: SOLO SE AGREGA, nunca se edita ni borra.
create table public.loyalty_events (
  id               bigint generated always as identity primary key,
  client_id        uuid not null references public.profiles (id) on delete cascade,
  appointment_id   uuid references public.appointments (id) on delete set null,
  type             public.loyalty_event_type not null,
  stamps_before    int not null,
  stamps_after     int not null,
  rewards_before   int not null,
  rewards_after    int not null,
  reason           text,
  created_by       uuid references public.profiles (id) on delete set null,
  created_at       timestamptz not null default now(),
  check (type <> 'manual_adjust' or (reason is not null and length(trim(reason)) > 0))
);
create index loyalty_events_client on public.loyalty_events (client_id, created_at desc);

create or replace function public.loyalty_events_immutable()
returns trigger language plpgsql set search_path = '' as $$
begin
  -- Permitimos el "set null" automático cuando se borra un turno o perfil,
  -- pero nunca cambiar los números ni el motivo.
  if tg_op = 'UPDATE'
     and new.client_id = old.client_id
     and new.type = old.type
     and new.stamps_before = old.stamps_before and new.stamps_after = old.stamps_after
     and new.rewards_before = old.rewards_before and new.rewards_after = old.rewards_after
     and new.reason is not distinct from old.reason
     and new.created_at = old.created_at then
    return new;
  end if;
  if tg_op = 'DELETE' and not exists (select 1 from public.profiles where id = old.client_id) then
    return old; -- se borró la cuenta del cliente (cascada)
  end if;
  raise exception 'El historial de fidelidad no se puede modificar ni borrar';
end $$;
create trigger loyalty_events_no_update before update or delete on public.loyalty_events
  for each row execute function public.loyalty_events_immutable();

-- ---------------------------------------------------------------------
-- Cola de notificaciones (push hoy, WhatsApp en el futuro)
-- ---------------------------------------------------------------------
create table public.notification_queue (
  id              bigint generated always as identity primary key,
  user_id         uuid not null references public.profiles (id) on delete cascade,
  channel         text not null default 'push' check (channel in ('push', 'whatsapp')),
  kind            text not null,           -- booking_confirmed, reminder, loyalty_almost, ...
  title           text not null,
  body            text not null,
  data            jsonb not null default '{}'::jsonb,
  appointment_id  uuid references public.appointments (id) on delete cascade,
  send_after      timestamptz not null default now(),
  status          text not null default 'pending' check (status in ('pending', 'sent', 'failed', 'cancelled')),
  sent_at         timestamptz,
  error           text,
  created_at      timestamptz not null default now()
);
create index notification_queue_pending on public.notification_queue (send_after) where status = 'pending';

-- ---------------------------------------------------------------------
-- Alta automática: cada usuario nuevo recibe su perfil y su tarjeta.
-- El rol SIEMPRE arranca como 'client' (nunca se toma de lo que manda la app).
-- ---------------------------------------------------------------------
create or replace function public.handle_new_user()
returns trigger language plpgsql security definer set search_path = '' as $$
begin
  insert into public.profiles (id, full_name, phone, email)
  values (
    new.id,
    coalesce(new.raw_user_meta_data ->> 'full_name', new.raw_user_meta_data ->> 'name', ''),
    new.raw_user_meta_data ->> 'phone',
    new.email
  );
  insert into public.loyalty_cards (client_id) values (new.id);
  return new;
end $$;

create trigger on_auth_user_created after insert on auth.users
  for each row execute function public.handle_new_user();

-- =====================================================================
-- SEGURIDAD (Row Level Security)
-- Regla general: el cliente ve SOLO lo suyo y no puede escribir turnos
-- ni la tarjeta directamente. Todo cambio importante pasa por funciones
-- del servidor que validan las reglas.
-- =====================================================================
alter table public.profiles           enable row level security;
alter table public.barbers            enable row level security;
alter table public.services           enable row level security;
alter table public.barber_services    enable row level security;
alter table public.working_hours      enable row level security;
alter table public.time_off           enable row level security;
alter table public.settings           enable row level security;
alter table public.appointments       enable row level security;
alter table public.loyalty_cards      enable row level security;
alter table public.loyalty_events     enable row level security;
alter table public.notification_queue enable row level security;

-- Perfiles: cada uno ve el suyo; el staff ve todos.
create policy profiles_select on public.profiles for select to authenticated
  using (id = auth.uid() or public.is_staff());
create policy profiles_update_own on public.profiles for update to authenticated
  using (id = auth.uid()) with check (id = auth.uid());
-- El usuario solo puede cambiar estas columnas (NUNCA su rol).
revoke update on public.profiles from anon, authenticated;
grant update (full_name, phone, push_token) on public.profiles to authenticated;
revoke insert, delete on public.profiles from anon, authenticated;

-- Catálogo público (servicios, barberos, horarios, configuración): todos leen, admin escribe.
create policy barbers_read  on public.barbers  for select to anon, authenticated using (true);
create policy barbers_admin on public.barbers  for all to authenticated using (public.is_admin()) with check (public.is_admin());
create policy services_read  on public.services for select to anon, authenticated using (active or public.is_staff());
create policy services_admin on public.services for all to authenticated using (public.is_admin()) with check (public.is_admin());
create policy barber_services_read  on public.barber_services for select to anon, authenticated using (true);
create policy barber_services_admin on public.barber_services for all to authenticated using (public.is_admin()) with check (public.is_admin());
create policy working_hours_read  on public.working_hours for select to anon, authenticated using (true);
create policy working_hours_admin on public.working_hours for all to authenticated using (public.is_admin()) with check (public.is_admin());
create policy settings_read  on public.settings for select to anon, authenticated using (true);
create policy settings_admin on public.settings for update to authenticated using (public.is_admin()) with check (public.is_admin());

-- Bloqueos: solo el staff los ve (el cliente solo ve horarios libres).
create policy time_off_staff on public.time_off for select to authenticated using (public.is_staff());
create policy time_off_admin on public.time_off for all to authenticated using (public.is_admin()) with check (public.is_admin());

-- Turnos: el cliente ve los suyos; el staff ve todos.
-- Nadie escribe directo: reservar, cancelar y cambiar estado se hace por funciones del servidor.
create policy appointments_select on public.appointments for select to authenticated
  using (client_id = auth.uid() or public.is_staff());

-- Fidelidad: el cliente SOLO puede ver. Nadie escribe directo desde la app.
create policy loyalty_cards_select on public.loyalty_cards for select to authenticated
  using (client_id = auth.uid() or public.is_staff());
create policy loyalty_events_select on public.loyalty_events for select to authenticated
  using (client_id = auth.uid() or public.is_staff());

-- Notificaciones: cada uno ve las suyas; staff todas.
create policy notification_queue_select on public.notification_queue for select to authenticated
  using (user_id = auth.uid() or public.is_staff());

-- Doble candado: quitamos permisos de escritura de las tablas sensibles.
revoke insert, update, delete on public.appointments       from anon, authenticated;
revoke insert, update, delete on public.loyalty_cards      from anon, authenticated;
revoke insert, update, delete on public.loyalty_events     from anon, authenticated;
revoke insert, update, delete on public.notification_queue from anon, authenticated;
