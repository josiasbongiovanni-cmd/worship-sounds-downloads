// Pantalla de inicio de sesión
import { Link } from 'expo-router';
import { useRef, useState } from 'react';
import { View, type TextInputInstance } from 'react-native';

import { Brand, Button, Card, Input, Screen, T } from '@/components/ui';
import { spacing } from '@/constants/theme';
import { friendlyError, isSupabaseConfigured, supabase } from '@/lib/supabase';
import { isValidEmail } from '@/lib/validation';

export default function LoginScreen() {
  const [email, setEmail] = useState('');
  const [password, setPassword] = useState('');
  const [loading, setLoading] = useState(false);
  const [error, setError] = useState<string | null>(null);
  const passwordRef = useRef<TextInputInstance>(null);

  async function onLogin() {
    setError(null);
    if (!isValidEmail(email)) return setError('Ingresá un email válido.');
    if (!password) return setError('Ingresá tu contraseña.');
    setLoading(true);
    const { error: err } = await supabase.auth.signInWithPassword({ email: email.trim().toLowerCase(), password });
    setLoading(false);
    if (err) setError(friendlyError(err.message));
    // Si sale bien, la app cambia sola a la pantalla que corresponde.
  }

  return (
    <Screen>
      <View style={{ paddingTop: spacing.xxl, paddingBottom: spacing.xl }}>
        <Brand />
        <T variant="muted" style={{ textAlign: 'center', marginTop: spacing.md }}>
          Reservá tu turno y sumá cortes gratis
        </T>
      </View>

      {!isSupabaseConfigured && (
        <Card highlight>
          <T variant="gold">Falta configurar Supabase</T>
          <T variant="muted">Creá el archivo .env con la URL y la clave de tu proyecto (ver GUIA_FASE_1.md).</T>
        </Card>
      )}

      <Input
        label="EMAIL"
        value={email}
        onChangeText={setEmail}
        placeholder="tu@email.com"
        keyboardType="email-address"
        autoCapitalize="none"
        autoComplete="email"
        textContentType="emailAddress"
        returnKeyType="next"
        onSubmitEditing={() => passwordRef.current?.focus()}
      />
      <Input
        ref={passwordRef}
        label="CONTRASEÑA"
        value={password}
        onChangeText={setPassword}
        placeholder="••••••"
        secureTextEntry
        autoComplete="password"
        textContentType="password"
        returnKeyType="go"
        onSubmitEditing={onLogin}
      />
      {error ? <T variant="error">{error}</T> : null}

      <Button label="Ingresar" onPress={onLogin} loading={loading} />

      <View style={{ alignItems: 'center', gap: spacing.sm, marginTop: spacing.md }}>
        <T variant="muted">¿Primera vez?</T>
        <Link href="/registro" asChild>
          <Button label="Crear mi cuenta" variant="outline" />
        </Link>
      </View>
    </Screen>
  );
}
