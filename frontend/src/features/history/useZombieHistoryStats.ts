import { useEffect, useMemo, useState } from 'react';
import { parseZombiePositions } from '../zombie/parser';
import type { ZombiePoint } from '../zombie/types';

export type ZombieRowCounter = Record<number, number>;

export type ZombieHistorySample = {
  timestamp: number;
  total: number;
  rows: ZombieRowCounter;
};

type ZombieHistoryStats = {
  samples: ZombieHistorySample[];
  latestSample: ZombieHistorySample | null;
  latestPoints: ZombiePoint[];
  peakCount: number;
  averageCount: number;
  accumulatedRows: ZombieRowCounter;
  errorMessage: string | null;
  lastUpdatedAt: number | null;
  reset: () => void;
};

const maxSamples = 1200;
const defaultPollIntervalMs = 700;

function resolveErrorMessage(error: unknown): string {
  if (error instanceof Error && error.message) {
    return error.message;
  }

  return String(error);
}

function normalizeRowIndex(rawRow: number): number | null {
  if (!Number.isFinite(rawRow)) {
    return null;
  }

  const rounded = Math.round(rawRow);
  if (rounded < 0 || rounded > 15) {
    return null;
  }

  return rounded;
}

function countRows(points: ZombiePoint[]): ZombieRowCounter {
  return points.reduce<ZombieRowCounter>((result, point) => {
    const row = normalizeRowIndex(point.row);
    if (row === null) {
      return result;
    }

    const previousCount = result[row] ?? 0;
    result[row] = previousCount + 1;
    return result;
  }, {});
}

function accumulateRows(samples: ZombieHistorySample[]): ZombieRowCounter {
  return samples.reduce<ZombieRowCounter>((result, sample) => {
    Object.entries(sample.rows).forEach(([rowKey, count]) => {
      const row = Number.parseInt(rowKey, 10);
      if (!Number.isFinite(row)) {
        return;
      }

      const previousCount = result[row] ?? 0;
      result[row] = previousCount + count;
    });
    return result;
  }, {});
}

export function useZombieHistoryStats(
  pollIntervalMs: number = defaultPollIntervalMs,
): ZombieHistoryStats {
  const [samples, setSamples] = useState<ZombieHistorySample[]>([]);
  const [latestPoints, setLatestPoints] = useState<ZombiePoint[]>([]);
  const [errorMessage, setErrorMessage] = useState<string | null>(null);
  const [lastUpdatedAt, setLastUpdatedAt] = useState<number | null>(null);

  useEffect(() => {
    let disposed = false;

    const poll = async () => {
      try {
        const raw = await 'GetZombiePositions'.invoke();
        if (disposed || typeof raw !== 'string') {
          return;
        }

        const points = parseZombiePositions(raw);
        const sample: ZombieHistorySample = {
          timestamp: Date.now(),
          total: points.length,
          rows: countRows(points),
        };

        setLatestPoints(points);
        setSamples((previousSamples) => {
          const nextSamples = [...previousSamples, sample];
          if (nextSamples.length > maxSamples) {
            return nextSamples.slice(nextSamples.length - maxSamples);
          }
          return nextSamples;
        });
        setLastUpdatedAt(sample.timestamp);
        if (errorMessage !== null) {
          setErrorMessage(null);
        }
      } catch (error) {
        if (disposed) {
          return;
        }

        const resolved = resolveErrorMessage(error);
        if (resolved !== errorMessage) {
          setErrorMessage(resolved);
        }
      }
    };

    void poll();
    const timer = window.setInterval(() => {
      void poll();
    }, pollIntervalMs);

    return () => {
      disposed = true;
      window.clearInterval(timer);
    };
  }, [errorMessage, pollIntervalMs]);

  const latestSample = useMemo(
    () => (samples.length > 0 ? samples[samples.length - 1] : null),
    [samples],
  );

  const peakCount = useMemo(
    () => samples.reduce((peak, sample) => Math.max(peak, sample.total), 0),
    [samples],
  );

  const averageCount = useMemo(() => {
    if (samples.length === 0) {
      return 0;
    }

    const total = samples.reduce((sum, sample) => sum + sample.total, 0);
    return Number((total / samples.length).toFixed(2));
  }, [samples]);

  const accumulatedRows = useMemo(() => accumulateRows(samples), [samples]);

  const reset = () => {
    setLatestPoints([]);
    setSamples([]);
    setErrorMessage(null);
    setLastUpdatedAt(null);
  };

  return {
    samples,
    latestSample,
    latestPoints,
    peakCount,
    averageCount,
    accumulatedRows,
    errorMessage,
    lastUpdatedAt,
    reset,
  };
}
