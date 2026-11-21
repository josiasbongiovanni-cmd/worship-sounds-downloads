import type { Session } from '@supabase/supabase-js';
import { createContext, useCallback, useContext, useEffect, useState, type ReactNode } from 'react';

import { supabase } from '@/lib/supabase';
import type { Profile } from '@/lib/types';

interface AuthState {
  loading: boolean;
  session: Session | null;
  profile: Profile | null;
  /** Mensaje si no se pudo leer el perfil (ej: falta correr el SQL en Supabase) */
  profileError: string | null;
  isStaff: boolean;
  refreshProfile: () => Promise<void>;
  signOut: () => Promise<void>;
}

const AuthContext = createContext<AuthState | null>(null);

export function AuthProvider({ children }: { children: ReactNode }) {
  const [loading, setLoading] = useState(true);
  const [session, setSession] = useState<Session | null>(null);
  const [profile, setProfile] = useState<Profile | null>(null);
  const [profileError, setProfileError] = useState<string | null>(null);

  const loadProfile = useCallback(async (s: Session | null) => {
    setProfileError(null);
    if (!s) {
      setProfile(null);
      return;
    }
    const { data, error } = await supabase
      .from('profiles')
      .select('id, full_name, phone, email, role, push_token')
      .eq('id', s.user.id)
      .maybeSingle();
    if (error || !data) {
      setProfile(null);
      setProfileError(error?.message ?? 'No se encontró tu perfil en la base de datos.');
      return;
    }
    setProfile(data as Profile);
  }, []);

  useEffect(() => {
    let mounted = true;
    supabase.auth.getSession().then(async ({ data }) => {
      if (!mounted) return;
      setSession(data.session);
      await loadProfile(data.session);
      if (mounted) setLoading(false);
    });
    const { data: sub } = supabase.auth.onAuthStateChange((_event, s) => {
      setSession(s);
      // Se difiere para no llamar a Supabase dentro del callback de auth
      setTimeout(() => loadProfile(s), 0);
    });
    return () => {
      mounted = false;
      sub.subscription.unsubscribe();
    };
  }, [loadProfile]);

  const value: AuthState = {
    loading,
    session,
    profile,
    profileError,
    isStaff: profile?.role === 'admin' || profile?.role === 'barber',
    refreshProfile: () => loadProfile(session),
    signOut: async () => {
      await supabase.auth.signOut();
    },
  };

  return <AuthContext.Provider value={value}>{children}</AuthContext.Provider>;
}

export function useAuth(): AuthState {
  const ctx = useContext(AuthContext);
  if (!ctx) throw new Error('useAuth debe usarse dentro de <AuthProvider>');
  return ctx;
}
