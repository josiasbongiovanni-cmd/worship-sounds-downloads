// Días y horarios de atención (admin)
import Ionicons from '@expo/vector-icons/Ionicons';
import { useEffect, useState } from 'react';
import { Alert, Pressable, View } from 'react-native';

import { Segmented, SwitchRow, TimeSelect } from '@/components/form';
import { Button, Card, Loading, Screen, T } from '@/components/ui';
import { colors, spacing } from '@/constants/theme';
import { fetchBarbers, fetchOutsideHours, fetchWorkingHours, saveWorkingHours, type BarberRow, type HourRange } from '@/lib/admin';
import { formatDateTime, WEEKDAY_NAMES } from '@/lib/dates';
import { friendlyError } from '@/lib/supabase';
import { useAuth } from '@/providers/auth';

type Range = { start: string; end: string };
type Week = Record<number, Range[]>; // weekday -> franjas (vacío = cerrado)

const ORDER = [1, 2, 3, 4, 5, 6, 0]; // lunes a domingo

function toWeek(rows: HourRange[]): Week {
  const w: Week = { 0: [], 1: [], 2: [], 3: [], 4: [], 5: [], 6: [] };
  rows.forEach((r) => w[r.weekday].push({ start: r.start, end: r.end }));
  return w;
}

export default function HoursScreen() {
  const { profile } = useAuth();
  const [barbers, setBarbers] = useState<BarberRow[]>([]);
  const [barberId, setBarberId] = useState<string | null>(null);
  const [week, setWeek] = useState<Week | null>(null);
  const [saving, setSaving] = useState(false);
  const [error, setError] = useState<string | null>(null);

  useEffect(() => {
    fetchBarbers()
      .then((list) => {
        setBarbers(list);
        const mine = list.find((b) => b.profile_id === profile?.id) ?? list[0];
        setBarberId(mine?.id ?? null);
      })
      .catch((e) => setError(e.message));
  }, [profile?.id]);

  useEffect(() => {
    if (!barberId) return;
    setWeek(null);
    fetchWorkingHours(barberId).then((r) => setWeek(toWeek(r))).catch((e) => setError(e.message));
  }, [barberId]);

  const update = (day: number, ranges: Range[]) => setWeek((w) => ({ ...w!, [day]: ranges }));

  function copyToOpenDays(from: number) {
    Alert.alert('Copiar horario', `¿Copiar el horario del ${WEEKDAY_NAMES[from].toLowerCase()} a todos los demás días abiertos?`, [
      { text: 'Cancelar', style: 'cancel' },
      {
        text: 'Copiar',
        onPress: () =>
          setWeek((w) => {
            const next = { ...w! };
            ORDER.forEach((d) => { if (d !== from && next[d].length) next[d] = next[from].map((r) => ({ ...r })); });
            return next;
          }),
      },
    ]);
  }

  async function save() {
    if (!week || !barberId) return;
    setError(null);
    const rows: HourRange[] = [];
    for (const d of ORDER) {
      for (const r of week[d]) {
        if (r.end <= r.start) return setError(`${WEEKDAY_NAMES[d]}: la hora de cierre tiene que ser después de la de apertura.`);
        rows.push({ weekday: d, start: r.start, end: r.end });
      }
    }
    setSaving(true);
    try {
      await saveWorkingHours(barberId, rows);
      const outside = await fetchOutsideHours(barberId);
      if (outside.length) {
        Alert.alert(
          'Horario guardado — ojo',
          `Hay ${outside.length} turno(s) ya reservado(s) que quedan fuera del horario nuevo:\n\n` +
            outside.slice(0, 5).map((a) => `• ${formatDateTime(a.starts_at)}`).join('\n') +
            '\n\nNo se cancelan solos: revisalos en la Agenda y avisale al cliente.',
        );
      } else {
        Alert.alert('Listo', 'El horario se guardó. Los clientes ya ven los horarios nuevos.');
      }
    } catch (e) {
      setError(friendlyError(e instanceof Error ? e.message : String(e)));
    } finally {
      setSaving(false);
    }
  }

  if (!week) return error ? <Screen withHeader><T variant="error">{error}</T></Screen> : <Loading />;

  return (
    <Screen withHeader>
      {barbers.length > 1 ? (
        <Segmented options={barbers.map((b) => ({ label: b.display_name, value: b.id }))} value={barberId!} onChange={setBarberId} />
      ) : null}
      <T variant="muted">
        Activá los días que abrís y elegí el horario. Si cerrás al mediodía, activá "Descanso" y cargá la franja de la tarde.
      </T>

      {ORDER.map((d) => {
        const ranges = week[d];
        const open = ranges.length > 0;
        return (
          <Card key={d} highlight={open}>
            <SwitchRow
              label={WEEKDAY_NAMES[d]}
              hint={open ? ranges.map((r) => `${r.start} a ${r.end}`).join(' y ') : 'Cerrado'}
              value={open}
              onChange={(v) => update(d, v ? [{ start: '09:00', end: '13:00' }, { start: '15:00', end: '20:00' }] : [])}
            />
            {ranges.map((r, i) => (
              <View key={i} style={{ flexDirection: 'row', alignItems: 'center', gap: spacing.sm }}>
                <TimeSelect label={i === 0 ? 'Abre' : 'Vuelve'} value={r.start}
                  onChange={(v) => update(d, ranges.map((x, j) => (j === i ? { ...x, start: v } : x)))} />
                <T variant="muted">a</T>
                <TimeSelect label={i === ranges.length - 1 ? 'Cierra' : 'Descanso desde'} value={r.end}
                  onChange={(v) => update(d, ranges.map((x, j) => (j === i ? { ...x, end: v } : x)))} />
                {ranges.length > 1 ? (
                  <Pressable onPress={() => update(d, ranges.filter((_, j) => j !== i))} hitSlop={10} accessibilityLabel="Quitar franja">
                    <Ionicons name="close-circle" size={28} color={colors.danger} />
                  </Pressable>
                ) : null}
              </View>
            ))}
            {open ? (
              <View style={{ flexDirection: 'row', gap: spacing.sm, flexWrap: 'wrap' }}>
                {ranges.length === 1 ? (
                  <Pressable onPress={() => update(d, [{ start: ranges[0].start, end: '13:00' }, { start: '15:00', end: ranges[0].end > '15:00' ? ranges[0].end : '20:00' }])}>
                    <T variant="gold">+ Agregar descanso</T>
                  </Pressable>
                ) : null}
                <Pressable onPress={() => copyToOpenDays(d)}>
                  <T variant="gold">Copiar a los demás días</T>
                </Pressable>
              </View>
            ) : null}
          </Card>
        );
      })}

      {error ? <T variant="error">{error}</T> : null}
      <Button label="Guardar horario" onPress={save} loading={saving} />
    </Screen>
  );
}
