// Agenda del admin: semana + turnos del día, con cambio de estado
import Ionicons from '@expo/vector-icons/Ionicons';
import { useFocusEffect } from 'expo-router';
import { useCallback, useMemo, useState } from 'react';
import { Alert, Linking, Pressable, StyleSheet, View } from 'react-native';

import { Brand, Card, Loading, Screen, T } from '@/components/ui';
import { colors, font, radius, spacing } from '@/constants/theme';
import { fetchAgenda, fetchBarbers, fetchOutsideHours, setAppointmentStatus, whatsappLink, type AgendaItem } from '@/lib/admin';
import { statusLabel } from '@/lib/booking';
import { addDays, dayParts, dayStartIso, formatDateTime, formatDayLong, formatTime, localDay, weekStart } from '@/lib/dates';
import { friendlyError } from '@/lib/supabase';
import type { AppointmentStatus } from '@/lib/types';
import { formatArPhone, formatPrice } from '@/lib/validation';

const statusColor: Record<AppointmentStatus, string> = {
  booked: colors.gold, completed: colors.success, no_show: colors.danger, cancelled: colors.textMuted,
};

export default function AdminAgenda() {
  const today = localDay(new Date());
  const [day, setDay] = useState(today);
  const [items, setItems] = useState<AgendaItem[] | null>(null);
  const [warnings, setWarnings] = useState<{ id: string; starts_at: string }[]>([]);
  const [error, setError] = useState<string | null>(null);
  const [busyId, setBusyId] = useState<string | null>(null);

  const monday = weekStart(day);
  const weekDays = useMemo(() => Array.from({ length: 7 }, (_, i) => addDays(monday, i)), [monday]);

  const load = useCallback(async () => {
    setError(null);
    try {
      const [list, barbers] = await Promise.all([fetchAgenda(dayStartIso(monday), dayStartIso(addDays(monday, 7))), fetchBarbers()]);
      setItems(list);
      setWarnings((await Promise.all(barbers.map((b) => fetchOutsideHours(b.id)))).flat());
    } catch (e) {
      setError(friendlyError(e instanceof Error ? e.message : String(e)));
    }
  }, [monday]);

  useFocusEffect(useCallback(() => { load(); }, [load]));

  const dayItems = (items ?? []).filter((a) => localDay(a.starts_at) === day);
  const active = dayItems.filter((a) => a.status !== 'cancelled');
  const cancelled = dayItems.filter((a) => a.status === 'cancelled');
  const expected = active.reduce((sum, a) => sum + a.price_charged, 0);

  function change(a: AgendaItem, status: AppointmentStatus) {
    const name = a.client?.full_name || 'el cliente';
    const msgs: Partial<Record<AppointmentStatus, [string, string]>> = {
      completed: ['Marcar como completado', `¿${name} ya se atendió? ${a.is_free_reward ? 'Se usa su corte gratis.' : 'Suma a su tarjeta de fidelidad.'}`],
      no_show: ['No se presentó', `¿Marcar que ${name} no vino?`],
      cancelled: ['Cancelar turno', `¿Cancelar el turno de ${name}? Acordate de avisarle.`],
      booked: ['Volver a reservado', `¿Volver a poner el turno de ${name} como reservado?`],
    };
    const [title, body] = msgs[status]!;
    Alert.alert(title, body, [
      { text: 'No', style: 'cancel' },
      {
        text: 'Sí',
        style: status === 'cancelled' ? 'destructive' : 'default',
        onPress: async () => {
          setBusyId(a.id);
          try {
            await setAppointmentStatus(a.id, status);
            await load();
          } catch (e) {
            Alert.alert('No se pudo cambiar', friendlyError(e instanceof Error ? e.message : String(e)));
          } finally {
            setBusyId(null);
          }
        },
      },
    ]);
  }

  return (
    <Screen>
      <View style={styles.header}>
        <Brand size="small" />
        <View style={{ flex: 1 }}>
          <T variant="title">Agenda</T>
          <T variant="muted">{day === today ? 'Hoy · ' : ''}{formatDayLong(day)}</T>
        </View>
        <Pressable onPress={load} hitSlop={10} accessibilityLabel="Actualizar"><Ionicons name="refresh" size={26} color={colors.gold} /></Pressable>
      </View>

      {/* Semana */}
      <View style={styles.weekNav}>
        <Pressable onPress={() => setDay(addDays(day, -7))} hitSlop={10} accessibilityLabel="Semana anterior">
          <Ionicons name="chevron-back" size={26} color={colors.gold} />
        </Pressable>
        <T style={{ flex: 1, textAlign: 'center', fontWeight: '600' }}>
          Semana del {dayParts(monday).day} {dayParts(monday).month} al {dayParts(addDays(monday, 6)).day} {dayParts(addDays(monday, 6)).month}
        </T>
        {day !== today ? (
          <Pressable onPress={() => setDay(today)} style={styles.todayBtn}><T style={{ color: colors.onGold, fontWeight: '700' }}>Hoy</T></Pressable>
        ) : null}
        <Pressable onPress={() => setDay(addDays(day, 7))} hitSlop={10} accessibilityLabel="Semana siguiente">
          <Ionicons name="chevron-forward" size={26} color={colors.gold} />
        </Pressable>
      </View>
      <View style={styles.week}>
        {weekDays.map((d) => {
          const p = dayParts(d);
          const n = (items ?? []).filter((a) => localDay(a.starts_at) === d && a.status !== 'cancelled').length;
          const sel = d === day;
          return (
            <Pressable key={d} onPress={() => setDay(d)} style={[styles.dayCell, sel && styles.daySel, d === today && !sel && { borderColor: colors.gold }]}
              accessibilityRole="button" accessibilityLabel={`${formatDayLong(d)}, ${n} turnos`}>
              <T style={{ fontSize: 12, color: sel ? colors.onGold : colors.textMuted }}>{p.weekday}</T>
              <T style={{ fontSize: 20, fontWeight: '700', color: sel ? colors.onGold : colors.text }}>{p.day}</T>
              <T style={{ fontSize: 12, fontWeight: '700', color: sel ? colors.onGold : n ? colors.gold : colors.border }}>{n || '·'}</T>
            </Pressable>
          );
        })}
      </View>

      {warnings.length ? (
        <Card style={{ borderColor: colors.danger }}>
          <T style={{ color: colors.danger, fontWeight: '700' }}>⚠️ {warnings.length} turno(s) reservado(s) fuera de horario o en un bloqueo</T>
          {warnings.slice(0, 4).map((w) => <T key={w.id} variant="muted">• {formatDateTime(w.starts_at)}</T>)}
          <T variant="muted">Avisale al cliente y cancelalo o mantenelo.</T>
        </Card>
      ) : null}

      {error ? <Card><T variant="error">{error}</T></Card> : null}
      {!items && !error ? <Loading /> : null}

      {items ? (
        <T variant="subtitle">
          {active.length === 0 ? 'Sin turnos este día' : `${active.length} turno${active.length > 1 ? 's' : ''} · ${formatPrice(expected)}`}
        </T>
      ) : null}

      {active.map((a) => (
        <AgendaCard key={a.id} a={a} busy={busyId === a.id} onChange={(s) => change(a, s)} />
      ))}

      {cancelled.length ? <T variant="muted" style={{ marginTop: spacing.md }}>Cancelados</T> : null}
      {cancelled.map((a) => (
        <AgendaCard key={a.id} a={a} busy={busyId === a.id} onChange={(s) => change(a, s)} />
      ))}
    </Screen>
  );
}

