// Reservar (o reprogramar) un turno, paso a paso:
// servicio -> barbero (si hay más de uno) -> día -> horario -> confirmar
import Ionicons from '@expo/vector-icons/Ionicons';
import { router, Stack, useLocalSearchParams } from 'expo-router';
import { useCallback, useEffect, useState } from 'react';
import { Pressable, StyleSheet, View } from 'react-native';

import { Button, Card, Loading, Screen, T } from '@/components/ui';
import { colors, font, radius, spacing, touch } from '@/constants/theme';
import {
  bookAppointment,
  fetchAppointment,
  fetchAvailableDays,
  fetchBarbersForService,
  fetchQuote,
  fetchSlots,
  rescheduleAppointment,
  type AvailableDay,
  type Barber,
  type Quote,
  type Slot,
} from '@/lib/booking';
import { fetchServices } from '@/lib/data';
import { dayParts, formatDateTime, formatDayLong, relativeDay } from '@/lib/dates';
import { friendlyError } from '@/lib/supabase';
import type { Service } from '@/lib/types';
import { formatDuration, formatPrice } from '@/lib/validation';

type Step = 'service' | 'barber' | 'day' | 'slot' | 'confirm' | 'done';

export default function BookingScreen() {
  const { reprogramar } = useLocalSearchParams<{ reprogramar?: string }>();
  const isReschedule = Boolean(reprogramar);

  const [step, setStep] = useState<Step>('service');
  const [busy, setBusy] = useState(true);
  const [error, setError] = useState<string | null>(null);

  const [services, setServices] = useState<Service[]>([]);
  const [barbers, setBarbers] = useState<Barber[]>([]);
  const [days, setDays] = useState<AvailableDay[]>([]);
  const [slots, setSlots] = useState<Slot[]>([]);

  const [service, setService] = useState<Service | null>(null);
  const [barber, setBarber] = useState<Barber | null>(null);
  const [day, setDay] = useState<string | null>(null);
  const [slot, setSlot] = useState<Slot | null>(null);
  const [quote, setQuote] = useState<Quote | null>(null);
  const [oldStart, setOldStart] = useState<string | null>(null);

  const run = useCallback(async (fn: () => Promise<void>) => {
    setBusy(true);
    setError(null);
    try {
      await fn();
    } catch (e) {
      setError(friendlyError(e instanceof Error ? e.message : String(e)));
    } finally {
      setBusy(false);
    }
  }, []);

  const loadDays = useCallback(
    (s: Service, b: Barber) =>
      run(async () => {
        setDays(await fetchAvailableDays(s.id, b.id));
        setStep('day');
      }),
    [run],
  );

  // Inicio: cargar servicios (o el turno a reprogramar)
  useEffect(() => {
    run(async () => {
      const all = await fetchServices();
      setServices(all);
      if (reprogramar) {
        const appt = await fetchAppointment(reprogramar);
        if (!appt) throw new Error('No encontramos ese turno.');
        const s = all.find((x) => x.id === appt.service_id);
        if (!s) throw new Error('Ese servicio ya no está disponible. Cancelá el turno y reservá uno nuevo.');
        const b: Barber = { id: appt.barber_id, display_name: appt.barber?.display_name ?? '', photo_url: null };
        setService(s);
        setBarber(b);
        setOldStart(appt.starts_at);
        setDays(await fetchAvailableDays(s.id, b.id));
        setStep('day');
      }
    });
  }, [reprogramar, run]);

  function chooseService(s: Service) {
    setService(s);
    run(async () => {
      const list = await fetchBarbersForService(s.id);
      setBarbers(list);
      if (list.length === 0) throw new Error('No hay barberos disponibles para este servicio.');
      if (list.length === 1) {
        setBarber(list[0]);
        setDays(await fetchAvailableDays(s.id, list[0].id));
        setStep('day');
      } else {
        setStep('barber');
      }
    });
  }

  function chooseBarber(b: Barber) {
    setBarber(b);
    loadDays(service!, b);
  }

  function chooseDay(d: string) {
    setDay(d);
    run(async () => {
      setSlots(await fetchSlots(service!.id, barber!.id, d));
      setStep('slot');
    });
  }

  function chooseSlot(sl: Slot) {
    setSlot(sl);
    run(async () => {
      if (!isReschedule) setQuote(await fetchQuote(service!.id));
      setStep('confirm');
    });
  }

  function confirm() {
    run(async () => {
      try {
        if (isReschedule) await rescheduleAppointment(reprogramar!, slot!.starts_at);
        else await bookAppointment(service!.id, barber!.id, slot!.starts_at);
        setStep('done');
      } catch (e) {
        // Si alguien ganó el horario, volvemos a mostrar los horarios actualizados
        const msg = e instanceof Error ? e.message : '';
        if (msg.includes('ya no está disponible')) {
          setSlots(await fetchSlots(service!.id, barber!.id, day!));
          setStep('slot');
        }
        throw e;
      }
    });
  }

  function back() {
    setError(null);
    const multiBarber = barbers.length > 1;
    if (step === 'confirm') setStep('slot');
    else if (step === 'slot') setStep('day');
    else if (step === 'day' && !isReschedule) setStep(multiBarber ? 'barber' : 'service');
    else if (step === 'barber') setStep('service');
    else router.back();
  }

  const title = isReschedule ? 'Reprogramar turno' : 'Reservar turno';
  const stepNumber = { service: 1, barber: 2, day: 2, slot: 3, confirm: 4, done: 4 }[step];

  if (busy && step === 'service' && !services.length && !error) return <Loading />;

  return (
    <Screen withHeader>
      <Stack.Screen options={{ title }} />

      {step !== 'done' && (
        <View style={styles.topRow}>
          {(step !== 'service' && !(isReschedule && step === 'day')) ? (
            <Pressable onPress={back} hitSlop={12} style={styles.backBtn} accessibilityRole="button" accessibilityLabel="Atrás">
              <Ionicons name="chevron-back" size={22} color={colors.gold} />
              <T variant="gold">Atrás</T>
            </Pressable>
          ) : <View />}
          {!isReschedule && <T variant="muted">Paso {stepNumber} de 4</T>}
        </View>
      )}

      {isReschedule && oldStart && step !== 'done' ? (
        <Card>
          <T variant="muted">Turno actual</T>
          <T>{service?.name} · {formatDateTime(oldStart)}</T>
        </Card>
      ) : null}

      {error ? (
        <Card style={{ borderColor: colors.danger }}>
          <T variant="error" style={{ fontSize: font.body }}>{error}</T>
        </Card>
      ) : null}

      {/* PASO 1: servicio */}
      {step === 'service' && (
        <>
          <T variant="title">¿Qué te hacés?</T>
          {services.map((s) => (
            <Option key={s.id} onPress={() => chooseService(s)} disabled={busy}>
              <View style={{ flex: 1, gap: 2 }}>
                <T variant="subtitle">{s.name}</T>
                <T variant="muted">{formatDuration(s.duration_min)}{s.counts_for_loyalty ? '  ·  suma a tu tarjeta ✂' : ''}</T>
              </View>
              <T style={styles.price}>{formatPrice(s.price)}</T>
            </Option>
          ))}
        </>
      )}

      {/* PASO 2: barbero (solo si hay más de uno) */}
      {step === 'barber' && (
        <>
          <T variant="title">¿Con quién?</T>
          {barbers.map((b) => (
            <Option key={b.id} onPress={() => chooseBarber(b)} disabled={busy}>
              <Ionicons name="person-circle" size={40} color={colors.gold} />
              <T variant="subtitle" style={{ flex: 1 }}>{b.display_name}</T>
            </Option>
          ))}
        </>
      )}

      {/* PASO 3: día */}
      {step === 'day' && (
        <>
          <T variant="title">¿Qué día?</T>
          <T variant="muted">{service?.name}{barbers.length > 1 ? ` con ${barber?.display_name}` : ''}</T>
          {busy ? <Loading /> : days.length === 0 ? (
            <Card><T>No hay horarios libres en los próximos días. Probá más adelante o escribinos por WhatsApp.</T></Card>
          ) : (
            <View style={styles.grid}>
              {days.map((d) => {
                const p = dayParts(d.day);
                const rel = relativeDay(d.day);
                return (
                  <Pressable key={d.day} onPress={() => chooseDay(d.day)} style={({ pressed }) => [styles.dayCell, pressed && styles.pressed]}
                    accessibilityRole="button" accessibilityLabel={formatDayLong(d.day)}>
                    <T variant="muted" style={{ color: rel ? colors.gold : colors.textMuted }}>{rel ?? p.weekday}</T>
                    <T style={{ fontSize: 26, fontWeight: '700' }}>{p.day}</T>
                    <T variant="muted">{p.month}</T>
                  </Pressable>
                );
              })}
            </View>
          )}
        </>
      )}

      {/* PASO 4: horario */}
      {step === 'slot' && day && (
        <>
          <T variant="title">¿A qué hora?</T>
          <T variant="gold">{formatDayLong(day)}</T>
          {busy ? <Loading /> : slots.length === 0 ? (
            <Card><T>Ya no quedan horarios este día. Elegí otro día.</T></Card>
          ) : (
            <View style={styles.grid}>
              {slots.map((sl) => (
                <Pressable key={sl.starts_at} onPress={() => chooseSlot(sl)} style={({ pressed }) => [styles.slotCell, pressed && styles.pressed]}
                  accessibilityRole="button" accessibilityLabel={`${sl.label} horas`}>
                  <T style={{ fontSize: font.large, fontWeight: '700' }}>{sl.label}</T>
                </Pressable>
              ))}
            </View>
          )}
        </>
      )}

      {/* PASO 5: confirmar */}
      {step === 'confirm' && slot && (
        <>
          <T variant="title">{isReschedule ? 'Confirmá el cambio' : 'Confirmá tu turno'}</T>
          {quote?.is_free_reward ? (
            <Card highlight style={{ backgroundColor: '#211B0E' }}>
              <T variant="gold" style={{ fontSize: font.large }}>🎉 ¡Usás tu corte GRATIS!</T>
              <T variant="muted">
                {quote.discount >= quote.list_price
                  ? 'Este turno no tiene costo.'
                  : `Se descuenta el valor del corte (${formatPrice(quote.discount)}). Pagás solo la diferencia.`}
              </T>
            </Card>
          ) : null}
          <Card>
            <Row icon="cut" text={service!.name} />
            {barbers.length > 1 || isReschedule ? <Row icon="person" text={barber!.display_name} /> : null}
            <Row icon="calendar" text={formatDayLong(day!)} />
            <Row icon="time" text={`${slot.label} hs · ${formatDuration(service!.duration_min)}`} />
            {!isReschedule && quote ? (
              <View style={styles.totalRow}>
                <T variant="subtitle">Total</T>
                <View style={{ alignItems: 'flex-end' }}>
                  {quote.discount > 0 ? <T variant="muted" style={{ textDecorationLine: 'line-through' }}>{formatPrice(quote.list_price)}</T> : null}
                  <T style={styles.price}>{formatPrice(quote.list_price - quote.discount)}</T>
                </View>
              </View>
            ) : null}
          </Card>
          <T variant="muted">Podés cancelar o reprogramar hasta 2 horas antes desde "Mis turnos".</T>
          <Button label={isReschedule ? 'Confirmar cambio' : 'Confirmar turno'} onPress={confirm} loading={busy} />
        </>
      )}

      {/* LISTO */}
      {step === 'done' && slot && (
        <View style={{ alignItems: 'center', gap: spacing.md, paddingTop: spacing.xl }}>
          <View style={styles.doneIcon}>
            <Ionicons name="checkmark" size={56} color={colors.onGold} />
          </View>
          <T variant="title" style={{ textAlign: 'center' }}>{isReschedule ? '¡Turno reprogramado!' : '¡Turno reservado!'}</T>
          <T style={{ textAlign: 'center', fontSize: font.large }}>{service?.name}</T>
          <T variant="gold" style={{ textAlign: 'center', fontSize: font.large }}>{formatDayLong(day!)} · {slot.label} hs</T>
          <T variant="muted" style={{ textAlign: 'center' }}>Te esperamos en Mathu Barber 💈</T>
          <View style={{ alignSelf: 'stretch', gap: spacing.sm, marginTop: spacing.lg }}>
            <Button label="Ver mis turnos" onPress={() => router.dismissTo('/turnos')} />
            <Button label="Volver al inicio" variant="outline" onPress={() => router.back()} />
          </View>
        </View>
      )}
    </Screen>
  );
}

