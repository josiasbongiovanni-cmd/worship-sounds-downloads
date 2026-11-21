# Mathu Barber 💈

App móvil (Android + iPhone) para reservar turnos en **Mathu Barber**, Villa Gobernador Gálvez, con tarjeta de fidelidad: **cada 9 cortes, el 10º es gratis**.

- **App:** React Native + Expo (Expo Router), en la carpeta `src/`
- **Backend:** Supabase. La base de datos, la seguridad y las reglas están en `supabase/migrations/`
- **Pruebas:** `supabase/tests/` (base de datos) y `src/**/*.test.ts` (lógica)

👉 Para empezar, leé **[GUIA_FASE_1.md](./GUIA_FASE_1.md)** después **[GUIA_FASE_2.md](./GUIA_FASE_2.md)** y **[GUIA_FASE_3.md](./GUIA_FASE_3.md)**.

## Estructura

```
src/app/(auth)      login y registro
src/app/(cliente)   inicio, mis turnos, mi tarjeta, perfil
src/app/(admin)     agenda, clientes, gestión (servicios, horarios, bloqueos, reglas)
src/components      botones, tarjeta de sellos, etc.
src/lib             conexión a Supabase, validaciones, tipos
supabase/migrations base de datos (correr en orden en el SQL Editor)
supabase/scripts    scripts útiles (hacerme admin)
supabase/tests      pruebas de seguridad, turnos superpuestos y fidelidad
```

## Estado

- [x] Fase 1: base, base de datos, login y roles
- [x] Fase 2: reservas
- [x] Fase 3: panel admin
- [ ] Fase 4: fidelidad
- [ ] Fase 5: notificaciones
- [ ] Fase 6: diseño final y publicación
