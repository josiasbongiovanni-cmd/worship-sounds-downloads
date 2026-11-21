import { View } from 'react-native';

import { colors, spacing } from '@/constants/theme';
import { statusLabel, type Appointment } from '@/lib/booking';
import { formatDateTime, localDay, relativeDay } from '@/lib/dates';
import { formatPrice } from '@/lib/validation';

import { Card, T } from './ui';

const statusColor = { booked: colors.gold, completed: colors.success, no_show: colors.danger, cancelled: colors.textMuted } as const;

export function AppointmentCard({ a, children, highlight }: { a: Appointment; children?: React.ReactNode; highlight?: boolean }) {
  const rel = relativeDay(localDay(a.starts_at));
  return (
    <Card highlight={highlight}>
      <View style={{ flexDirection: 'row', justifyContent: 'space-between', alignItems: 'center' }}>
        <T variant="subtitle">{a.service?.name ?? 'Servicio'}</T>
        <T style={{ color: statusColor[a.status], fontWeight: '700', fontSize: 14 }}>{statusLabel[a.status].toUpperCase()}</T>
      </View>
      <T style={{ fontSize: 18 }}>{rel ? `${rel} · ` : ''}{formatDateTime(a.starts_at)}</T>
      <View style={{ flexDirection: 'row', gap: spacing.md, flexWrap: 'wrap' }}>
        {a.barber ? <T variant="muted">con {a.barber.display_name}</T> : null}
        <T variant="muted">{formatPrice(a.price_charged)}</T>
        {a.is_free_reward ? <T variant="gold" style={{ fontSize: 14 }}>🎉 Corte gratis</T> : null}
      </View>
      {children ? <View style={{ gap: spacing.sm, marginTop: spacing.sm }}>{children}</View> : null}
    </Card>
  );
}