function Option({ children, onPress, disabled }: { children: React.ReactNode; onPress: () => void; disabled?: boolean }) {
  return (
    <Pressable onPress={onPress} disabled={disabled} accessibilityRole="button"
      style={({ pressed }) => [styles.option, pressed && styles.pressed, disabled && { opacity: 0.5 }]}>
      {children}
      <Ionicons name="chevron-forward" size={22} color={colors.textMuted} />
    </Pressable>
  );
}

function Row({ icon, text }: { icon: React.ComponentProps<typeof Ionicons>['name']; text: string }) {
  return (
    <View style={{ flexDirection: 'row', alignItems: 'center', gap: spacing.md, paddingVertical: 4 }}>
      <Ionicons name={icon} size={22} color={colors.gold} />
      <T style={{ fontSize: font.large, flex: 1 }}>{text}</T>
    </View>
  );
}

const styles = StyleSheet.create({
  topRow: { flexDirection: 'row', justifyContent: 'space-between', alignItems: 'center' },
  backBtn: { flexDirection: 'row', alignItems: 'center', gap: 2, minHeight: 44 },
  option: {
    flexDirection: 'row', alignItems: 'center', gap: spacing.md, minHeight: 84,
    backgroundColor: colors.surface, borderRadius: radius.lg, borderWidth: 1, borderColor: colors.border, padding: spacing.lg,
  },
  pressed: { borderColor: colors.gold, opacity: 0.85 },
  price: { color: colors.gold, fontSize: 22, fontWeight: '700' },
  grid: { flexDirection: 'row', flexWrap: 'wrap', gap: spacing.sm },
  dayCell: {
    width: '23%', flexGrow: 1, maxWidth: '25%', alignItems: 'center', paddingVertical: spacing.md,
    backgroundColor: colors.surface, borderRadius: radius.md, borderWidth: 1, borderColor: colors.border,
  },
  slotCell: {
    width: '31%', flexGrow: 1, maxWidth: '33%', minHeight: touch.minHeight, alignItems: 'center', justifyContent: 'center',
    backgroundColor: colors.surface, borderRadius: radius.md, borderWidth: 1, borderColor: colors.goldDark,
  },
  totalRow: { flexDirection: 'row', justifyContent: 'space-between', alignItems: 'center', borderTopWidth: 1, borderTopColor: colors.border, paddingTop: spacing.md, marginTop: spacing.sm },
  doneIcon: { width: 100, height: 100, borderRadius: 50, backgroundColor: colors.gold, alignItems: 'center', justifyContent: 'center' },
});
