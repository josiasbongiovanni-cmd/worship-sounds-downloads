import { Stack } from 'expo-router';

import { colors } from '@/constants/theme';

export default function AdminLayout() {
  return (
    <Stack
      screenOptions={{
        headerStyle: { backgroundColor: colors.background },
        headerTintColor: colors.gold,
        headerTitleStyle: { color: colors.text },
        headerShadowVisible: false,
        headerBackTitle: 'Volver',
        contentStyle: { backgroundColor: colors.background },
      }}>
      <Stack.Screen name="(tabs)" options={{ headerShown: false }} />
      <Stack.Screen name="servicios" options={{ title: 'Servicios' }} />
      <Stack.Screen name="servicio" options={{ title: 'Servicio' }} />
      <Stack.Screen name="horarios" options={{ title: 'Días y horarios' }} />
      <Stack.Screen name="bloqueos" options={{ title: 'Feriados y bloqueos' }} />
      <Stack.Screen name="reglas" options={{ title: 'Reglas de reserva' }} />
    </Stack>
  );
}
