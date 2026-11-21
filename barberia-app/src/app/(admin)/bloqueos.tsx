// Feriados, vacaciones y bloqueos de horario (admin)
import Ionicons from '@expo/vector-icons/Ionicons';
import { useState } from 'react';
import { Alert, Pressable, View } from 'react-native';

import { Segmented, TimeSelect } from '@/components/form';
import { Button, Card, Input, Loading, Screen, T } from '@/components/ui';
import { colors, font, spacing } from '@/constants/theme';
import { addTimeOff, deleteTimeOff, fetchBarbers, fetchOutsideHours, fetchTimeOff } from '@/lib/admin';
import { useLoad } from '@/lib/data';
import { addDays, formatDateTime, formatDayLong, localDay, localIso } from '@/lib/dates';
import { friendlyError } from '@/lib/supabase';
import { useAuth } from '@/providers/auth';

function DayStepper({ label, value, onChange, min }: { label: string; value: string; onChange: (d: string) => void; min: string }) {
  const arrow = (icon: 'chevron-back' | 'chevron-forward', next: string, disabled: boolean) => (
    <Pressable onPress={() => onChange(next)} disabled={disabled} hitSlop={10} accessibilityRole="button" accessibilityLabel={icon === 'chevron-back' ? 'Día anterior' : 'Día siguiente'}
      style={{ opacity: disabled ? 0.3 : 1, padding: spacing.sm }}>
      <Ionicons name={icon} size={28} color={colors.gold} />
    </Pressable>
  );
  return (
    <View style={{ gap: spacing.xs }}>
      <T variant="muted" style={{ fontWeight: '600' }}>{label}</T>
      <Card style={{ flexDirection: 'row', alignItems: 'center', paddingVertical: spacing.sm }}>
        {arrow('chevron-back', addDays(value, -1), value <= min)}
        <T style={{ flex: 1, textAlign: 'center', fontSize: font.body, fontWeight: '700' }}>{formatDayLong(value)}</T>
        {arrow('chevron-forward', addDays(value, 1), false)}
      </Card>
    </View>
  );
}

const REASONS = ['Feriado', 'Vacaciones', 'Trámite', 'Capacitación'];

