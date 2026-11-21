// Inicio del cliente
import { Brand, Button, Card, Loading, Screen, T } from '@/components/ui';
import { LoyaltyStamps } from '@/components/loyalty-stamps';
import { ServicesList } from '@/components/services-list';
import { fetchMyLoyaltyCard, fetchServices, fetchSettings, useLoad } from '@/lib/data';
import { useAuth } from '@/providers/auth';

export default function ClientHome() {
  const { profile } = useAuth();
  const { data, error, loading } = useLoad(async () => {
    const [services, settings, card] = await Promise.all([fetchServices(), fetchSettings(), fetchMyLoyaltyCard(profile!.id)]);
    return { services, settings, card };
  });
  const firstName = profile?.full_name.split(' ')[0] ?? '';

  if (loading && !data) return <Loading />;
  return (
    <Screen>
      <Brand size="small" />
      <T variant="title">Hola, {firstName} 👋</T>

      <Button label="Reservar turno (próximamente)" disabled />
      <T variant="muted" style={{ textAlign: 'center' }}>Las reservas se activan en la Fase 2.</T>

      {error ? <Card><T variant="error">{error}</T></Card> : null}
      {data ? <LoyaltyStamps card={data.card} goal={data.settings.loyalty_goal} /> : null}

      <T variant="subtitle" style={{ marginTop: 8 }}>Servicios</T>
      {data ? <ServicesList services={data.services} /> : null}
    </Screen>
  );
}
