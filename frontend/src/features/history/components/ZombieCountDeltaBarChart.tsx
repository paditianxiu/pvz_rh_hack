type DeltaPoint = {
  timestamp: number;
  delta: number;
};

type ZombieCountDeltaBarChartProps = {
  title: string;
  data: DeltaPoint[];
};

const chartWidth = 760;
const chartHeight = 240;
const paddingTop = 20;
const paddingRight = 18;
const paddingBottom = 30;
const paddingLeft = 36;

function ZombieCountDeltaBarChart({ title, data }: ZombieCountDeltaBarChartProps) {
  const plotWidth = chartWidth - paddingLeft - paddingRight;
  const plotHeight = chartHeight - paddingTop - paddingBottom;
  const baselineY = paddingTop + plotHeight / 2;
  const maxAbsDelta = Math.max(1, ...data.map((item) => Math.abs(item.delta)));
  const barGap = 2;
  const barWidth = data.length > 0 ? Math.max((plotWidth - (data.length - 1) * barGap) / data.length, 1) : 1;

  return (
    <div className="history-chart-card">
      <div className="history-chart-title">{title}</div>
      {data.length === 0 ? (
        <div className="history-chart-empty">暂无变化数据</div>
      ) : (
        <svg viewBox={`0 0 ${chartWidth} ${chartHeight}`} className="history-trend-chart" role="img">
          <line
            x1={paddingLeft}
            y1={baselineY}
            x2={paddingLeft + plotWidth}
            y2={baselineY}
            stroke="#c8d0df"
            strokeWidth={1.5}
          />
          {data.map((item, index) => {
            const ratio = Math.abs(item.delta) / maxAbsDelta;
            const barHeight = ratio * (plotHeight / 2);
            const x = paddingLeft + index * (barWidth + barGap);
            const y = item.delta >= 0 ? baselineY - barHeight : baselineY;
            const fill = item.delta >= 0 ? '#2f7cff' : '#ff7875';

            return (
              <rect
                key={`${item.timestamp}-${index}`}
                x={x}
                y={y}
                width={barWidth}
                height={Math.max(barHeight, 1)}
                rx={1}
                fill={fill}
              >
                <title>{`变化: ${item.delta > 0 ? '+' : ''}${item.delta}`}</title>
              </rect>
            );
          })}
          <text x={paddingLeft - 10} y={baselineY - plotHeight / 2 + 4} className="history-trend-axis-label">
            +{maxAbsDelta}
          </text>
          <text x={paddingLeft - 10} y={baselineY + 4} className="history-trend-axis-label">
            0
          </text>
          <text x={paddingLeft - 10} y={baselineY + plotHeight / 2 + 4} className="history-trend-axis-label">
            -{maxAbsDelta}
          </text>
        </svg>
      )}
    </div>
  );
}

export type { DeltaPoint };
export default ZombieCountDeltaBarChart;
