// Registro de cliente nuevo: nombre, WhatsApp, email y contraseña
import { useRef, useState } from 'react';
import { Alert, type TextInputInstance } from 'react-native';
import { router } from 'expo-router';

import { Button, Input, Screen, T } from '@/components/ui';
import { friendlyError, supabase } from '@/lib/supabase';
import { isValidEmail, normalizeArPhone } from '@/lib/validation';

export default function RegisterScreen() {
  const [name, setName] = useState('');
  const [phone, setPhone] = useState('');
  const [email, setEmail] = useState('');
  const [password, setPassword] = useState('');
  const [errors, setErrors] = useState<Record<string, string>>({});
  const [loading, setLoading] = useState(false);
  const [formError, setFormError] = useState<string | null>(null);
  const phoneRef = useRef<TextInputInstance>(null);
  const emailRef = useRef<TextInputInstance>(null);
  const passRef = useRef<TextInputInstance>(null);

  async function onRegister() {
    setFormError(null);
    const e: Record<string, string> = {};
    const phoneE164 = normalizeArPhone(phone);
    if (name.trim().length < 2) e.name = 'Ingresá tu nombre y apellido.';
    if (!phoneE164) e.phone = 'Ingresá un celular válido, con código de área. Ej: 341 555 1234';
    if (!isValidEmail(email)) e.email = 'Ingresá un email válido.';
    if (password.length < 6) e.password = 'Mínimo 6 caracteres.';
    setErrors(e);
    if (Object.keys(e).length) return;

    setLoading(true);
    const { data, error } = await supabase.auth.signUp({
      email: email.trim().toLowerCase(),
      password,
      options: { data: { full_name: name.trim(), phone: phoneE164 } },
    });
    setLoading(false);
    if (error) return setFormError(friendlyError(error.message));

    if (!data.session) {
      // Supabase pide confirmar el email antes de entrar
      Alert.alert('¡Cuenta creada!', 'Te enviamos un email para confirmar tu cuenta. Después volvé e iniciá sesión.');
      router.back();
    }
    // Si ya hay sesión, la app entra sola.
  }

  return (
    <Screen>
      <T variant="title">Crear cuenta</T>
      <T variant="muted">Con tus datos te avisamos de tus turnos y llevamos tu tarjeta de cortes.</T>

      <Input label="NOMBRE Y APELLIDO" value={name} onChangeText={setName} placeholder="Juan Pérez"
        autoCapitalize="words" autoComplete="name" textContentType="name" error={errors.name}
        returnKeyType="next" onSubmitEditing={() => phoneRef.current?.focus()} />
      <Input ref={phoneRef} label="CELULAR (WHATSAPP)" value={phone} onChangeText={setPhone} placeholder="341 555 1234"
        keyboardType="phone-pad" autoComplete="tel" textContentType="telephoneNumber" error={errors.phone}
        hint="Con código de área, sin 0 ni 15." returnKeyType="next" onSubmitEditing={() => emailRef.current?.focus()} />
      <Input ref={emailRef} label="EMAIL" value={email} onChangeText={setEmail} placeholder="tu@email.com"
        keyboardType="email-address" autoCapitalize="none" autoComplete="email" textContentType="emailAddress"
        error={errors.email} returnKeyType="next" onSubmitEditing={() => passRef.current?.focus()} />
      <Input ref={passRef} label="CONTRASEÑA" value={password} onChangeText={setPassword} placeholder="Mínimo 6 caracteres"
        secureTextEntry autoComplete="new-password" textContentType="newPassword" error={errors.password}
        returnKeyType="go" onSubmitEditing={onRegister} />

      {formError ? <T variant="error">{formError}</T> : null}
      <Button label="Crear mi cuenta" onPress={onRegister} loading={loading} />
    </Screen>
  );
}
