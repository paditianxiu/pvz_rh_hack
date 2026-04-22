import { Events, Window } from '@wailsio/runtime';
import { useEffect, useMemo, useRef, useState } from 'react';
import { overlayVisibilityEventName, overlayVisibilityStorageKey } from './constants/overlay';

type ZombiePoint = {
  x: number;
  y: number;
  z: number;
  row: number;
  column: number;
};

const zombiePollIntervalMs = 120;


function readOverlayVisibilityFromStorage(): boolean {
  try {
    return localStorage.getItem(overlayVisibilityStorageKey) === '1';
  } catch {
    return false;
  }
}


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

function parseZombiePositions(raw: string): ZombiePoint[] {

  const parsed = JSON.parse(raw);

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
      const column = parseFiniteNumber(record.Colum);


      return { x, y, z, row, column };
    })
    .filter((point): point is ZombiePoint => point !== null);

}

function OverlayPage() {
  const canvasRef = useRef<HTMLCanvasElement | null>(null);
  const [zombiePoints, setZombiePoints] = useState<ZombiePoint[]>([]);
  const [overlayOrigin, setOverlayOrigin] = useState({ x: 0, y: 0 });
  const [overlayEnabled, setOverlayEnabled] = useState<boolean>(readOverlayVisibilityFromStorage);

  const draw = useMemo(
    () => (canvas: HTMLCanvasElement, points: ZombiePoint[], originX: number) => {
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
      context.strokeStyle = 'rgba(255, 65, 65, 0.9)';
      context.fillStyle = 'rgba(255, 65, 65, 0.95)';
      context.lineWidth = 5;
      points.forEach((point) => {
        const localX = point.x / 1.5;
        const localY = (height / 5) * (point.row + 0.2);

        context.beginPath();
        context.moveTo(centerX, centerY);
        context.lineTo(localX, localY);
        context.stroke();
        context.beginPath();
        context.arc(localX, localY, 4, 0, Math.PI * 2);
        context.fill();

        context.fillText(`Row:${point.row}`, localX, localY)

        // context.fillText(`X:${localX}`, localX, localY)

        // context.fillText(`Y:${localY}`, localX, localY + 20)

      });
      context.fillStyle = 'rgba(54, 214, 84, 0.95)';
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
        const position = await Window.Position();
        if (disposed) {
          return;
        }

        setOverlayOrigin(position);
        if (!overlayEnabled) {
          setZombiePoints((previousPoints) => (previousPoints.length === 0 ? previousPoints : []));
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
      draw(canvas, zombiePoints, overlayOrigin.x);
    };

    redraw();
    window.addEventListener('resize', redraw);
    return () => {
      window.removeEventListener('resize', redraw);
    };
  }, [draw, zombiePoints, overlayOrigin]);

  return (
    <div className="overlay-root">
      <canvas ref={canvasRef} className="overlay-canvas" />
    </div>
  );
}

export default OverlayPage;
