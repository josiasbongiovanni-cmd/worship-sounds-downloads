// Lista de servicios (admin)
import Ionicons from '@expo/vector-icons/Ionicons';
import { router } from 'expo-router';
import { Pressable, View } from 'react-native';

import { Button, Card, Loading, Screen, T } from '@/components/ui';
import { colors, spacing } from '@/constants/theme';
import { fetchAllServices } from '@/lib/admin';
import { useLoad } from '@/lib/data';
import { friendlyError } from '@/lib/supabase';
import { formatDuration, formatPrice } from '@/lib/validation';

export default function AdminServices() {
  const { data, error, loading } = useLoad(fetchAllServices);
  if (loading && !data) return <Loading />;
  return (
    <Screen withHeader>
      <Button label="+ Nuevo servicio" onPress={() => router.push('/servicio')} />
      {error ? <T variant="error">{friendlyError(error)}</T> : null}
      <View style={{ gap: spacing.sm }}>
        {data?.map((s) => (
          <Pressable key={s.id} onPress={() => router.push({ pathname: '/servicio', params: { id: s.id } })} accessibilityRole="button">
            <Card style={{ flexDirection: 'row', alignItems: 'center', gap: spacing.md, opacity: s.active ? 1 : 0.5 }}>
              <View style={{ flex: 1, gap: 2 }}>
                <T variant="subtitle">{s.name}</T>
                <T variant="muted">
                  {formatDuration(s.duration_min)}
                  {s.counts_for_loyalty ? ' · suma a la tarjeta' : ' · no suma'}
                  {s.is_reward_base ? ' · base del corte gratis' : ''}
                </T>
                {!s.active ? <T style={{ color: colors.danger, fontSize: 13 }}>DESACTIVADO (los clientes no lo ven)</T> : null}
              </View>
              <T style={{ color: colors.gold, fontSize: 20, fontWeight: '700' }}>{formatPrice(s.price)}</T>
              <Ionicons name="create-outline" size={22} color={colors.textMuted} />
            </Card>
          </Pressable>
        ))}
      </View>
    </Screen>
  );
}
