// Datos del panel admin. La seguridad la da el servidor: si no sos admin, todo esto falla.
import { supabase } from './supabase';
import type { AppointmentStatus, Service } from './types';

function check<T>(res: { data: T | null; error: { message: string } | null }): T {
  if (res.error) throw new Error(res.error.message);
  return res.data as T;
}

export interface AgendaItem {
  id: string;
  starts_at: string;
  ends_at: string;
  status: AppointmentStatus;
  price_charged: number;
  list_price: number;
  is_free_reward: boolean;
  cancel_reason: string | null;
  client: { id: string; full_name: string; phone: string | null } | null;
  service: { name: string } | null;
  barber: { display_name: string } | null;
}

const one = <T,>(x: T | T[] | null): T | null => (Array.isArray(x) ? x[0] ?? null : x);

/** Turnos entre dos fechas/horas (para la agenda) */
export async function fetchAgenda(fromIso: string, toIso: string): Promise<AgendaItem[]> {
  const rows = check(
    await supabase
      .from('appointments')
      .select('id, starts_at, ends_at, status, price_charged, list_price, is_free_reward, cancel_reason, client:profiles!appointments_client_id_fkey(id, full_name, phone), service:services(name), barber:barbers(display_name)')
      .gte('starts_at', fromIso)
      .lt('starts_at', toIso)
      .order('starts_at'),
  ) as any[];
  return rows.map((r) => ({
    ...r,
    price_charged: Number(r.price_charged),
    list_price: Number(r.list_price),
    client: one(r.client),
    service: one(r.service),
    barber: one(r.barber),
  }));
}

export async function setAppointmentStatus(id: string, status: AppointmentStatus, note?: string) {
  return check(await supabase.rpc('set_appointment_status', { p_id: id, p_status: status, p_note: note ?? null }));
}

// ----- Barberos -----
export interface BarberRow { id: string; display_name: string; profile_id: string | null; active: boolean }

export async function fetchBarbers(): Promise<BarberRow[]> {
  return check(await supabase.from('barbers').select('id, display_name, profile_id, active').order('sort_order')) ?? [];
}

/** Turnos reservados que quedaron fuera del horario o caen en un bloqueo */
export async function fetchOutsideHours(barberId: string) {
  return (check(await supabase.rpc('appointments_outside_hours', { p_barber: barberId })) as { id: string; starts_at: string }[]) ?? [];
}

// ----- Servicios -----
export async function fetchAllServices(): Promise<Service[]> {
  const rows = check(await supabase.from('services').select('*').order('active', { ascending: false }).order('sort_order'));
  return (rows ?? []).map((s: any) => ({ ...s, price: Number(s.price) }));
}

export type ServiceInput = Pick<Service, 'name' | 'description' | 'price' | 'duration_min' | 'counts_for_loyalty' | 'is_reward_base' | 'active'>;

export async function saveService(id: string | null, input: ServiceInput) {
  if (input.is_reward_base) {
    // Solo un servicio puede ser la referencia del corte gratis
    check(await supabase.from('services').update({ is_reward_base: false }).eq('is_reward_base', true).neq('id', id ?? '00000000-0000-0000-0000-000000000000'));
  }
  if (id) return check(await supabase.from('services').update(input).eq('id', id).select().single());
  const { count } = await supabase.from('services').select('id', { count: 'exact', head: true });
  return check(await supabase.from('services').insert({ ...input, sort_order: (count ?? 0) + 1 }).select().single());
}

// ----- Horarios -----
export interface HourRange { weekday: number; start: string; end: string }

export async function fetchWorkingHours(barberId: string): Promise<HourRange[]> {
  const rows = check(await supabase.from('working_hours').select('weekday, start_time, end_time').eq('barber_id', barberId).order('start_time'));
  return (rows ?? []).map((r: any) => ({ weekday: r.weekday, start: r.start_time.slice(0, 5), end: r.end_time.slice(0, 5) }));
}

export async function saveWorkingHours(barberId: string, hours: HourRange[]) {
  return check(await supabase.rpc('set_working_hours', { p_barber: barberId, p_hours: hours }));
}

// ----- Bloqueos / feriados -----
export interface TimeOff { id: string; starts_at: string; ends_at: string; reason: string | null; barber_id: string | null }

export async function fetchTimeOff(): Promise<TimeOff[]> {
  return check(await supabase.from('time_off').select('id, starts_at, ends_at, reason, barber_id').gt('ends_at', new Date().toISOString()).order('starts_at')) ?? [];
}

export async function addTimeOff(startsAt: string, endsAt: string, reason: string, barberId: string | null, createdBy: string) {
  return check(await supabase.from('time_off').insert({ starts_at: startsAt, ends_at: endsAt, reason: reason || null, barber_id: barberId, created_by: createdBy }).select().single());
}

export async function deleteTimeOff(id: string) {
  check(await supabase.from('time_off').delete().eq('id', id).select());
}

// ----- Reglas -----
export interface Rules {
  slot_interval_min: number;
  cancel_limit_hours: number;
  booking_window_days: number;
  min_notice_min: number;
  max_active_bookings: number;
}

export async function fetchRules(): Promise<Rules> {
  return check(await supabase.from('settings').select('slot_interval_min, cancel_limit_hours, booking_window_days, min_notice_min, max_active_bookings').single()) as Rules;
}

export async function saveRules(r: Rules) {
  const res = check(await supabase.from('settings').update(r).eq('id', true).select());
  if (!res || (Array.isArray(res) && res.length === 0)) throw new Error('No tenés permiso para cambiar las reglas.');
}

/** Link para abrir WhatsApp con el cliente */
export function whatsappLink(phoneE164: string | null, text?: string): string | null {
  if (!phoneE164) return null;
  const digits = phoneE164.replace(/\D/g, '');
  return `https://wa.me/${digits}${text ? `?text=${encodeURIComponent(text)}` : ''}`;
}
