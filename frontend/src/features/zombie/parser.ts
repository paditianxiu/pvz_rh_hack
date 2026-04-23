import type { ZombiePoint } from './types';

function parseFiniteNumber(value: unknown): number | null {
  if (typeof value === 'number') {
    return Number.isFinite(value) ? value : null;
  }

  if (typeof value === 'string') {
    const parsed = Number.parseFloat(value);
    return Number.isFinite(parsed) ? parsed : null;
  }

  return null;
}

export function parseZombiePositions(raw: string): ZombiePoint[] {
  let parsed: unknown;
  try {
    parsed = JSON.parse(raw);
  } catch {
    return [];
  }

  if (!Array.isArray(parsed)) {
    return [];
  }

  return parsed
    .map((item) => {
      if (!item || typeof item !== 'object') {
        return null;
      }

      const record = item as Record<string, unknown>;
      const x = parseFiniteNumber(record.X);
      const y = parseFiniteNumber(record.Y);
      const z = parseFiniteNumber(record.Z);
      const row = parseFiniteNumber(record.Row);
      const column = parseFiniteNumber(record.Column);
      const name = record.Name
      if (x === null || y === null || z === null || row === null || column === null) {
        return null;
      }

      return {
        x,
        y,
        z,
        row,
        column,
        name
      };
    })
    .filter((point): point is ZombiePoint => point !== null);
}
