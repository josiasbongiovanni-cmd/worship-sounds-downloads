-- Herramientas para las pruebas
create schema if not exists tests;
grant usage on schema tests to anon, authenticated;

-- Crea un usuario como si se registrara en la app
create or replace function tests.create_user(p_email text, p_name text, p_role public.user_role default 'client')
returns uuid language plpgsql as $$
declare v uuid;
begin
  insert into auth.users (email, raw_user_meta_data)
  values (p_email, jsonb_build_object('full_name', p_name, 'phone', '+5493410000000'))
  returning id into v;
  update public.profiles set role = p_role where id = v;
  return v;
end $$;

-- "Inicia sesión" como ese usuario (usa los mismos permisos que la app)
create or replace function tests.login(p_user uuid) returns void language plpgsql as $$
begin
  perform set_config('request.jwt.claim.sub', p_user::text, true);
  execute 'set local role authenticated';
end $$;

create or replace function tests.logout() returns void language plpgsql as $$
begin
  execute 'reset role';
  perform set_config('request.jwt.claim.sub', '', true);
end $$;

create or replace function tests.ok(p_cond boolean, p_msg text) returns void language plpgsql as $$
begin
  if p_cond is distinct from true then
    raise exception 'FALLÓ: %', p_msg;
  end if;
  raise notice 'ok - %', p_msg;
end $$;
grant execute on all functions in schema tests to anon, authenticated;
