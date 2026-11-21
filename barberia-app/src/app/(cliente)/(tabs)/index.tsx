// Inicio del cliente
import { router } from 'expo-router';

import { AppointmentCard } from '@/components/appointment-card';
import { LoyaltyStamps } from '@/components/loyalty-stamps';
import { ServicesList } from '@/components/services-list';
import { Brand, Button, Card, Loading, Screen, T } from '@/components/ui';
import { fetchMyAppointments } from '@/lib/booking';
import { fetchMyLoyaltyCard, fetchServices, fetchSettings, useLoad } from '@/lib/data';
import { friendlyError } from '@/lib/supabase';
import { useAuth } from '@/providers/auth';

export default function ClientHome() {
  const { profile } = useAuth();
  const { data, error, loading } = useLoad(async () => {
    const [services, settings, card, appts] = await Promise.all([
      fetchServices(),
      fetchSettings(),
      fetchMyLoyaltyCard(profile!.id),
      fetchMyAppointments(profile!.id),
    ]);
    return { services, settings, card, next: appts.upcoming[0] ?? null };
  });
  const firstName = profile?.full_name.split(' ')[0] ?? '';

  if (loading && !data) return <Loading />;
  return (
    <Screen>
      <Brand size="small" />
      <T variant="title">Hola, {firstName} 👋</T>

      {error ? <Card><T variant="error">{friendlyError(error)}</T></Card> : null}

      {data?.next ? (
        <>
          <T variant="muted">Tu próximo turno</T>
          <AppointmentCard a={data.next} highlight />
        </>
      ) : null}

      <Button label="Reservar turno" onPress={() => router.push('/reservar')} />

      {data ? <LoyaltyStamps card={data.card} goal={data.settings.loyalty_goal} /> : null}

      <T variant="subtitle" style={{ marginTop: 8 }}>Servicios</T>
      {data ? <ServicesList services={data.services} /> : null}
    </Screen>
  );
}
