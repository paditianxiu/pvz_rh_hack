type HistogramBin = {
  label: string;
  count: number;
};

type ZombieCountHistogramChartProps = {
  title: string;
  bins: HistogramBin[];
};

function ZombieCountHistogramChart({ title, bins }: ZombieCountHistogramChartProps) {
  const maxCount = Math.max(1, ...bins.map((item) => item.count));

  return (
    <div className="history-chart-card">
      <div className="history-chart-title">{title}</div>
      {bins.length === 0 ? (
        <div className="history-chart-empty">暂无分布数据</div>
      ) : (
        <div className="history-histogram-wrap">
          <div className="history-histogram-bars">
            {bins.map((bin) => {
              const ratio = bin.count / maxCount;
              return (
                <div key={bin.label} className="history-histogram-item">
                  <div className="history-histogram-bar-box">
                    <div className="history-histogram-bar" style={{ height: `${Math.max(6, ratio * 100)}%` }}>
                      <span className="history-histogram-count">{bin.count}</span>
                    </div>
                  </div>
                  <div className="history-histogram-label" title={bin.label}>
                    {bin.label}
                  </div>
                </div>
              );
            })}
          </div>
        </div>
      )}
    </div>
  );
}

export type { HistogramBin };
export default ZombieCountHistogramChart;
