type HeatmapColumn = {
  timestamp: number;
  rows: Record<number, number>;
};

type ZombieLaneHeatmapChartProps = {
  title: string;
  rows: number[];
  columns: HeatmapColumn[];
};

function getHeatColor(ratio: number): string {
  const normalized = Math.max(0, Math.min(ratio, 1));
  const hue = 210 - normalized * 170;
  const saturation = 78;
  const lightness = 92 - normalized * 44;
  return `hsl(${hue}, ${saturation}%, ${lightness}%)`;
}

function ZombieLaneHeatmapChart({ title, rows, columns }: ZombieLaneHeatmapChartProps) {
  const maxCellValue = Math.max(
    1,
    ...columns.flatMap((column) => rows.map((row) => column.rows[row] ?? 0)),
  );

  return (
    <div className="history-chart-card">
      <div className="history-chart-title">{title}</div>
      {rows.length === 0 || columns.length === 0 ? (
        <div className="history-chart-empty">暂无热力数据</div>
      ) : (
        <div className="history-heatmap-root">
          <div className="history-heatmap-grid" style={{ gridTemplateColumns: `76px repeat(${columns.length}, 1fr)` }}>
            {rows.map((row) => (
              <div key={`row-${row}`} className="history-heatmap-row-wrapper">
                <div className="history-heatmap-row-label">{`第 ${row + 1} 路`}</div>
                {columns.map((column, index) => {
                  const value = column.rows[row] ?? 0;
                  const ratio = value / maxCellValue;
                  const color = getHeatColor(ratio);
                  return (
                    <div
                      key={`cell-${row}-${column.timestamp}-${index}`}
                      className="history-heatmap-cell"
                      style={{ backgroundColor: color }}
                      title={`第 ${row + 1} 路: ${value}`}
                    />
                  );
                })}
              </div>
            ))}
          </div>
          <div className="history-heatmap-legend">
            <span>低密度</span>
            <div className="history-heatmap-legend-gradient" />
            <span>高密度</span>
          </div>
        </div>
      )}
    </div>
  );
}

export type { HeatmapColumn };
export default ZombieLaneHeatmapChart;
