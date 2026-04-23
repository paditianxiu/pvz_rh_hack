import type { ZombiePoint } from '../../zombie/types';

type ZombiePositionRadarChartProps = {
  title: string;
  points: ZombiePoint[];
};

const chartSize = 360;
const chartCenter = chartSize / 2;
const chartRadius = 132;
const minRadarRadius = 18;

function normalizeRow(rawRow: number): number | null {
  if (!Number.isFinite(rawRow)) {
    return null;
  }

  const rounded = Math.round(rawRow);
  if (rounded < 0 || rounded > 31) {
    return null;
  }

  return rounded;
}

function toPolarPoint(laneIndex: number, laneCount: number, ratio: number): { x: number; y: number } {
  const clampedRatio = Math.max(0, Math.min(ratio, 1));
  const radius = minRadarRadius + (chartRadius - minRadarRadius) * clampedRatio;
  const angle = -Math.PI / 2 + (laneIndex / laneCount) * Math.PI * 2;
  return {
    x: chartCenter + radius * Math.cos(angle),
    y: chartCenter + radius * Math.sin(angle),
  };
}

function ZombiePositionRadarChart({ title, points }: ZombiePositionRadarChartProps) {
  const normalizedRows = points
    .map((point) => normalizeRow(point.row))
    .filter((row): row is number => row !== null);

  const laneCount = normalizedRows.length > 0
    ? Math.max(5, Math.min(12, Math.max(...normalizedRows) + 1))
    : 5;
  const lanes = Array.from({ length: laneCount }, (_, index) => index);

  const maxDistance = Math.max(1, ...points.map((point) => Math.max(0, point.x)));

  const scatterPoints = points
    .map((point, index) => {
      const row = normalizeRow(point.row);
      if (row === null) {
        return null;
      }

      const laneIndex = Math.min(row, laneCount - 1);
      const ratio = Math.max(0, point.x) / maxDistance;
      const polar = toPolarPoint(laneIndex, laneCount, ratio);
      return {
        key: `${index}-${point.x}-${point.row}`,
        x: polar.x,
        y: polar.y,
        row: laneIndex,
        ratio,
      };
    })
    .filter((point): point is { key: string; x: number; y: number; row: number; ratio: number } => point !== null);

  const averageRatioByLane = lanes.map((laneIndex) => {
    const lanePoints = scatterPoints.filter((point) => point.row === laneIndex);
    if (lanePoints.length === 0) {
      return 0;
    }

    const totalRatio = lanePoints.reduce((sum, point) => sum + point.ratio, 0);
    return totalRatio / lanePoints.length;
  });

  const averagePolygon = lanes
    .map((laneIndex) => {
      const polar = toPolarPoint(laneIndex, laneCount, averageRatioByLane[laneIndex]);
      return `${polar.x},${polar.y}`;
    })
    .join(' ');

  return (
    <div className="history-chart-card">
      <div className="history-chart-title">{title}</div>
      {points.length === 0 ? (
        <div className="history-chart-empty">暂无位置数据</div>
      ) : (
        <svg viewBox={`0 0 ${chartSize} ${chartSize}`} className="history-radar-chart" role="img">
          {[0.25, 0.5, 0.75, 1].map((ratio) => (
            <circle
              key={`ring-${ratio}`}
              cx={chartCenter}
              cy={chartCenter}
              r={minRadarRadius + (chartRadius - minRadarRadius) * ratio}
              className="history-radar-grid"
            />
          ))}

          {lanes.map((laneIndex) => {
            const outer = toPolarPoint(laneIndex, laneCount, 1);
            const label = toPolarPoint(laneIndex, laneCount, 1.08);
            return (
              <g key={`lane-${laneIndex}`}>
                <line
                  x1={chartCenter}
                  y1={chartCenter}
                  x2={outer.x}
                  y2={outer.y}
                  className="history-radar-spoke"
                />
                <text x={label.x} y={label.y} textAnchor="middle" dominantBaseline="middle" className="history-radar-label">
                  {laneIndex + 1}
                </text>
              </g>
            );
          })}

          <polygon points={averagePolygon} className="history-radar-polygon" />
          {scatterPoints.map((point) => (
            <circle key={point.key} cx={point.x} cy={point.y} r={2.5} className="history-radar-dot" />
          ))}
          <circle cx={chartCenter} cy={chartCenter} r={3} className="history-radar-center-dot" />
        </svg>
      )}
    </div>
  );
}

export default ZombiePositionRadarChart;
