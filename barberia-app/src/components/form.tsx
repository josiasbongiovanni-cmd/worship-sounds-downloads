// Controles de formulario para el panel admin (grandes y fáciles de tocar)
import Ionicons from '@expo/vector-icons/Ionicons';
import { useState } from 'react';
import { FlatList, Modal, Pressable, StyleSheet, Switch, View } from 'react-native';
import { SafeAreaView } from 'react-native-safe-area-context';

import { colors, font, radius, spacing, touch } from '@/constants/theme';

import { Button, T } from './ui';

/** Horas cada 30 minutos, de 06:00 a 23:30 */
export const TIME_OPTIONS = Array.from({ length: 36 }, (_, i) => {
  const m = 360 + i * 30;
  return `${String(Math.floor(m / 60)).padStart(2, '0')}:${m % 60 === 0 ? '00' : '30'}`;
});

export function TimeSelect({ label, value, onChange }: { label: string; value: string; onChange: (v: string) => void }) {
  const [open, setOpen] = useState(false);
  return (
    <>
      <Pressable onPress={() => setOpen(true)} style={styles.select} accessibilityRole="button" accessibilityLabel={`${label}: ${value}`}>
        <T variant="muted" style={{ fontSize: 12 }}>{label}</T>
        <T style={{ fontSize: font.large, fontWeight: '700' }}>{value}</T>
      </Pressable>
      <Modal visible={open} animationType="slide" transparent onRequestClose={() => setOpen(false)}>
        <View style={styles.backdrop}>
          <SafeAreaView style={styles.sheet} edges={['bottom']}>
            <T variant="subtitle" style={{ textAlign: 'center', marginBottom: spacing.sm }}>{label}</T>
            <FlatList
              data={TIME_OPTIONS}
              numColumns={4}
              keyExtractor={(t) => t}
              initialScrollIndex={Math.max(0, Math.floor(TIME_OPTIONS.indexOf(value) / 4) - 2)}
              getItemLayout={(_, i) => ({ length: 60, offset: 60 * i, index: i })}
              columnWrapperStyle={{ gap: spacing.sm }}
              contentContainerStyle={{ gap: spacing.sm }}
              renderItem={({ item }) => (
                <Pressable onPress={() => { onChange(item); setOpen(false); }}
                  style={[styles.timeCell, item === value && { backgroundColor: colors.gold, borderColor: colors.gold }]}>
                  <T style={{ fontWeight: '700', color: item === value ? colors.onGold : colors.text }}>{item}</T>
                </Pressable>
              )}
            />
            <Button label="Cerrar" variant="ghost" onPress={() => setOpen(false)} />
          </SafeAreaView>
        </View>
      </Modal>
    </>
  );
}

export function SwitchRow({ label, hint, value, onChange }: { label: string; hint?: string; value: boolean; onChange: (v: boolean) => void }) {
  return (
    <Pressable onPress={() => onChange(!value)} style={styles.switchRow} accessibilityRole="switch" accessibilityState={{ checked: value }}>
      <View style={{ flex: 1, gap: 2 }}>
        <T style={{ fontSize: font.body, fontWeight: '600' }}>{label}</T>
        {hint ? <T variant="muted">{hint}</T> : null}
      </View>
      <Switch value={value} onValueChange={onChange} trackColor={{ true: colors.gold, false: colors.border }} thumbColor={colors.text} />
    </Pressable>
  );
}

export function Stepper({ label, hint, value, onChange, min, max, step = 1, format }: {
  label: string; hint?: string; value: number; onChange: (v: number) => void;
  min: number; max: number; step?: number; format?: (v: number) => string;
}) {
  const btn = (icon: 'remove' | 'add', next: number, disabled: boolean) => (
    <Pressable onPress={() => onChange(next)} disabled={disabled} accessibilityRole="button"
      accessibilityLabel={icon === 'add' ? `Aumentar ${label}` : `Disminuir ${label}`}
      style={[styles.stepBtn, disabled && { opacity: 0.3 }]}>
      <Ionicons name={icon} size={26} color={colors.gold} />
    </Pressable>
  );
  return (
    <View style={styles.switchRow}>
      <View style={{ flex: 1, gap: 2 }}>
        <T style={{ fontSize: font.body, fontWeight: '600' }}>{label}</T>
        {hint ? <T variant="muted">{hint}</T> : null}
      </View>
      <View style={{ flexDirection: 'row', alignItems: 'center', gap: spacing.sm }}>
        {btn('remove', Math.max(min, value - step), value <= min)}
        <T style={{ minWidth: 72, textAlign: 'center', fontSize: font.large, fontWeight: '700' }}>{format ? format(value) : value}</T>
        {btn('add', Math.min(max, value + step), value >= max)}
      </View>
    </View>
  );
}

export function Segmented<V extends string | number>({ options, value, onChange }: {
  options: { label: string; value: V }[]; value: V; onChange: (v: V) => void;
}) {
  return (
    <View style={styles.segmented}>
      {options.map((o) => {
        const on = o.value === value;
        return (
          <Pressable key={String(o.value)} onPress={() => onChange(o.value)} style={[styles.segment, on && { backgroundColor: colors.gold }]}
            accessibilityRole="button" accessibilityState={{ selected: on }}>
            <T style={{ fontWeight: '700', color: on ? colors.onGold : colors.text }}>{o.label}</T>
          </Pressable>
        );
      })}
    </View>
  );
}

const styles = StyleSheet.create({
  select: {
    flex: 1, minHeight: touch.minHeight, borderRadius: radius.md, borderWidth: 1, borderColor: colors.goldDark,
    backgroundColor: colors.surfaceAlt, paddingHorizontal: spacing.md, paddingVertical: spacing.xs, justifyContent: 'center',
  },
  backdrop: { flex: 1, justifyContent: 'flex-end', backgroundColor: 'rgba(0,0,0,0.6)' },
  sheet: { maxHeight: '70%', backgroundColor: colors.surface, borderTopLeftRadius: radius.lg, borderTopRightRadius: radius.lg, padding: spacing.lg },
  timeCell: {
    flex: 1, height: 52, alignItems: 'center', justifyContent: 'center', borderRadius: radius.md,
    borderWidth: 1, borderColor: colors.border, backgroundColor: colors.surfaceAlt,
  },
  switchRow: {
    flexDirection: 'row', alignItems: 'center', gap: spacing.md, minHeight: touch.minHeight,
    backgroundColor: colors.surface, borderRadius: radius.md, borderWidth: 1, borderColor: colors.border, padding: spacing.md,
  },
  stepBtn: { width: 48, height: 48, borderRadius: 24, borderWidth: 1, borderColor: colors.goldDark, alignItems: 'center', justifyContent: 'center' },
  segmented: { flexDirection: 'row', backgroundColor: colors.surface, borderRadius: radius.md, borderWidth: 1, borderColor: colors.border, padding: 4, gap: 4 },
  segment: { flex: 1, minHeight: 48, borderRadius: radius.sm, alignItems: 'center', justifyContent: 'center' },
});
