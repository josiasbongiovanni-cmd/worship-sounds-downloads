import Ionicons from '@expo/vector-icons/Ionicons';
import type { ComponentProps } from 'react';

import { colors } from '@/constants/theme';

export const tabScreenOptions = {
  headerShown: false,
  tabBarActiveTintColor: colors.gold,
  tabBarInactiveTintColor: colors.textMuted,
  tabBarStyle: { backgroundColor: colors.surface, borderTopColor: colors.border, height: 68, paddingTop: 6, paddingBottom: 10 },
  tabBarLabelStyle: { fontSize: 12, fontWeight: '600' as const },
  sceneStyle: { backgroundColor: colors.background },
};

/** Ícono para la barra de abajo */
export function tabIcon(name: ComponentProps<typeof Ionicons>['name']) {
  return function TabIcon({ color, size }: { color: unknown; size: number }) {
    return <Ionicons name={name} color={String(color)} size={size} />;
  };
}
