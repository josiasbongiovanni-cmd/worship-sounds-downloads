// Fechas y horas en horario de Argentina.
// Argentina usa UTC-3 todo el año (sin horario de verano desde 2009), así que
// usamos un desfase fijo: funciona igual en cualquier celular, esté en la zona que esté.
const AR_OFFSET_MS = -3 * 60 * 60 * 1000;

const DIAS = ['domingo', 'lunes', 'martes', 'miércoles', 'jueves', 'viernes', 'sábado'];
const MESES = ['enero', 'febrero', 'marzo', 'abril', 'mayo', 'junio', 'julio', 'agosto', 'septiembre', 'octubre', 'noviembre', 'diciembre'];

/** Devuelve un Date "corrido" para leer la hora argentina con getUTC*() */
function ar(iso: string | Date): Date {
  return new Date(new Date(iso).getTime() + AR_OFFSET_MS);
}

const pad = (n: number) => n.toString().padStart(2, '0');
const cap = (s: string) => s.charAt(0).toUpperCase() + s.slice(1);

/** "2026-11-25T13:00:00Z" -> "10:00" */
export function formatTime(iso: string): string {
  const d = ar(iso);
  return `${pad(d.getUTCHours())}:${pad(d.getUTCMinutes())}`;
}

/** Día (YYYY-MM-DD) -> partes para mostrar en tarjetitas: { weekday: 'Mar', day: '25', month: 'nov' } */
export function dayParts(day: string) {
  const d = new Date(`${day}T12:00:00Z`);
  return { weekday: cap(DIAS[d.getUTCDay()].slice(0, 3)), day: String(d.getUTCDate()), month: MESES[d.getUTCMonth()].slice(0, 3) };
}

/** "2026-11-25" -> "Miércoles 25 de noviembre" */
export function formatDayLong(day: string): string {
  const d = new Date(`${day}T12:00:00Z`);
  return `${cap(DIAS[d.getUTCDay()])} ${d.getUTCDate()} de ${MESES[d.getUTCMonth()]}`;
}

/** Fecha y hora de un turno -> "Miércoles 25 de noviembre · 10:00" */
export function formatDateTime(iso: string): string {
  return `${formatDayLong(localDay(iso))} · ${formatTime(iso)}`;
}

/** Día argentino (YYYY-MM-DD) de una fecha/hora */
export function localDay(iso: string | Date): string {
  const d = ar(iso);
  return `${d.getUTCFullYear()}-${pad(d.getUTCMonth() + 1)}-${pad(d.getUTCDate())}`;
}

/** "Hoy", "Mañana" o null */
export function relativeDay(day: string, now: Date = new Date()): string | null {
  const today = localDay(now);
  if (day === today) return 'Hoy';
  const tomorrow = localDay(new Date(now.getTime() + 24 * 3600 * 1000));
  return day === tomorrow ? 'Mañana' : null;
}

/** ¿El cliente todavía puede cancelar/reprogramar? (la regla real la valida el servidor) */
export function canClientChange(startsAt: string, limitHours: number, now: Date = new Date()): boolean {
  return new Date(startsAt).getTime() - now.getTime() >= limitHours * 3600 * 1000;
}
