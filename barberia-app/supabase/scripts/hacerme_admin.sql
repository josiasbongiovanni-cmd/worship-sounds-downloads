-- =====================================================================
-- Convertir una cuenta en ADMIN de Mathu Barber
-- 1) Registrate primero en la app con tu email.
-- 2) Cambiá el email de abajo por el tuyo.
-- 3) Pegá esto en Supabase > SQL Editor y tocá "Run".
-- =====================================================================
update public.profiles
set role = 'admin'
where email = 'TU-EMAIL@ejemplo.com';

-- Vincula tu cuenta con el barbero "Mathu" (para ver tu agenda)
update public.barbers
set profile_id = (select id from public.profiles where email = 'TU-EMAIL@ejemplo.com')
where display_name = 'Mathu';

-- Verificación: debería mostrar tu nombre con role = admin
select full_name, email, role from public.profiles where role = 'admin';
