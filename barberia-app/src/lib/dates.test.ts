/// <reference types="jest" />
import { canClientChange, dayParts, formatDateTime, formatDayLong, formatTime, localDay, relativeDay } from './dates';

describe('fechas en horario de Argentina', () => {
  it('convierte la hora UTC a hora argentina', () => {
    expect(formatTime('2026-11-25T13:00:00Z')).toBe('10:00');
    expect(formatTime('2026-11-25T13:00:00-03:00')).toBe('13:00');
    expect(formatTime('2026-11-26T01:30:00Z')).toBe('22:30');
  });
  it('el día cambia según Argentina, no según UTC', () => {
    expect(localDay('2026-11-26T01:30:00Z')).toBe('2026-11-25');
    expect(localDay('2026-11-26T03:00:00Z')).toBe('2026-11-26');
  });
  it('nombres de días y meses en español', () => {
    expect(formatDayLong('2026-11-25')).toBe('Miércoles 25 de noviembre');
    expect(dayParts('2026-11-24')).toEqual({ weekday: 'Mar', day: '24', month: 'nov' });
    expect(formatDateTime('2026-11-25T13:00:00Z')).toBe('Miércoles 25 de noviembre · 10:00');
  });
  it('hoy y mañana', () => {
    const now = new Date('2026-11-25T15:00:00Z'); // 12:00 en Argentina
    expect(relativeDay('2026-11-25', now)).toBe('Hoy');
    expect(relativeDay('2026-11-26', now)).toBe('Mañana');
    expect(relativeDay('2026-11-27', now)).toBeNull();
  });
  it('límite para cancelar (2 horas)', () => {
    const now = new Date('2026-11-25T12:00:00Z');
    expect(canClientChange('2026-11-25T14:00:00Z', 2, now)).toBe(true);
    expect(canClientChange('2026-11-25T13:59:00Z', 2, now)).toBe(false);
  });
});

describe('semana y días para la agenda', () => {
  const { addDays, weekStart, localIso, weekdayOf, dayStartIso } = require('./dates');
  it('suma días cruzando meses', () => {
    expect(addDays('2026-11-30', 1)).toBe('2026-12-01');
    expect(addDays('2026-12-01', -1)).toBe('2026-11-30');
  });
  it('la semana empieza el lunes', () => {
    expect(weekStart('2026-11-25')).toBe('2026-11-23'); // miércoles -> lunes
    expect(weekStart('2026-11-23')).toBe('2026-11-23'); // lunes
    expect(weekStart('2026-11-29')).toBe('2026-11-23'); // domingo -> lunes anterior
  });
  it('arma fechas/horas en horario argentino', () => {
    expect(new Date(localIso('2026-11-25', '15:30')).toISOString()).toBe('2026-11-25T18:30:00.000Z');
    expect(new Date(dayStartIso('2026-11-25')).toISOString()).toBe('2026-11-25T03:00:00.000Z');
    expect(weekdayOf('2026-11-24')).toBe(2);
  });
});
