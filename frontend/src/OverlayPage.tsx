import { Call, Events, Window } from '@wailsio/runtime';
import { useEffect, useMemo, useRef, useState } from 'react';
import { overlayVisibilityEventName, overlayVisibilityStorageKey } from './constants/overlay';
import { parseZombiePositions } from './features/zombie/parser';
import { type ZombiePoint } from './features/zombie/types';

const zombiePollIntervalMs = 120;
const gameProcessName = 'PlantsVsZombiesRH.exe';
const syncOverlayMethodCandidates = [
  'main.ProcessService.SyncOverlayWindow',
  'changeme.ProcessService.SyncOverlayWindow',
];


function readOverlayVisibilityFromStorage(): boolean {
  try {
    return localStorage.getItem(overlayVisibilityStorageKey) === '1';
  } catch {
    return false;
  }
}

async function callFirstAvailable(candidates: string[], ...args: unknown[]) {
  let lastError: unknown;

  for (const methodName of candidates) {
    try {
      await Call.ByName(methodName, ...args);
      return;
    } catch (error: unknown) {
      const errorName =
        typeof error === 'object' && error !== null && 'name' in error
          ? String((error as { name: unknown }).name)
          : '';

      if (errorName === 'ReferenceError') {
        lastError = error;
        continue;
      }

      throw error;
    }
  }

  throw lastError ?? new Error('未找到可用的 ProcessService 方法');
}

function OverlayPage() {
  const canvasRef = useRef<HTMLCanvasElement | null>(null);
  const [zombiePoints, setZombiePoints] = useState<ZombiePoint[]>([]);
  const [overlayEnabled, setOverlayEnabled] = useState<boolean>(readOverlayVisibilityFromStorage);

  const draw = useMemo(
    () => (canvas: HTMLCanvasElement, points: ZombiePoint[]) => {
      const width = window.innerWidth;
      const height = window.innerHeight;
      if (width <= 0 || height <= 0) {
        return;
      }

      if (canvas.width !== width || canvas.height !== height) {
        canvas.width = width;
        canvas.height = height;
      }

      const context = canvas.getContext('2d');
      if (!context) {
        return;
      }

      context.clearRect(0, 0, width, height);
      const centerX = width / 2;
      const centerY = height / 2;
      context.strokeStyle = '#14f903e6';
      context.fillStyle = '#ffff0be6';
      context.lineWidth = 5;
      points.forEach((point) => {
        const localX = point.x / 1.5;
        const localY = (height / 5) * (point.row + 0.5);

        context.beginPath();
        context.moveTo(centerX, centerY);
        context.lineTo(localX, localY);
        context.stroke();
        context.beginPath();
        context.arc(localX, localY, 4, 0, Math.PI * 2);
        context.fill();
        context.fillText(point.name, localX, localY)
        // context.fillText(`X:${localX}`, localX, localY)
        // context.fillText(`Y:${localY}`, localX, localY + 20)
      });
      context.fillStyle = '#ff0000f2';
      context.beginPath();
      context.arc(centerX, centerY, 5, 0, Math.PI * 2);
      context.fill();
    },
    [],
  );

  useEffect(() => {
    document.body.classList.add('overlay-window');
    return () => {
      document.body.classList.remove('overlay-window');
    };
  }, []);

  useEffect(() => {
    const off = Events.On(overlayVisibilityEventName, (event) => {
      const enabled = Boolean(event.data);
      setOverlayEnabled(enabled);

      if (!enabled) {
        setZombiePoints((previousPoints) => (previousPoints.length === 0 ? previousPoints : []));
      }
    });

    return () => {
      off();
    };
  }, []);

  useEffect(() => {
    let disposed = false;
    const poll = async () => {
      try {
        if (!overlayEnabled) {
          setZombiePoints((previousPoints) => (previousPoints.length === 0 ? previousPoints : []));
          return;
        }

        await callFirstAvailable(syncOverlayMethodCandidates, gameProcessName);
        await Window.SetAlwaysOnTop(true);
        if (disposed) {
          return;
        }

        const raw = await 'GetZombiePositions'.invoke();
        if (disposed || typeof raw !== 'string') {
          return;
        }

        setZombiePoints(parseZombiePositions(raw));
      } catch {
      }
    };

    void poll();
    const timer = window.setInterval(() => {
      void poll();
    }, zombiePollIntervalMs);

    return () => {
      disposed = true;
      window.clearInterval(timer);
    };
  }, [overlayEnabled]);

  useEffect(() => {
    const canvas = canvasRef.current;
    if (!canvas) {
      return;
    }

    const redraw = () => {
      draw(canvas, zombiePoints);
    };

    redraw();
    window.addEventListener('resize', redraw);
    return () => {
      window.removeEventListener('resize', redraw);
    };
  }, [draw, zombiePoints]);

  return (
    <div className="overlay-root">
      <canvas ref={canvasRef} className="overlay-canvas" />
    </div>
  );
}

export default OverlayPage;
