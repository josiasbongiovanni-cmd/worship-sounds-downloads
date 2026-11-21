-- Fase 1: alta de usuarios, roles y seguridad básica
begin;

do $$
declare
  ana uuid; beto uuid; admin uuid;
  v_role public.user_role; v_count int; v_name text;
  failed boolean;
begin
  ana   := tests.create_user('ana@test.com', 'Ana');
  beto  := tests.create_user('beto@test.com', 'Beto');
  admin := tests.create_user('admin@test.com', 'Admin', 'admin');

  -- Alta automática
  perform tests.ok((select role from public.profiles where id = ana) = 'client', 'un usuario nuevo arranca como cliente');
  perform tests.ok((select full_name from public.profiles where id = ana) = 'Ana', 'se guarda el nombre del registro');
  perform tests.ok((select phone from public.profiles where id = ana) = '+5493410000000', 'se guarda el teléfono del registro');
  perform tests.ok(exists (select 1 from public.loyalty_cards where client_id = ana and stamps = 0 and rewards_available = 0),
                   'cada usuario nuevo recibe su tarjeta de fidelidad en 0');

  -- El registro no permite elegir rol
  insert into auth.users (email, raw_user_meta_data) values ('tramposo@test.com', '{"full_name":"T","role":"admin"}');
  perform tests.ok((select role from public.profiles where email = 'tramposo@test.com') = 'client',
                   'mandar role=admin al registrarse NO da permisos de admin');

  -- ===== Como cliente Ana =====
  perform tests.login(ana);

  update public.profiles set full_name = 'Ana María' where id = ana;
  perform tests.ok((select full_name from public.profiles where id = ana) = 'Ana María', 'el cliente puede editar su nombre');

  failed := false;
  begin update public.profiles set role = 'admin' where id = ana;
  exception when insufficient_privilege then failed := true; end;
  perform tests.ok(failed, 'el cliente NO puede cambiarse el rol a admin');

  update public.profiles set full_name = 'hackeado' where id = beto;
  perform tests.ok((select count(*) from public.profiles where full_name = 'hackeado') = 0, 'el cliente NO puede editar el perfil de otro');

  perform tests.ok((select count(*) from public.profiles) = 1, 'el cliente solo ve su propio perfil');
  perform tests.ok((select count(*) from public.loyalty_cards) = 1, 'el cliente solo ve su propia tarjeta');

  failed := false;
  begin update public.loyalty_cards set stamps = 8 where client_id = ana;
  exception when insufficient_privilege then failed := true; end;
  perform tests.ok(failed, 'el cliente NO puede modificar su tarjeta de fidelidad');

  failed := false;
  begin insert into public.loyalty_events (client_id, type, stamps_before, stamps_after, rewards_before, rewards_after)
        values (ana, 'earn', 0, 9, 0, 1);
  exception when insufficient_privilege then failed := true; end;
  perform tests.ok(failed, 'el cliente NO puede agregar movimientos de fidelidad');

  failed := false;
  begin insert into public.appointments (client_id, barber_id, service_id, starts_at, ends_at, list_price, counts_for_loyalty)
        select ana, b.id, s.id, now() + interval '1 day', now() + interval '1 day 30 min', 0, true
        from public.barbers b, public.services s limit 1;
  exception when insufficient_privilege then failed := true; end;
  perform tests.ok(failed, 'el cliente NO puede crear turnos directamente (solo por la función de reservar)');

  failed := false;
  begin update public.services set price = 1;
  exception when insufficient_privilege then failed := true; end;
  perform tests.ok(failed or (select count(*) from public.services where price = 1) = 0, 'el cliente NO puede cambiar precios');

  perform tests.ok((select count(*) from public.services where active) = 2, 'el cliente ve los 2 servicios activos');
  perform tests.logout();

  -- ===== Como admin =====
  perform tests.login(admin);
  perform tests.ok((select count(*) from public.profiles) >= 4, 'el admin ve todos los perfiles');
  perform tests.ok((select count(*) from public.loyalty_cards) >= 4, 'el admin ve todas las tarjetas');
  update public.services set price = 9000 where name = 'Corte';
  perform tests.ok((select price from public.services where name = 'Corte') = 9000, 'el admin puede cambiar precios');
  perform tests.logout();
end $$;

rollback;