function AgendaCard({ a, busy, onChange }: { a: AgendaItem; busy: boolean; onChange: (s: AppointmentStatus) => void }) {
  const wa = whatsappLink(a.client?.phone ?? null, `Hola ${a.client?.full_name?.split(' ')[0] ?? ''}! Te escribimos de Mathu Barber por tu turno del ${formatDateTime(a.starts_at)}.`);
  const isPastStart = new Date(a.starts_at).getTime() - Date.now() < 12 * 3600 * 1000;
  const actions: { label: string; status: AppointmentStatus; color: string; icon: React.ComponentProps<typeof Ionicons>['name'] }[] =
    a.status === 'booked'
      ? [
          ...(isPastStart ? [
            { label: 'Completado', status: 'completed' as const, color: colors.success, icon: 'checkmark-circle' as const },
            { label: 'No vino', status: 'no_show' as const, color: colors.danger, icon: 'close-circle' as const },
          ] : []),
          { label: 'Cancelar', status: 'cancelled', color: colors.textMuted, icon: 'trash' },
        ]
      : a.status === 'no_show'
        ? [
            { label: 'Completado', status: 'completed', color: colors.success, icon: 'checkmark-circle' },
            { label: 'Reservado', status: 'booked', color: colors.gold, icon: 'arrow-undo' },
          ]
        : a.status === 'cancelled'
          ? [{ label: 'Reactivar', status: 'booked', color: colors.gold, icon: 'arrow-undo' }]
          : [];

  return (
    <Card highlight={a.status === 'booked'} style={{ opacity: a.status === 'cancelled' ? 0.6 : 1 }}>
      <View style={{ flexDirection: 'row', gap: spacing.md }}>
        <View style={{ alignItems: 'center', minWidth: 58 }}>
          <T style={{ fontSize: font.large, fontWeight: '800', color: colors.gold }}>{formatTime(a.starts_at)}</T>
          <T variant="muted">{formatTime(a.ends_at)}</T>
        </View>
        <View style={{ flex: 1, gap: 2 }}>
          <View style={{ flexDirection: 'row', justifyContent: 'space-between', gap: spacing.sm }}>
            <T variant="subtitle" style={{ flex: 1 }} numberOfLines={1}>{a.client?.full_name || 'Cliente'}</T>
            <T style={{ color: statusColor[a.status], fontWeight: '700', fontSize: 12 }}>{statusLabel[a.status].toUpperCase()}</T>
          </View>
          <T>{a.service?.name}{a.barber ? ` · ${a.barber.display_name}` : ''}</T>
          <View style={{ flexDirection: 'row', gap: spacing.md, flexWrap: 'wrap' }}>
            <T variant="muted">{formatPrice(a.price_charged)}</T>
            {a.is_free_reward ? <T variant="gold" style={{ fontSize: 14 }}>🎉 Usa su corte gratis</T> : null}
          </View>
          {a.client?.phone ? (
            <Pressable onPress={() => wa && Linking.openURL(wa)} style={{ flexDirection: 'row', alignItems: 'center', gap: 6, paddingVertical: 4 }}
              accessibilityRole="link" accessibilityLabel={`WhatsApp ${a.client.full_name}`}>
              <Ionicons name="logo-whatsapp" size={20} color={colors.success} />
              <T style={{ color: colors.success }}>{formatArPhone(a.client.phone)}</T>
            </Pressable>
          ) : null}
          {a.status === 'cancelled' && a.cancel_reason ? <T variant="muted">Motivo: {a.cancel_reason}</T> : null}
        </View>
      </View>
      {actions.length ? (
        <View style={styles.actions}>
          {actions.map((ac) => (
            <Pressable key={ac.label} disabled={busy} onPress={() => onChange(ac.status)} accessibilityRole="button" accessibilityLabel={ac.label}
              style={({ pressed }) => [styles.action, { borderColor: ac.color }, pressed && { opacity: 0.7 }, busy && { opacity: 0.4 }]}>
              <Ionicons name={ac.icon} size={20} color={ac.color} />
              <T style={{ color: ac.color, fontWeight: '700' }}>{ac.label}</T>
            </Pressable>
          ))}
        </View>
      ) : null}
    </Card>
  );
}

const styles = StyleSheet.create({
  header: { flexDirection: 'row', alignItems: 'center', gap: spacing.md },
  weekNav: { flexDirection: 'row', alignItems: 'center', gap: spacing.sm },
  todayBtn: { backgroundColor: colors.gold, borderRadius: 999, paddingHorizontal: spacing.md, paddingVertical: 6 },
  week: { flexDirection: 'row', gap: 6 },
  dayCell: {
    flex: 1, alignItems: 'center', paddingVertical: spacing.sm, borderRadius: radius.md,
    backgroundColor: colors.surface, borderWidth: 1, borderColor: colors.border,
  },
  daySel: { backgroundColor: colors.gold, borderColor: colors.gold },
  actions: { flexDirection: 'row', gap: spacing.sm, flexWrap: 'wrap', marginTop: spacing.sm },
  action: {
    flexGrow: 1, flexDirection: 'row', alignItems: 'center', justifyContent: 'center', gap: 6,
    minHeight: 48, borderRadius: radius.md, borderWidth: 1.5, paddingHorizontal: spacing.md,
  },
});
