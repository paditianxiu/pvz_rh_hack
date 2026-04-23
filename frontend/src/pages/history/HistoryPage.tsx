import { Button } from 'antd';
import { useMemo } from 'react';
import ZombieCountDeltaBarChart, {
  type DeltaPoint,
} from '../../features/history/components/ZombieCountDeltaBarChart';
import ZombieCountHistogramChart, {
  type HistogramBin,
} from '../../features/history/components/ZombieCountHistogramChart';
import ZombieCountTrendChart, {
  type TrendPoint,
} from '../../features/history/components/ZombieCountTrendChart';
import ZombieLaneBarChart, {
  type LaneCount,
} from '../../features/history/components/ZombieLaneBarChart';
import ZombieLaneHeatmapChart, {
  type HeatmapColumn,
} from '../../features/history/components/ZombieLaneHeatmapChart';
import { type ZombieRowCounter, useZombieHistoryStats } from '../../features/history/useZombieHistoryStats';
import './HistoryPage.css';

const trendSampleLimit = 120;
const heatmapColumnLimit = 60;
const deltaSampleLimit = 80;
const histogramBinCount = 8;

function formatTimestamp(timestamp: number | null): string {
  if (timestamp === null) {
    return '--';
  }

  return new Date(timestamp).toLocaleString('zh-CN', {
    hour12: false,
  });
}

function toLaneCounts(rows: ZombieRowCounter): LaneCount[] {
  return Object.entries(rows)
    .map(([rowKey, count]) => ({
      row: Number.parseInt(rowKey, 10),
      count,
    }))
    .filter((item) => Number.isFinite(item.row))
    .sort((left, right) => left.row - right.row);
}

function toDeltaData(trendData: TrendPoint[]): DeltaPoint[] {
  if (trendData.length <= 1) {
    return [];
  }

  const deltas = trendData.slice(1).map((point, index) => ({
    timestamp: point.timestamp,
    delta: point.value - trendData[index].value,
  }));

  return deltas.slice(Math.max(0, deltas.length - deltaSampleLimit));
}

function toHistogramBins(samples: { total: number }[]): HistogramBin[] {
  if (samples.length === 0) {
    return [];
  }

  const values = samples.map((sample) => sample.total);
  const minValue = Math.min(...values);
  const maxValue = Math.max(...values);

  if (minValue === maxValue) {
    return [{ label: `${minValue}`, count: values.length }];
  }

  const range = maxValue - minValue + 1;
  const binSize = Math.max(1, Math.ceil(range / histogramBinCount));
  const binStartValues = Array.from({ length: histogramBinCount }, (_, index) => minValue + index * binSize)
    .filter((start) => start <= maxValue);

  return binStartValues.map((startValue, index) => {
    const endValue = index === binStartValues.length - 1 ? maxValue : Math.min(startValue + binSize - 1, maxValue);
    const count = values.filter((value) => value >= startValue && value <= endValue).length;
    return {
      label: startValue === endValue ? `${startValue}` : `${startValue}-${endValue}`,
      count,
    };
  });
}

function toHeatmapRows(samples: { rows: ZombieRowCounter }[]): number[] {
  const rowSet = new Set<number>();

  samples.forEach((sample) => {
    Object.keys(sample.rows).forEach((rowKey) => {
      const row = Number.parseInt(rowKey, 10);
      if (Number.isFinite(row)) {
        rowSet.add(row);
      }
    });
  });

  if (rowSet.size === 0) {
    return [0, 1, 2, 3, 4];
  }

  return Array.from(rowSet).sort((left, right) => left - right);
}

function toHeatmapColumns(samples: { timestamp: number; rows: ZombieRowCounter }[]): HeatmapColumn[] {
  return samples
    .slice(Math.max(0, samples.length - heatmapColumnLimit))
    .map((sample) => ({
      timestamp: sample.timestamp,
      rows: sample.rows,
    }));
}

function HistoryPage() {
  const {
    samples,
    latestSample,
    peakCount,
    averageCount,
    errorMessage,
    lastUpdatedAt,
    reset,
  } = useZombieHistoryStats();

  const trendData = useMemo<TrendPoint[]>(
    () =>
      samples.slice(Math.max(0, samples.length - trendSampleLimit)).map((sample) => ({
        timestamp: sample.timestamp,
        value: sample.total,
      })),
    [samples],
  );

  const deltaData = useMemo(() => toDeltaData(trendData), [trendData]);
  const histogramBins = useMemo(() => toHistogramBins(samples), [samples]);
  const currentLaneData = useMemo(() => toLaneCounts(latestSample?.rows ?? {}), [latestSample]);
  const heatmapRows = useMemo(() => toHeatmapRows(samples), [samples]);
  const heatmapColumns = useMemo(() => toHeatmapColumns(samples), [samples]);

  const currentCount = latestSample?.total ?? 0;

  return (
    <section className="settings-section">
      <div className="history-header">
        <div>
          <h2 className="section-title">对局记录</h2>
          <div className="history-subtitle">实时采样僵尸数量并生成统计图表</div>
        </div>
        <Button onClick={reset}>清空统计</Button>
      </div>

      <div className="history-summary-grid">
        <div className="history-summary-card">
          <div className="history-summary-label">实时僵尸数</div>
          <div className="history-summary-value">{currentCount}</div>
        </div>
        <div className="history-summary-card">
          <div className="history-summary-label">峰值僵尸数</div>
          <div className="history-summary-value">{peakCount}</div>
        </div>
        <div className="history-summary-card">
          <div className="history-summary-label">平均僵尸数</div>
          <div className="history-summary-value">{averageCount}</div>
        </div>
        <div className="history-summary-card">
          <div className="history-summary-label">采样点数</div>
          <div className="history-summary-value">{samples.length}</div>
        </div>
      </div>

      <div className="history-status-row">
        <span>最后采样时间：{formatTimestamp(lastUpdatedAt)}</span>
        {errorMessage ? <span className="history-status-error">采样失败：{errorMessage}</span> : null}
      </div>

      <div className="history-chart-grid">
        <ZombieCountTrendChart title="僵尸数量趋势（最近 120 个采样点）" data={trendData} />
        <ZombieLaneBarChart title="当前各路僵尸数" data={currentLaneData} />
        <ZombieCountDeltaBarChart title="僵尸数量变化（相邻采样差值）" data={deltaData} />
        <ZombieCountHistogramChart title="僵尸数量分布直方图" bins={histogramBins} />
        <ZombieLaneHeatmapChart
          title="各路僵尸热力图（最近采样）"
          rows={heatmapRows}
          columns={heatmapColumns}
        />
      </div>
    </section>
  );
}

export default HistoryPage;
