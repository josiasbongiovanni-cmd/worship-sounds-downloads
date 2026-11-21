import { View } from 'react-native';

import { Card, Loading, Screen, T } from '@/components/ui';
import { spacing } from '@/constants/theme';
import { useLoad } from '@/lib/data';
import { supabase } from '@/lib/supabase';
import { formatArPhone } from '@/lib/validation';

export default function AdminClients() {
  const { data, error, loading } = useLoad(async () => {
    const { data, error } = await supabase
      .from('profiles')
      .select('id, full_name, phone, email, role, loyalty_cards(stamps, rewards_available)')
      .eq('role', 'client')
      .order('full_name');
    if (error) throw error;
    return data;
  });
  if (loading && !data) return <Loading />;
  return (
    <Screen>
      <T variant="title">Clientes</T>
      {error ? <T variant="error">{error}</T> : null}
      {data?.length === 0 ? <T variant="muted">Todavía no hay clientes registrados.</T> : null}
      <View style={{ gap: spacing.sm }}>
        {data?.map((c) => {
          const card = Array.isArray(c.loyalty_cards) ? c.loyalty_cards[0] : c.loyalty_cards;
          return (
            <Card key={c.id}>
              <T variant="subtitle">{c.full_name || '(sin nombre)'}</T>
              <T variant="muted">{formatArPhone(c.phone)} · {c.email}</T>
              <T variant="gold">
                {card?.stamps ?? 0} sellos{card?.rewards_available ? ` · ${card.rewards_available} gratis disponible` : ''}
              </T>
            </Card>
          );
        })}
      </View>
    </Screen>
  );
}
