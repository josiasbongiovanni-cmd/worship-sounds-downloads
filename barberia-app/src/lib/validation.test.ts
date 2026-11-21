/// <reference types="jest" />
import { formatArPhone, formatDuration, formatPrice, isValidEmail, normalizeArPhone } from './validation';

describe('normalizeArPhone (WhatsApp Argentina)', () => {
  it.each([
    ['341 555-1234', '+5493415551234'],
    ['3415551234', '+5493415551234'],
    ['0341 15 555 1234', '+5493415551234'],
    ['0341 155551234', '+5493415551234'],
    ['+54 9 341 555 1234', '+5493415551234'],
    ['+54 341 555 1234', '+5493415551234'],
    ['5493415551234', '+5493415551234'],
    ['11 15 5555 1234', '+5491155551234'],
    ['011 5555-1234', '+5491155551234'],
    ['+54 9 11 5555-1234', '+5491155551234'],
  ])('%s -> %s', (input, expected) => {
    expect(normalizeArPhone(input)).toBe(expected);
  });

  it.each(['', '123', 'hola', '555-1234', '34155512345678'])('rechaza "%s"', (input) => {
    expect(normalizeArPhone(input)).toBeNull();
  });
});

describe('formatos', () => {
  it('muestra el teléfono lindo', () => {
    expect(formatArPhone('+5493415551234')).toBe('+54 9 341 555-1234');
    expect(formatArPhone('+5491155551234')).toBe('+54 9 11 5555-1234');
  });
  it('valida emails', () => {
    expect(isValidEmail('cliente@gmail.com')).toBe(true);
    expect(isValidEmail('cliente@gmail')).toBe(false);
    expect(isValidEmail('sin arroba.com')).toBe(false);
  });
  it('formatea precios en pesos', () => {
    expect(formatPrice(8000)).toBe('$ 8.000');
    expect(formatPrice(0)).toBe('$ 0');
    expect(formatPrice(1250000)).toBe('$ 1.250.000');
  });
  it('formatea duraciones', () => {
    expect(formatDuration(30)).toBe('30 min');
    expect(formatDuration(60)).toBe('1 h');
    expect(formatDuration(90)).toBe('1 h 30 min');
  });
});
