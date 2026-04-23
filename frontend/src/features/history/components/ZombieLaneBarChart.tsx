type LaneCount = {
  row: number;
  count: number;
};

type ZombieLaneBarChartProps = {
  title: string;
  data: LaneCount[];
  emptyText?: string;
};

function ZombieLaneBarChart({ title, data, emptyText = '暂无数据' }: ZombieLaneBarChartProps) {
  const maxCount = Math.max(1, ...data.map((item) => item.count));

  return (
    <div className="history-chart-card">
      <div className="history-chart-title">{title}</div>
      {data.length === 0 ? (
        <div className="history-chart-empty">{emptyText}</div>
      ) : (
        <div className="history-lane-chart">
          {data.map((item) => {
            const ratio = item.count / maxCount;
            return (
              <div key={`${title}-${item.row}`} className="history-lane-row">
                <span className="history-lane-label">第 {item.row + 1} 路</span>
                <div className="history-lane-track">
                  <div className="history-lane-fill" style={{ width: `${Math.max(ratio * 100, 1)}%` }} />
                </div>
                <span className="history-lane-value">{item.count}</span>
              </div>
            );
          })}
        </div>
      )}
    </div>
  );
}

export type { LaneCount };
export default ZombieLaneBarChart;
