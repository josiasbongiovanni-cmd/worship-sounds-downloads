import { LoyaltyStamps } from '@/components/loyalty-stamps';
import { Card, Loading, Screen, T } from '@/components/ui';
import { fetchMyLoyaltyCard, fetchSettings, useLoad } from '@/lib/data';
import { useAuth } from '@/providers/auth';

export default function LoyaltyScreen() {
  const { profile } = useAuth();
  const { data, error, loading } = useLoad(async () => {
    const [settings, card] = await Promise.all([fetchSettings(), fetchMyLoyaltyCard(profile!.id)]);
    return { settings, card };
  });
  if (loading && !data) return <Loading />;
  const goal = data?.settings.loyalty_goal ?? 9;
  return (
    <Screen>
      <T variant="title">Mi tarjeta</T>
      {error ? <T variant="error">{error}</T> : null}
      <LoyaltyStamps card={data?.card ?? null} goal={goal} />
      <Card>
        <T variant="subtitle">¿Cómo funciona?</T>
        <T variant="muted">• Cada corte que completás en la barbería suma 1 sello.</T>
        <T variant="muted">• Al juntar {goal} sellos, tu próximo corte es GRATIS.</T>
        <T variant="muted">• El corte gratis se aplica solo cuando reservás.</T>
        <T variant="muted">• Si elegís un servicio más caro, se descuenta el precio del corte y pagás la diferencia.</T>
      </Card>
      {data?.card ? (
        <T variant="muted">Cortes sumados en total: {data.card.total_earned} · Gratis usados: {data.card.total_redeemed}</T>
      ) : null}
    </Screen>
  );
}
