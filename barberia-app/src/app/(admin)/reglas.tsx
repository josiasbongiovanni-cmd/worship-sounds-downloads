// Reglas de reserva (admin)
import { useEffect, useState } from 'react';
import { Alert } from 'react-native';

import { Segmented, Stepper } from '@/components/form';
import { Button, Loading, Screen, T } from '@/components/ui';
import { fetchRules, saveRules, type Rules } from '@/lib/admin';
import { friendlyError } from '@/lib/supabase';
import { formatDuration } from '@/lib/validation';

export default function RulesScreen() {
  const [rules, setRules] = useState<Rules | null>(null);
  const [saving, setSaving] = useState(false);
  const [error, setError] = useState<string | null>(null);

  useEffect(() => {
    fetchRules().then(setRules).catch((e) => setError(e.message));
  }, []);

  const set = <K extends keyof Rules>(k: K, v: Rules[K]) => setRules((r) => ({ ...r!, [k]: v }));

  async function save() {
    setSaving(true);
    setError(null);
    try {
      await saveRules(rules!);
      Alert.alert('Listo', 'Las reglas se guardaron.');
    } catch (e) {
      setError(friendlyError(e instanceof Error ? e.message : String(e)));
    } finally {
      setSaving(false);
    }
  }

  if (!rules) return error ? <Screen withHeader><T variant="error">{error}</T></Screen> : <Loading />;
  return (
    <Screen withHeader>
      <T variant="subtitle">Turnos cada</T>
      <Segmented
        options={[{ label: '15 min', value: 15 }, { label: '30 min', value: 30 }, { label: '1 hora', value: 60 }]}
        value={rules.slot_interval_min}
        onChange={(v) => set('slot_interval_min', v)}
      />
      <Stepper label="Cancelar o reprogramar hasta" hint="Horas antes del turno" value={rules.cancel_limit_hours}
        onChange={(v) => set('cancel_limit_hours', v)} min={0} max={48} format={(v) => `${v} h`} />
      <Stepper label="Anticipación mínima" hint="Para reservar un turno de hoy" value={rules.min_notice_min}
        onChange={(v) => set('min_notice_min', v)} min={0} max={1440} step={30} format={(v) => (v === 0 ? 'nada' : formatDuration(v))} />
      <Stepper label="Reservar hasta" hint="Días hacia adelante" value={rules.booking_window_days}
        onChange={(v) => set('booking_window_days', v)} min={1} max={90} format={(v) => `${v} días`} />
      <Stepper label="Turnos por cliente" hint="Máximo de turnos futuros reservados a la vez" value={rules.max_active_bookings}
        onChange={(v) => set('max_active_bookings', v)} min={1} max={10} />
      {error ? <T variant="error">{error}</T> : null}
      <Button label="Guardar reglas" onPress={save} loading={saving} />
    </Screen>
  );
}
