// Pantalla de perfil (la usan cliente y admin)
import { useState } from 'react';
import { Alert } from 'react-native';

import { colors } from '@/constants/theme';
import { friendlyError, supabase } from '@/lib/supabase';
import { formatArPhone, normalizeArPhone } from '@/lib/validation';
import { useAuth } from '@/providers/auth';

import { Button, Card, Input, Screen, T } from './ui';

const roleLabel = { client: 'Cliente', barber: 'Barbero', admin: 'Administrador' } as const;

export function ProfileView() {
  const { profile, refreshProfile, signOut } = useAuth();
  const [name, setName] = useState(profile?.full_name ?? '');
  const [phone, setPhone] = useState(formatArPhone(profile?.phone));
  const [saving, setSaving] = useState(false);
  const [error, setError] = useState<string | null>(null);

  async function save() {
    setError(null);
    const phoneE164 = normalizeArPhone(phone);
    if (name.trim().length < 2) return setError('Ingresá tu nombre.');
    if (!phoneE164) return setError('Celular inválido. Ej: 341 555 1234');
    setSaving(true);
    const { error: err } = await supabase.from('profiles').update({ full_name: name.trim(), phone: phoneE164 }).eq('id', profile!.id);
    setSaving(false);
    if (err) return setError(friendlyError(err.message));
    await refreshProfile();
    Alert.alert('Listo', 'Tus datos se guardaron.');
  }

  function confirmSignOut() {
    Alert.alert('Cerrar sesión', '¿Querés salir de tu cuenta?', [
      { text: 'Cancelar', style: 'cancel' },
      { text: 'Salir', style: 'destructive', onPress: signOut },
    ]);
  }

  return (
    <Screen>
      <T variant="title">Mi perfil</T>
      <Card>
        <T variant="muted">{profile?.email}</T>
        <T style={{ color: colors.gold }}>{profile ? roleLabel[profile.role] : ''}</T>
      </Card>
      <Input label="NOMBRE Y APELLIDO" value={name} onChangeText={setName} autoCapitalize="words" />
      <Input label="CELULAR (WHATSAPP)" value={phone} onChangeText={setPhone} keyboardType="phone-pad" />
      {error ? <T variant="error">{error}</T> : null}
      <Button label="Guardar cambios" onPress={save} loading={saving} />
      <Button label="Cerrar sesión" variant="danger" onPress={confirmSignOut} />
    </Screen>
  );
}
