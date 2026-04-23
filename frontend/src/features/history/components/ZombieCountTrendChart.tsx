type TrendPoint = {
  timestamp: number;
  value: number;
};

type ZombieCountTrendChartProps = {
  title: string;
  data: TrendPoint[];
};

const chartWidth = 760;
const chartHeight = 240;
const paddingTop = 20;
const paddingRight = 18;
const paddingBottom = 32;
const paddingLeft = 36;

function formatTime(timestamp: number): string {
  const date = new Date(timestamp);
  return date.toLocaleTimeString('zh-CN', {
    hour12: false,
    hour: '2-digit',
    minute: '2-digit',
    second: '2-digit',
  });
}

function ZombieCountTrendChart({ title, data }: ZombieCountTrendChartProps) {
  const plotWidth = chartWidth - paddingLeft - paddingRight;
  const plotHeight = chartHeight - paddingTop - paddingBottom;
  const maxValue = Math.max(1, ...data.map((item) => item.value));
  const xStep = data.length > 1 ? plotWidth / (data.length - 1) : 0;

  const points = data.map((item, index) => {
    const x = paddingLeft + index * xStep;
    const y = paddingTop + ((maxValue - item.value) / maxValue) * plotHeight;
    return { x, y };
  });

  const linePoints = points.map((point) => `${point.x},${point.y}`).join(' ');
  const firstPoint = points.length > 0 ? points[0] : null;
  const lastPoint = points.length > 0 ? points[points.length - 1] : null;
  const areaPath =
    firstPoint && lastPoint
      ? `M ${firstPoint.x} ${paddingTop + plotHeight} L ${linePoints} L ${lastPoint.x} ${paddingTop + plotHeight} Z`
      : '';

  const yGridValues = [0, 0.25, 0.5, 0.75, 1];

  return (
    <div className="history-chart-card">
      <div className="history-chart-title">{title}</div>
      {data.length === 0 ? (
        <div className="history-chart-empty">暂无采样数据</div>
      ) : (
        <svg viewBox={`0 0 ${chartWidth} ${chartHeight}`} className="history-trend-chart" role="img">
          <defs>
            <linearGradient id="historyTrendFill" x1="0" y1="0" x2="0" y2="1">
              <stop offset="0%" stopColor="#2f7cff" stopOpacity="0.35" />
              <stop offset="100%" stopColor="#2f7cff" stopOpacity="0.04" />
            </linearGradient>
          </defs>
          {yGridValues.map((ratio) => {
            const y = paddingTop + ratio * plotHeight;
            const labelValue = Math.round((1 - ratio) * maxValue);
            return (
              <g key={ratio}>
                <line
                  x1={paddingLeft}
                  y1={y}
                  x2={paddingLeft + plotWidth}
                  y2={y}
                  stroke="#e6eaf2"
                  strokeWidth={1}
                />
                <text x={paddingLeft - 8} y={y + 4} className="history-trend-axis-label">
                  {labelValue}
                </text>
              </g>
            );
          })}
          {areaPath ? <path d={areaPath} fill="url(#historyTrendFill)" /> : null}
          <polyline points={linePoints} fill="none" stroke="#2f7cff" strokeWidth={3} />
          {lastPoint ? <circle cx={lastPoint.x} cy={lastPoint.y} r={4} fill="#2f7cff" /> : null}
          <text x={paddingLeft} y={chartHeight - 8} className="history-trend-axis-label">
            {formatTime(data[0].timestamp)}
          </text>
          <text x={paddingLeft + plotWidth / 2 - 28} y={chartHeight - 8} className="history-trend-axis-label">
            {formatTime(data[Math.floor(data.length / 2)].timestamp)}
          </text>
          <text x={paddingLeft + plotWidth - 64} y={chartHeight - 8} className="history-trend-axis-label">
            {formatTime(data[data.length - 1].timestamp)}
          </text>
        </svg>
      )}
    </div>
  );
}

export type { TrendPoint };
export default ZombieCountTrendChart;
