// Menú de gestión del admin
import Ionicons from '@expo/vector-icons/Ionicons';
import { router, type Href } from 'expo-router';
import { Pressable, StyleSheet, View } from 'react-native';

import { Screen, T } from '@/components/ui';
import { colors, radius, spacing } from '@/constants/theme';

const items: { icon: React.ComponentProps<typeof Ionicons>['name']; title: string; desc: string; href: Href }[] = [
  { icon: 'cut', title: 'Servicios', desc: 'Precios, duración y qué suma a la tarjeta', href: '/servicios' },
  { icon: 'time', title: 'Días y horarios', desc: 'Qué días abrís y en qué horario', href: '/horarios' },
  { icon: 'calendar-clear', title: 'Feriados y bloqueos', desc: 'Días cerrados, vacaciones o unas horas', href: '/bloqueos' },
  { icon: 'options', title: 'Reglas de reserva', desc: 'Cancelación, anticipación y límites', href: '/reglas' },
];

export default function Gestion() {
  return (
    <Screen>
      <T variant="title">Gestión</T>
      <View style={{ gap: spacing.sm }}>
        {items.map((it) => (
          <Pressable key={it.title} onPress={() => router.push(it.href)} accessibilityRole="button"
            style={({ pressed }) => [styles.item, pressed && { borderColor: colors.gold }]}>
            <View style={styles.icon}><Ionicons name={it.icon} size={26} color={colors.gold} /></View>
            <View style={{ flex: 1, gap: 2 }}>
              <T variant="subtitle">{it.title}</T>
              <T variant="muted">{it.desc}</T>
            </View>
            <Ionicons name="chevron-forward" size={22} color={colors.textMuted} />
          </Pressable>
        ))}
      </View>
    </Screen>
  );
}

const styles = StyleSheet.create({
  item: {
    flexDirection: 'row', alignItems: 'center', gap: spacing.md, minHeight: 84, padding: spacing.md,
    backgroundColor: colors.surface, borderRadius: radius.lg, borderWidth: 1, borderColor: colors.border,
  },
  icon: { width: 52, height: 52, borderRadius: 26, backgroundColor: colors.surfaceAlt, alignItems: 'center', justifyContent: 'center' },
});
