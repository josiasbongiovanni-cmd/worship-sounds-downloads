// Componentes visuales reutilizables con el estilo Mathu Barber
import { forwardRef, type ReactNode } from 'react';
import {
  ActivityIndicator,
  Image,
  KeyboardAvoidingView,
  Platform,
  Pressable,
  ScrollView,
  StyleSheet,
  Text,
  TextInput,
  View,
  type TextInputInstance,
  type TextInputProps,
  type TextProps,
  type ViewStyle,
} from 'react-native';
import { SafeAreaView } from 'react-native-safe-area-context';

import { colors, font, radius, spacing, touch } from '@/constants/theme';

export function Screen({ children, scroll = true }: { children: ReactNode; scroll?: boolean }) {
  return (
    <SafeAreaView style={styles.safe} edges={['top', 'left', 'right']}>
      <KeyboardAvoidingView style={{ flex: 1 }} behavior={Platform.OS === 'ios' ? 'padding' : undefined}>
        {scroll ? (
          <ScrollView contentContainerStyle={styles.content} keyboardShouldPersistTaps="handled">
            {children}
          </ScrollView>
        ) : (
          <View style={[styles.content, { flex: 1 }]}>{children}</View>
        )}
      </KeyboardAvoidingView>
    </SafeAreaView>
  );
}

type Variant = 'title' | 'subtitle' | 'body' | 'muted' | 'gold' | 'error';

export function T({ variant = 'body', style, ...props }: TextProps & { variant?: Variant }) {
  return <Text style={[styles.text, textVariants[variant], style]} {...props} />;
}

export function Button({
  label,
  onPress,
  variant = 'primary',
  loading = false,
  disabled = false,
  icon,
}: {
  label: string;
  onPress?: () => void;
  variant?: 'primary' | 'outline' | 'ghost' | 'danger';
  loading?: boolean;
  disabled?: boolean;
  icon?: ReactNode;
}) {
  const isDisabled = disabled || loading;
  return (
    <Pressable
      accessibilityRole="button"
      accessibilityLabel={label}
      accessibilityState={{ disabled: isDisabled, busy: loading }}
      onPress={onPress}
      disabled={isDisabled}
      style={({ pressed }) => [
        styles.button,
        buttonVariants[variant],
        pressed && { opacity: 0.8, transform: [{ scale: 0.99 }] },
        isDisabled && { opacity: 0.45 },
      ]}>
      {loading ? (
        <ActivityIndicator color={variant === 'primary' ? colors.onGold : colors.gold} />
      ) : (
        <View style={styles.buttonRow}>
          {icon}
          <Text style={[styles.buttonText, variant === 'primary' ? { color: colors.onGold } : variant === 'danger' ? { color: colors.danger } : { color: colors.gold }]}>
            {label}
          </Text>
        </View>
      )}
    </Pressable>
  );
}

export const Input = forwardRef<TextInputInstance, TextInputProps & { label: string; hint?: string; error?: string | null }>(
  function Input({ label, hint, error, style, ...props }, ref) {
    return (
      <View style={{ gap: spacing.xs }}>
        <T style={styles.label}>{label}</T>
        <TextInput
          ref={ref}
          placeholderTextColor={colors.textMuted}
          selectionColor={colors.gold}
          style={[styles.input, error ? { borderColor: colors.danger } : null, style]}
          {...props}
        />
        {error ? <T variant="error">{error}</T> : hint ? <T variant="muted">{hint}</T> : null}
      </View>
    );
  },
);

export function Card({ children, style, highlight }: { children: ReactNode; style?: ViewStyle; highlight?: boolean }) {
  return <View style={[styles.card, highlight && { borderColor: colors.gold }, style]}>{children}</View>;
}

export function Loading({ message }: { message?: string }) {
  return (
    <View style={styles.center}>
      <ActivityIndicator size="large" color={colors.gold} />
      {message ? <T variant="muted">{message}</T> : null}
    </View>
  );
}

const logo = require('@/assets/images/logo.png');
const LOGO_RATIO = 705 / 825; // ancho / alto del logo

/** Logo de Mathu Barber */
export function Brand({ size = 'large' }: { size?: 'large' | 'small' }) {
  const height = size === 'large' ? 230 : 110;
  return (
    <View style={{ alignItems: 'center' }}>
      <Image
        source={logo}
        style={{ height, width: height * LOGO_RATIO }}
        resizeMode="contain"
        accessibilityLabel="Mathu Barber"
      />
    </View>
  );
}

const textVariants = StyleSheet.create({
  title: { fontSize: font.title, fontWeight: '700' },
  subtitle: { fontSize: font.large, fontWeight: '600' },
  body: { fontSize: font.body },
  muted: { fontSize: font.small, color: colors.textMuted },
  gold: { fontSize: font.body, color: colors.gold, fontWeight: '600' },
  error: { fontSize: font.small, color: colors.danger },
});

const buttonVariants = StyleSheet.create({
  primary: { backgroundColor: colors.gold, borderColor: colors.gold },
  outline: { backgroundColor: 'transparent', borderColor: colors.gold },
  ghost: { backgroundColor: 'transparent', borderColor: 'transparent' },
  danger: { backgroundColor: 'transparent', borderColor: colors.danger },
});

const styles = StyleSheet.create({
  safe: { flex: 1, backgroundColor: colors.background },
  content: { padding: spacing.lg, gap: spacing.md, paddingBottom: spacing.xxl },
  text: { color: colors.text },
  label: { fontSize: font.small, color: colors.textMuted, fontWeight: '600', letterSpacing: 0.5 },
  input: {
    minHeight: touch.minHeight,
    backgroundColor: colors.surface,
    borderWidth: 1,
    borderColor: colors.border,
    borderRadius: radius.md,
    paddingHorizontal: spacing.md,
    fontSize: font.body,
    color: colors.text,
  },
  button: {
    minHeight: touch.minHeight,
    borderRadius: radius.md,
    borderWidth: 1.5,
    alignItems: 'center',
    justifyContent: 'center',
    paddingHorizontal: spacing.lg,
  },
  buttonRow: { flexDirection: 'row', alignItems: 'center', gap: spacing.sm },
  buttonText: { fontSize: font.body, fontWeight: '700', letterSpacing: 0.3 },
  card: {
    backgroundColor: colors.surface,
    borderRadius: radius.lg,
    borderWidth: 1,
    borderColor: colors.border,
    padding: spacing.lg,
    gap: spacing.sm,
  },
  center: { flex: 1, alignItems: 'center', justifyContent: 'center', gap: spacing.md, backgroundColor: colors.background },
});
