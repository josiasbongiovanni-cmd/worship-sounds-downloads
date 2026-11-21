// Tipos de datos (espejo de las tablas de supabase/migrations)
export type UserRole = 'client' | 'barber' | 'admin';
export type AppointmentStatus = 'booked' | 'completed' | 'no_show' | 'cancelled';

export interface Profile {
  id: string;
  full_name: string;
  phone: string | null;
  email: string | null;
  role: UserRole;
  push_token: string | null;
}

export interface Service {
  id: string;
  name: string;
  description: string | null;
  price: number;
  duration_min: number;
  counts_for_loyalty: boolean;
  is_reward_base: boolean;
  active: boolean;
  sort_order: number;
}

export interface LoyaltyCard {
  client_id: string;
  stamps: number;
  rewards_available: number;
  total_earned: number;
  total_redeemed: number;
}

export interface Settings {
  business_name: string;
  timezone: string;
  loyalty_goal: number;
  cancel_limit_hours: number;
}
