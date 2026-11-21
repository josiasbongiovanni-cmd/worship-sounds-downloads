// Tarjeta de sellos: muestra el progreso "X de 9" con sellos visuales
import Ionicons from '@expo/vector-icons/Ionicons';
import { StyleSheet, View } from 'react-native';

import { colors, radius, spacing } from '@/constants/theme';
import type { LoyaltyCard } from '@/lib/types';

import { Card, T } from './ui';

export function LoyaltyStamps({ card, goal }: { card: LoyaltyCard | null; goal: number }) {
  const stamps = card?.stamps ?? 0;
  const rewards = card?.rewards_available ?? 0;
  const hasReward = rewards > 0;
  // Si tiene un gratis pendiente y todavía no empezó la tarjeta nueva, mostramos la tarjeta llena.
  const filled = hasReward && stamps === 0 ? goal : stamps;
  const remaining = goal - stamps;

  return (
    <Card highlight={hasReward}>
      <T variant="muted" style={{ letterSpacing: 2 }}>TARJETA DE FIDELIDAD</T>
      <View style={styles.grid}>
        {Array.from({ length: goal }, (_, i) => (
          <View key={i} style={[styles.stamp, i < filled && styles.stampOn]}>
            <Ionicons name="cut" size={22} color={i < filled ? colors.onGold : colors.border} />
          </View>
        ))}
        <View style={[styles.stamp, styles.free, hasReward && styles.freeOn]}>
          <T style={{ fontSize: 11, fontWeight: '800', color: hasReward ? colors.onGold : colors.gold }}>GRATIS</T>
        </View>
      </View>

      {hasReward ? (
        <>
          <T variant="gold" style={{ fontSize: 20 }}>¡Tu próximo corte es GRATIS! 🎉</T>
          {rewards > 1 ? <T variant="muted">Tenés {rewards} cortes gratis disponibles.</T> : null}
          {stamps > 0 ? <T variant="muted">Y ya llevás {stamps} de {goal} en tu próxima tarjeta.</T> : null}
        </>
      ) : (
        <>
          <T variant="subtitle">{stamps} de {goal} cortes</T>
          <T variant="muted">
            {remaining === 1 ? '¡Te falta 1 corte para el gratis!' : `Te faltan ${remaining} cortes para tu corte gratis.`}
          </T>
        </>
      )}
    </Card>
  );
}

const styles = StyleSheet.create({
  grid: { flexDirection: 'row', flexWrap: 'wrap', gap: spacing.sm, justifyContent: 'center', paddingVertical: spacing.sm },
  stamp: {
    width: 52, height: 52, borderRadius: radius.pill, borderWidth: 2, borderColor: colors.border,
    alignItems: 'center', justifyContent: 'center', backgroundColor: colors.surfaceAlt,
  },
  stampOn: { backgroundColor: colors.gold, borderColor: colors.goldSoft },
  free: { borderStyle: 'dashed', borderColor: colors.gold },
  freeOn: { backgroundColor: colors.gold, borderStyle: 'solid' },
});
