// Crear o editar un servicio (admin)
import { router, Stack, useLocalSearchParams } from 'expo-router';
import { useEffect, useState } from 'react';
import { Alert } from 'react-native';

import { Stepper, SwitchRow } from '@/components/form';
import { Button, Input, Loading, Screen, T } from '@/components/ui';
import { fetchAllServices, saveService } from '@/lib/admin';
import { friendlyError } from '@/lib/supabase';
import { formatDuration } from '@/lib/validation';

export default function ServiceEditor() {
  const { id } = useLocalSearchParams<{ id?: string }>();
  const [loading, setLoading] = useState(Boolean(id));
  const [saving, setSaving] = useState(false);
  const [error, setError] = useState<string | null>(null);

  const [name, setName] = useState('');
  const [description, setDescription] = useState('');
  const [price, setPrice] = useState('');
  const [duration, setDuration] = useState(60);
  const [counts, setCounts] = useState(true);
  const [rewardBase, setRewardBase] = useState(false);
  const [active, setActive] = useState(true);

  useEffect(() => {
    if (!id) return;
    fetchAllServices()
      .then((all) => {
        const s = all.find((x) => x.id === id);
        if (!s) throw new Error('No encontramos ese servicio.');
        setName(s.name);
        setDescription(s.description ?? '');
        setPrice(String(Math.round(s.price)));
        setDuration(s.duration_min);
        setCounts(s.counts_for_loyalty);
        setRewardBase(s.is_reward_base);
        setActive(s.active);
      })
      .catch((e) => setError(e.message))
      .finally(() => setLoading(false));
  }, [id]);

  async function save() {
    setError(null);
    const priceNum = Number(price.replace(/\D/g, ''));
    if (name.trim().length < 2) return setError('Poné un nombre al servicio.');
    if (!price || Number.isNaN(priceNum)) return setError('Poné un precio (solo números, ej: 12000).');
    setSaving(true);
    try {
      await saveService(id ?? null, {
        name: name.trim(),
        description: description.trim() || null,
        price: priceNum,
        duration_min: duration,
        counts_for_loyalty: counts,
        is_reward_base: rewardBase,
        active,
      });
      Alert.alert('Listo', 'El servicio se guardó.');
      router.back();
    } catch (e) {
      setError(friendlyError(e instanceof Error ? e.message : String(e)));
    } finally {
      setSaving(false);
    }
  }

  if (loading) return <Loading />;
  return (
    <Screen withHeader>
      <Stack.Screen options={{ title: id ? 'Editar servicio' : 'Nuevo servicio' }} />
      <Input label="NOMBRE" value={name} onChangeText={setName} placeholder="Ej: Corte" />
      <Input label="DESCRIPCIÓN (OPCIONAL)" value={description} onChangeText={setDescription} placeholder="Ej: Corte clásico o moderno" />
      <Input label="PRECIO ($)" value={price} onChangeText={(v) => setPrice(v.replace(/\D/g, ''))} keyboardType="number-pad" placeholder="12000" />
      <Stepper label="Duración" value={duration} onChange={setDuration} min={15} max={240} step={15} format={formatDuration} />
      <SwitchRow label="Suma a la tarjeta de fidelidad" hint="Cada vez que se completa, suma 1 sello" value={counts} onChange={setCounts} />
      <SwitchRow label="Es el corte base del corte gratis" hint="Su precio es lo que se descuenta cuando el cliente usa el gratis" value={rewardBase} onChange={setRewardBase} />
      <SwitchRow label="Activo" hint="Si lo desactivás, los clientes no lo ven (el historial se conserva)" value={active} onChange={setActive} />
      <T variant="muted">Los cambios de precio valen para los turnos nuevos. Los turnos ya reservados mantienen su precio.</T>
      {error ? <T variant="error">{error}</T> : null}
      <Button label="Guardar" onPress={save} loading={saving} />
    </Screen>
  );
}
