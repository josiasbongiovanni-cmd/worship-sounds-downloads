// Reservas: todo pasa por funciones del servidor (supabase/migrations/..._reservas.sql)
import { supabase } from './supabase';
import type { AppointmentStatus } from './types';

export interface Barber { id: string; display_name: string; photo_url: string | null }
export interface AvailableDay { day: string; free_slots: number }
export interface Slot { starts_at: string; ends_at: string; label: string }
export interface Quote { list_price: number; discount: number; is_free_reward: boolean }

export interface Appointment {
  id: string;
  starts_at: string;
  ends_at: string;
  status: AppointmentStatus;
  list_price: number;
  discount: number;
  price_charged: number;
  is_free_reward: boolean;
  service_id: string;
  barber_id: string;
  service: { name: string } | null;
  barber: { display_name: string } | null;
}

function check<T>(res: { data: T | null; error: { message: string } | null }): T {
  if (res.error) throw new Error(res.error.message);
  return res.data as T;
}

/** Barberos activos que hacen ese servicio */
export async function fetchBarbersForService(serviceId: string): Promise<Barber[]> {
  const rows = check(
    await supabase
      .from('barbers')
      .select('id, display_name, photo_url, barber_services!inner(service_id)')
      .eq('active', true)
      .eq('barber_services.service_id', serviceId)
      .order('sort_order'),
  );
  return (rows ?? []).map(({ id, display_name, photo_url }) => ({ id, display_name, photo_url }));
}

export async function fetchAvailableDays(serviceId: string, barberId: string): Promise<AvailableDay[]> {
  return check(await supabase.rpc('get_available_days', { p_service: serviceId, p_barber: barberId })) ?? [];
}

export async function fetchSlots(serviceId: string, barberId: string, day: string): Promise<Slot[]> {
  return check(await supabase.rpc('get_available_slots', { p_service: serviceId, p_barber: barberId, p_date: day })) ?? [];
}

export async function fetchQuote(serviceId: string): Promise<Quote> {
  const rows = check(await supabase.rpc('get_booking_quote', { p_service: serviceId })) as Quote[];
  const q = rows?.[0];
  return { list_price: Number(q?.list_price ?? 0), discount: Number(q?.discount ?? 0), is_free_reward: Boolean(q?.is_free_reward) };
}

export async function bookAppointment(serviceId: string, barberId: string, startsAt: string) {
  return check(await supabase.rpc('book_appointment', { p_service: serviceId, p_barber: barberId, p_starts_at: startsAt }));
}

export async function rescheduleAppointment(id: string, newStartsAt: string) {
  return check(await supabase.rpc('reschedule_appointment', { p_id: id, p_new_starts_at: newStartsAt }));
}

export async function cancelAppointment(id: string, reason?: string) {
  return check(await supabase.rpc('cancel_appointment', { p_id: id, p_reason: reason ?? null }));
}

const APPOINTMENT_FIELDS =
  'id, starts_at, ends_at, status, list_price, discount, price_charged, is_free_reward, service_id, barber_id, service:services(name), barber:barbers(display_name)';

function normalize(rows: any[]): Appointment[] {
  return rows.map((r) => ({
    ...r,
    list_price: Number(r.list_price),
    discount: Number(r.discount),
    price_charged: Number(r.price_charged),
    service: Array.isArray(r.service) ? r.service[0] : r.service,
    barber: Array.isArray(r.barber) ? r.barber[0] : r.barber,
  }));
}

/** Turnos del cliente logueado: próximos (reservados) e historial */
export async function fetchMyAppointments(userId: string) {
  const rows = check(
    await supabase.from('appointments').select(APPOINTMENT_FIELDS).eq('client_id', userId).order('starts_at', { ascending: false }).limit(100),
  );
  const all = normalize(rows ?? []);
  const now = Date.now();
  const upcoming = all.filter((a) => a.status === 'booked' && new Date(a.ends_at).getTime() > now).reverse();
  const history = all.filter((a) => !upcoming.includes(a));
  return { upcoming, history };
}

export async function fetchAppointment(id: string): Promise<Appointment | null> {
  const row = check(await supabase.from('appointments').select(APPOINTMENT_FIELDS).eq('id', id).maybeSingle());
  return row ? normalize([row])[0] : null;
}

export const statusLabel: Record<AppointmentStatus, string> = {
  booked: 'Reservado',
  completed: 'Completado',
  no_show: 'No se presentó',
  cancelled: 'Cancelado',
};
