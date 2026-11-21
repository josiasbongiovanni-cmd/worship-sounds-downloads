// Paleta Mathu Barber: negro, dorado y blanco
export const colors = {
  background: '#0B0B0D',
  surface: '#16161A',
  surfaceAlt: '#1F1F25',
  border: '#2C2C33',
  gold: '#C9A24B',
  goldSoft: '#E4C77D',
  goldDark: '#8E6F2C',
  text: '#F5F3EE',
  textMuted: '#A3A1A8',
  onGold: '#0B0B0D',
  danger: '#E5534B',
  success: '#4CAF7A',
} as const;

export const spacing = { xs: 4, sm: 8, md: 16, lg: 24, xl: 32, xxl: 48 } as const;

export const radius = { sm: 8, md: 14, lg: 22, pill: 999 } as const;

// Letras grandes: fácil de leer para cualquier persona
export const font = {
  small: 14,
  body: 17,
  large: 20,
  title: 28,
  hero: 36,
} as const;

// Botones grandes (mínimo recomendado para tocar con el dedo: 48)
export const touch = { minHeight: 56 } as const;
