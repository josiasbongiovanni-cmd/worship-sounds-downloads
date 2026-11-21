import { DarkTheme, Stack, ThemeProvider } from 'expo-router';
import * as SplashScreen from 'expo-splash-screen';
import { StatusBar } from 'expo-status-bar';
import { useEffect } from 'react';

import { colors } from '@/constants/theme';
import { AuthProvider, useAuth } from '@/providers/auth';

SplashScreen.preventAutoHideAsync();

const theme = {
  ...DarkTheme,
  colors: { ...DarkTheme.colors, background: colors.background, card: colors.surface, primary: colors.gold, text: colors.text, border: colors.border },
};

function RootNavigator() {
  const { loading, session, profile, isStaff } = useAuth();

  useEffect(() => {
    if (!loading) SplashScreen.hideAsync();
  }, [loading]);

  if (loading) return null;

  // Cada grupo de pantallas solo existe para quien corresponde:
  // sin sesión -> login; cliente -> app de cliente; admin/barbero -> panel admin.
  return (
    <Stack screenOptions={{ headerShown: false, contentStyle: { backgroundColor: colors.background } }}>
      <Stack.Protected guard={!session}>
        <Stack.Screen name="(auth)" />
      </Stack.Protected>
      <Stack.Protected guard={!!session && !!profile && !isStaff}>
        <Stack.Screen name="(cliente)" />
      </Stack.Protected>
      <Stack.Protected guard={!!session && !!profile && isStaff}>
        <Stack.Screen name="(admin)" />
      </Stack.Protected>
      <Stack.Protected guard={!!session && !profile}>
        <Stack.Screen name="index" />
      </Stack.Protected>
    </Stack>
  );
}

export default function RootLayout() {
  return (
    <ThemeProvider value={theme}>
      <AuthProvider>
        <StatusBar style="light" />
        <RootNavigator />
      </AuthProvider>
    </ThemeProvider>
  );
}
