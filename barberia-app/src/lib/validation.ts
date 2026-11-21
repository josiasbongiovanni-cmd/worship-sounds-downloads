// Validaciones y formatos (con pruebas en validation.test.ts)

/**
 * Normaliza un celular argentino al formato de WhatsApp: +549 + código de área + número (10 dígitos).
 * Acepta formas comunes: "341 555-1234", "0341 15 555 1234", "+54 9 341 555 1234", "11 15 5555 1234".
 * Devuelve null si no parece un celular argentino válido.
 */
export function normalizeArPhone(input: string): string | null {
  let d = (input ?? '').replace(/\D/g, '');
  if (d.startsWith('54')) d = d.slice(2);
  if (d.startsWith('9') && d.length === 11) d = d.slice(1);
  if (d.startsWith('0')) d = d.slice(1);

  // Quitar el "15" que va después del código de área
  if (d.length === 12) {
    const areaLengths = d.startsWith('11') ? [2] : [3, 4, 2];
    for (const len of areaLengths) {
      if (d.slice(len, len + 2) === '15') {
        d = d.slice(0, len) + d.slice(len + 2);
        break;
      }
    }
  }
  if (d.length !== 10 || d.startsWith('0')) return null;
  return `+549${d}`;
}

/** "+5493415551234" -> "+54 9 341 555-1234" (solo para mostrar) */
export function formatArPhone(e164: string | null | undefined): string {
  if (!e164) return '';
  const m = /^\+549(\d{10})$/.exec(e164);
  if (!m) return e164;
  const n = m[1];
  const area = n.startsWith('11') ? 2 : 3;
  const rest = n.slice(area);
  return `+54 9 ${n.slice(0, area)} ${rest.slice(0, rest.length - 4)}-${rest.slice(-4)}`;
}

export function isValidEmail(email: string): boolean {
  return /^[^\s@]+@[^\s@]+\.[^\s@]{2,}$/.test(email.trim());
}

/** Precio en pesos: 8000 -> "$ 8.000" */
export function formatPrice(value: number): string {
  const rounded = Math.round(value);
  const sign = rounded < 0 ? '-' : '';
  const digits = Math.abs(rounded).toString().replace(/\B(?=(\d{3})+(?!\d))/g, '.');
  return `${sign}$ ${digits}`;
}

/** Duración: 30 -> "30 min", 90 -> "1 h 30 min" */
export function formatDuration(min: number): string {
  if (min < 60) return `${min} min`;
  const h = Math.floor(min / 60);
  const m = min % 60;
  return m ? `${h} h ${m} min` : `${h} h`;
}
