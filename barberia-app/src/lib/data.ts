// Lecturas comunes a la base de datos
import { useFocusEffect } from 'expo-router';
import { useCallback, useState } from 'react';

import { supabase } from './supabase';
import type { LoyaltyCard, Service, Settings } from './types';

/** Ejecuta una carga cada vez que la pantalla se muestra */
export function useLoad<T>(loader: () => Promise<T>) {
  const [data, setData] = useState<T | null>(null);
  const [error, setError] = useState<string | null>(null);
  const [loading, setLoading] = useState(true);
  const reload = useCallback(async () => {
    setLoading(true);
    try {
      setData(await loader());
      setError(null);
    } catch (e) {
      setError(e instanceof Error ? e.message : String(e));
    } finally {
      setLoading(false);
    }
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, []);
  useFocusEffect(useCallback(() => { reload(); }, [reload]));
  return { data, error, loading, reload };
}

export async function fetchServices(): Promise<Service[]> {
  const { data, error } = await supabase.from('services').select('*').eq('active', true).order('sort_order');
  if (error) throw error;
  return (data ?? []).map((s) => ({ ...s, price: Number(s.price) })) as Service[];
}

export async function fetchSettings(): Promise<Settings> {
  const { data, error } = await supabase.from('settings').select('business_name, timezone, loyalty_goal, cancel_limit_hours').single();
  if (error) throw error;
  return data as Settings;
}

export async function fetchMyLoyaltyCard(userId: string): Promise<LoyaltyCard | null> {
  const { data, error } = await supabase.from('loyalty_cards').select('*').eq('client_id', userId).maybeSingle();
  if (error) throw error;
  return data as LoyaltyCard | null;
}