export default function TimeOffScreen() {
  const { profile } = useAuth();
  const today = localDay(new Date());
  const [kind, setKind] = useState<'full' | 'hours'>('full');
  const [from, setFrom] = useState(today);
  const [to, setTo] = useState(today);
  const [startTime, setStartTime] = useState('09:00');
  const [endTime, setEndTime] = useState('13:00');
  const [reason, setReason] = useState('Feriado');
  const [saving, setSaving] = useState(false);
  const [error, setError] = useState<string | null>(null);

  const { data, loading, reload } = useLoad(fetchTimeOff);

  async function add() {
    setError(null);
    let startsAt: string;
    let endsAt: string;
    if (kind === 'full') {
      if (to < from) return setError('El día "hasta" no puede ser antes del día "desde".');
      startsAt = localIso(from, '00:00');
      endsAt = localIso(addDays(to, 1), '00:00');
    } else {
      if (endTime <= startTime) return setError('La hora "hasta" tiene que ser después de la hora "desde".');
      startsAt = localIso(from, startTime);
      endsAt = localIso(from, endTime);
    }
    setSaving(true);
    try {
      await addTimeOff(startsAt, endsAt, reason.trim(), null, profile!.id);
      await reload();
      const barbers = await fetchBarbers();
      const affected = (await Promise.all(barbers.map((b) => fetchOutsideHours(b.id)))).flat();
      if (affected.length) {
        Alert.alert('Bloqueo guardado — ojo',
          `Hay ${affected.length} turno(s) ya reservado(s) en ese período. No se cancelan solos: revisalos en la Agenda y avisale al cliente.`);
      } else {
        Alert.alert('Listo', 'Bloqueo guardado. Esos horarios ya no se pueden reservar.');
      }
    } catch (e) {
      setError(friendlyError(e instanceof Error ? e.message : String(e)));
    } finally {
      setSaving(false);
    }
  }

  function remove(id: string, text: string) {
    Alert.alert('Quitar bloqueo', `¿Quitar "${text}"? Esos horarios vuelven a estar disponibles.`, [
      { text: 'Cancelar', style: 'cancel' },
      { text: 'Quitar', style: 'destructive', onPress: async () => {
        try { await deleteTimeOff(id); await reload(); } catch (e) { Alert.alert('Error', friendlyError((e as Error).message)); }
      } },
    ]);
  }

  return (
    <Screen withHeader>
      <T variant="subtitle">Agregar bloqueo</T>
      <Segmented options={[{ label: 'Día(s) completo(s)', value: 'full' }, { label: 'Unas horas', value: 'hours' }]} value={kind} onChange={setKind} />

      <DayStepper label={kind === 'full' ? 'DESDE' : 'DÍA'} value={from} min={today}
        onChange={(d) => { setFrom(d); if (to < d) setTo(d); }} />
      {kind === 'full' ? (
        <DayStepper label="HASTA (INCLUSIVE)" value={to} min={from} onChange={setTo} />
      ) : (
        <View style={{ flexDirection: 'row', alignItems: 'center', gap: spacing.sm }}>
          <TimeSelect label="Desde" value={startTime} onChange={setStartTime} />
          <T variant="muted">a</T>
          <TimeSelect label="Hasta" value={endTime} onChange={setEndTime} />
        </View>
      )}

      <View style={{ flexDirection: 'row', flexWrap: 'wrap', gap: spacing.sm }}>
        {REASONS.map((r) => (
          <Pressable key={r} onPress={() => setReason(r)}
            style={{ paddingHorizontal: spacing.md, paddingVertical: spacing.sm, borderRadius: 999, borderWidth: 1,
              borderColor: reason === r ? colors.gold : colors.border, backgroundColor: reason === r ? colors.gold : 'transparent' }}>
            <T style={{ color: reason === r ? colors.onGold : colors.text, fontWeight: '600' }}>{r}</T>
          </Pressable>
        ))}
      </View>
      <Input label="MOTIVO" value={reason} onChangeText={setReason} placeholder="Ej: Feriado" />

      {error ? <T variant="error">{error}</T> : null}
      <Button label="Guardar bloqueo" onPress={add} loading={saving} />

      <T variant="subtitle" style={{ marginTop: spacing.lg }}>Próximos bloqueos</T>
      {loading && !data ? <Loading /> : null}
      {data?.length === 0 ? <T variant="muted">No hay bloqueos cargados.</T> : null}
      {data?.map((t) => {
        const fullDays = formatDateTime(t.starts_at).endsWith('00:00') && formatDateTime(t.ends_at).endsWith('00:00');
        const text = fullDays
          ? (localDay(t.starts_at) === addDays(localDay(t.ends_at), -1)
              ? formatDayLong(localDay(t.starts_at))
              : `${formatDayLong(localDay(t.starts_at))} al ${formatDayLong(addDays(localDay(t.ends_at), -1))}`)
          : `${formatDateTime(t.starts_at)} a ${formatDateTime(t.ends_at).split(' · ')[1]}`;
        return (
          <Card key={t.id} style={{ flexDirection: 'row', alignItems: 'center', gap: spacing.md }}>
            <View style={{ flex: 1, gap: 2 }}>
              <T style={{ fontWeight: '700' }}>{text}</T>
              <T variant="muted">{t.reason ?? 'Sin motivo'}{fullDays ? ' · todo el día' : ''}</T>
            </View>
            <Pressable onPress={() => remove(t.id, text)} hitSlop={10} accessibilityLabel="Quitar bloqueo">
              <Ionicons name="trash-outline" size={26} color={colors.danger} />
            </Pressable>
          </Card>
        );
      })}
    </Screen>
  );
}
