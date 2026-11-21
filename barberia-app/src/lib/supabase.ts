import AsyncStorage from '@react-native-async-storage/async-storage';
import { createClient } from '@supabase/supabase-js';
import { AppState, Platform } from 'react-native';

const url = process.env.EXPO_PUBLIC_SUPABASE_URL;
const anonKey = process.env.EXPO_PUBLIC_SUPABASE_ANON_KEY;

export const isSupabaseConfigured = Boolean(url && anonKey && !url.includes('TU-PROYECTO'));

export const supabase = createClient(url ?? 'https://placeholder.supabase.co', anonKey ?? 'placeholder', {
  auth: {
    // En el celular la sesión queda guardada: no hay que loguearse cada vez
    storage: Platform.OS === 'web' && typeof window === 'undefined' ? undefined : AsyncStorage,
    autoRefreshToken: true,
    persistSession: true,
    detectSessionInUrl: false,
  },
});

// Renueva la sesión solo mientras la app está abierta
if (Platform.OS !== 'web') {
  AppState.addEventListener('change', (state) => {
    if (state === 'active') supabase.auth.startAutoRefresh();
    else supabase.auth.stopAutoRefresh();
  });
}

/** Traduce los errores más comunes de Supabase a mensajes simples */
export function friendlyError(message: string | undefined | null): string {
  const m = (message ?? '').toLowerCase();
  if (m.includes('invalid login credentials')) return 'Email o contraseña incorrectos.';
  if (m.includes('already registered') || m.includes('already been registered')) return 'Ya existe una cuenta con ese email. Probá iniciar sesión.';
  if (m.includes('email not confirmed')) return 'Tenés que confirmar tu email. Revisá tu casilla (y spam).';
  if (m.includes('password should be at least')) return 'La contraseña tiene que tener al menos 6 caracteres.';
  if (m.includes('rate limit') || m.includes('too many')) return 'Demasiados intentos. Esperá unos minutos.';
  if (m.includes('network') || m.includes('fetch')) return 'Sin conexión. Revisá tu internet.';
  return message || 'Ocurrió un error. Probá de nuevo.';
}
