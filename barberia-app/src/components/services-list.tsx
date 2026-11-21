import { View } from 'react-native';

import { colors, spacing } from '@/constants/theme';
import type { Service } from '@/lib/types';
import { formatDuration, formatPrice } from '@/lib/validation';

import { Card, T } from './ui';

export function ServicesList({ services }: { services: Service[] }) {
  if (!services.length) return <T variant="muted">Todavía no hay servicios cargados.</T>;
  return (
    <View style={{ gap: spacing.sm }}>
      {services.map((s) => (
        <Card key={s.id} style={{ flexDirection: 'row', alignItems: 'center', gap: spacing.md }}>
          <View style={{ flex: 1, gap: 2 }}>
            <T variant="subtitle">{s.name}</T>
            <T variant="muted">
              {formatDuration(s.duration_min)}
              {s.counts_for_loyalty ? '  ·  suma a tu tarjeta ✂' : ''}
            </T>
          </View>
          <T style={{ color: colors.gold, fontSize: 20, fontWeight: '700' }}>{formatPrice(s.price)}</T>
        </Card>
      ))}
    </View>
  );
}
