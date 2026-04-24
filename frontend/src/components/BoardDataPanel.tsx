import { ReloadOutlined } from '@ant-design/icons';
import { Button, Tag, Tooltip } from 'antd';
import { useCallback, useEffect, useMemo, useState } from 'react';

type BoardDataPanelProps = {
  isInjected: boolean;
};

type BoardFieldSnapshot = {
  Name: string;
  Type: string;
  Static: boolean;
  Offset: number;
  Value: unknown;
};

type BoardSnapshot = {
  Class: string;
  Instance: string | null;
  Error?: string;
  Fields: BoardFieldSnapshot[];
};

type NumericBoardField = BoardFieldSnapshot & {
  Value: number;
};

type BooleanBoardField = BoardFieldSnapshot & {
  Value: boolean;
};

type PointerBoardField = BoardFieldSnapshot & {
  Value: string;
};

const boardDataRefreshIntervalMs = 1200;
const numericChartLimit = 12;
const referencePreviewLimit = 18;

const keyMetricFieldNames = [
  'theSun',
  'theMoney',
  'theWave',
  'theMaxWave',
  'enermyCount',
  'killZombieCount',
  'rowNum',
  'columnNum',
  'timeUntilNextWave',
  'waveInterval',
];

const fieldLabelMap: Record<string, string> = {
  theSun: '当前阳光',
  theMoney: '当前金币',
  theWave: '当前波次',
  theMaxWave: '最大波次',
  enermyCount: '场上僵尸',
  killZombieCount: '击杀数',
  rowNum: '行数',
  columnNum: '列数',
  timeUntilNextWave: '下波倒计时',
  waveInterval: '波次间隔',
  maxSun: '阳光上限',
  maxMoney: '金币上限',
  freeCD: '无 CD',
  randomCard: '随机卡槽',
  rightPutPot: '右键放罐子',
};

function resolveErrorMessage(error: unknown): string {
  if (error instanceof Error && error.message) {
    return error.message;
  }
  return String(error);
}

function formatDateTime(timestamp: number | null): string {
  if (timestamp === null) {
    return '--';
  }

  return new Date(timestamp).toLocaleString('zh-CN', {
    hour12: false,
  });
}

function isFiniteNumber(value: unknown): value is number {
  return typeof value === 'number' && Number.isFinite(value);
}

function isBoolean(value: unknown): value is boolean {
  return typeof value === 'boolean';
}

function isPointerLikeValue(value: unknown): value is string {
  return typeof value === 'string' && /^0x[0-9a-f]+$/i.test(value);
}

function isNullPointer(address: string): boolean {
  return /^0x0+$/i.test(address);
}

function formatAddressShort(address: string): string {
  if (address.length <= 14) {
    return address;
  }

  return `${address.slice(0, 8)}...${address.slice(-4)}`;
}

function formatNumericValue(value: number): string {
  if (Number.isInteger(value)) {
    return value.toLocaleString('zh-CN');
  }

  return value.toLocaleString('zh-CN', {
    maximumFractionDigits: 3,
  });
}

function formatScalarValue(value: unknown): string {
  if (value === null) {
    return '--';
  }

  if (isBoolean(value)) {
    return value ? '开启' : '关闭';
  }

  if (isFiniteNumber(value)) {
    return formatNumericValue(value);
  }

  if (typeof value === 'string') {
    return value;
  }

  if (typeof value === 'object') {
    try {
      return JSON.stringify(value);
    } catch {
      return '[对象]';
    }
  }

  return String(value);
}

function toFieldLabel(name: string): string {
  return fieldLabelMap[name] ?? name;
}

function normalizeBoardField(rawField: unknown): BoardFieldSnapshot | null {
  if (!rawField || typeof rawField !== 'object') {
    return null;
  }

  const candidate = rawField as Record<string, unknown>;
  const name = typeof candidate.Name === 'string' ? candidate.Name : '';
  if (name.length === 0) {
    return null;
  }

  return {
    Name: name,
    Type: typeof candidate.Type === 'string' ? candidate.Type : 'Unknown',
    Static: Boolean(candidate.Static),
    Offset: typeof candidate.Offset === 'number' ? candidate.Offset : -1,
    Value: candidate.Value ?? null,
  };
}

function normalizeBoardSnapshot(rawData: unknown): BoardSnapshot {
  if (!rawData || typeof rawData !== 'object') {
    return {
      Class: 'Board',
      Instance: null,
      Fields: [],
    };
  }

  const candidate = rawData as Record<string, unknown>;
  const fields = Array.isArray(candidate.Fields)
    ? candidate.Fields
      .map((field) => normalizeBoardField(field))
      .filter((field): field is BoardFieldSnapshot => field !== null)
    : [];

  return {
    Class: typeof candidate.Class === 'string' ? candidate.Class : 'Board',
    Instance: typeof candidate.Instance === 'string' ? candidate.Instance : null,
    Error: typeof candidate.Error === 'string' ? candidate.Error : undefined,
    Fields: fields,
  };
}

function BoardDataPanel({ isInjected }: BoardDataPanelProps) {
  const [snapshot, setSnapshot] = useState<BoardSnapshot | null>(null);
  const [loading, setLoading] = useState(false);
  const [manualRefreshing, setManualRefreshing] = useState(false);
  const [errorMessage, setErrorMessage] = useState('');
  const [lastUpdatedAt, setLastUpdatedAt] = useState<number | null>(null);

  const fetchBoardSnapshot = useCallback(async (manualRefresh: boolean) => {
    if (!isInjected) {
      return;
    }

    if (manualRefresh) {
      setManualRefreshing(true);
    }

    try {
      const rawResult = await 'GetBoardFields'.invoke();
      const parsedResult =
        typeof rawResult === 'string'
          ? normalizeBoardSnapshot(JSON.parse(rawResult) as unknown)
          : normalizeBoardSnapshot(rawResult);

      setSnapshot(parsedResult);
      setErrorMessage('');
      setLastUpdatedAt(Date.now());
    } catch (error) {
      setErrorMessage(resolveErrorMessage(error));
    } finally {
      setLoading(false);
      if (manualRefresh) {
        setManualRefreshing(false);
      }
    }
  }, [isInjected]);

  useEffect(() => {
    if (!isInjected) {
      setSnapshot(null);
      setErrorMessage('');
      setLastUpdatedAt(null);
      setLoading(false);
      setManualRefreshing(false);
      return;
    }

    setLoading(true);
    void fetchBoardSnapshot(false);
    const timer = window.setInterval(() => {
      void fetchBoardSnapshot(false);
    }, boardDataRefreshIntervalMs);

    return () => {
      window.clearInterval(timer);
    };
  }, [fetchBoardSnapshot, isInjected]);

  const fields = snapshot?.Fields ?? [];

  const numericFields = useMemo<NumericBoardField[]>(
    () => fields.filter((field): field is NumericBoardField => isFiniteNumber(field.Value)),
    [fields],
  );

  const booleanFields = useMemo<BooleanBoardField[]>(
    () => fields.filter((field): field is BooleanBoardField => isBoolean(field.Value)),
    [fields],
  );

  const pointerFields = useMemo<PointerBoardField[]>(
    () => fields.filter((field): field is PointerBoardField => isPointerLikeValue(field.Value)),
    [fields],
  );

  const keyMetrics = useMemo(() => {
    const fieldMap = new Map(fields.map((field) => [field.Name, field]));
    const selected: BoardFieldSnapshot[] = [];

    keyMetricFieldNames.forEach((name) => {
      const field = fieldMap.get(name);
      if (field && (isFiniteNumber(field.Value) || isBoolean(field.Value))) {
        selected.push(field);
      }
    });

    if (selected.length >= 8) {
      return selected.slice(0, 8);
    }

    numericFields.forEach((field) => {
      if (selected.some((item) => item.Name === field.Name)) {
        return;
      }
      selected.push(field);
    });

    return selected.slice(0, 8);
  }, [fields, numericFields]);

  const numericChartData = useMemo(
    () => [...numericFields]
      .sort((left, right) => Math.abs(right.Value) - Math.abs(left.Value))
      .slice(0, numericChartLimit),
    [numericFields],
  );

  const numericChartMaxAbs = useMemo(
    () => Math.max(1, ...numericChartData.map((field) => Math.abs(field.Value))),
    [numericChartData],
  );

  const trueBoolCount = useMemo(
    () => booleanFields.filter((field) => field.Value === true).length,
    [booleanFields],
  );

  const falseBoolCount = booleanFields.length - trueBoolCount;
  const trueBoolPercent = booleanFields.length === 0 ? 0 : (trueBoolCount / booleanFields.length) * 100;

  const livePointerCount = useMemo(
    () => pointerFields.filter((field) => !isNullPointer(field.Value)).length,
    [pointerFields],
  );

  const nullPointerCount = pointerFields.length - livePointerCount;

  const referencePreview = useMemo(
    () => [...pointerFields]
      .sort((left, right) => {
        const leftNull = isNullPointer(left.Value);
        const rightNull = isNullPointer(right.Value);
        if (leftNull !== rightNull) {
          return leftNull ? 1 : -1;
        }
        return left.Offset - right.Offset;
      })
      .slice(0, referencePreviewLimit),
    [pointerFields],
  );

  return (
    <aside className="board-data-panel">
      <div className="board-data-panel-header">
        <div>
          <div className="board-data-panel-title">游戏数据面板</div>
          <div className="board-data-panel-subtitle">Board 字段快照（1.2 秒自动刷新）</div>
        </div>
        <Button
          icon={<ReloadOutlined />}
          loading={manualRefreshing}
          onClick={() => {
            void fetchBoardSnapshot(true);
          }}
          size="small"
        >
          刷新
        </Button>
      </div>

      {!isInjected ? (
        <div className="board-data-empty">请先开启“插件注入”，再查看右侧游戏数据。</div>
      ) : null}

      {isInjected && (
        <>
          <div className="board-data-meta-grid">
            <div className="board-data-meta-card">
              <div className="board-data-meta-label">类名</div>
              <div className="board-data-meta-value">{snapshot?.Class ?? 'Board'}</div>
            </div>
            <div className="board-data-meta-card">
              <div className="board-data-meta-label">字段总数</div>
              <div className="board-data-meta-value">{fields.length}</div>
            </div>
            <div className="board-data-meta-card">
              <div className="board-data-meta-label">实例地址</div>
              <Tooltip title={snapshot?.Instance ?? '--'}>
                <div className="board-data-meta-value mono-text">
                  {snapshot?.Instance ? formatAddressShort(snapshot.Instance) : '--'}
                </div>
              </Tooltip>
            </div>
            <div className="board-data-meta-card">
              <div className="board-data-meta-label">更新时间</div>
              <div className="board-data-meta-value">{formatDateTime(lastUpdatedAt)}</div>
            </div>
          </div>

          {loading && snapshot === null ? (
            <div className="board-data-empty">正在读取游戏数据...</div>
          ) : null}

          {snapshot?.Error ? (
            <div className="board-data-error">接口返回错误：{snapshot.Error}</div>
          ) : null}

          {errorMessage ? (
            <div className="board-data-error">读取失败：{errorMessage}</div>
          ) : null}

          <section className="board-data-section">
            <div className="board-data-section-title">关键指标</div>
            <div className="board-metric-grid">
              {keyMetrics.length === 0 ? (
                <div className="board-data-empty board-data-empty-compact">暂无关键指标数据</div>
              ) : (
                keyMetrics.map((field) => (
                  <div className="board-metric-card" key={`${field.Name}-${field.Offset}`}>
                    <div className="board-metric-label">{toFieldLabel(field.Name)}</div>
                    <div className="board-metric-value">{formatScalarValue(field.Value)}</div>
                    <div className="board-metric-key">{field.Name}</div>
                  </div>
                ))
              )}
            </div>
          </section>

          <section className="board-data-section">
            <div className="board-data-section-title">数值分布图（Top 12）</div>
            <div className="board-numeric-chart">
              {numericChartData.length === 0 ? (
                <div className="board-data-empty board-data-empty-compact">暂无数值字段</div>
              ) : (
                numericChartData.map((field) => {
                  const numericValue = field.Value;
                  const widthPercent = (Math.abs(numericValue) / numericChartMaxAbs) * 100;

                  return (
                    <div className="board-numeric-row" key={`${field.Name}-${field.Offset}`}>
                      <div className="board-numeric-row-header">
                        <span>{toFieldLabel(field.Name)}</span>
                        <span>{formatNumericValue(numericValue)}</span>
                      </div>
                      <div className="board-numeric-track">
                        <div
                          className={`board-numeric-fill${numericValue < 0 ? ' negative' : ''}`}
                          style={{ width: `${Math.max(4, widthPercent)}%` }}
                        />
                      </div>
                    </div>
                  );
                })
              )}
            </div>
          </section>

          <section className="board-data-section">
            <div className="board-data-section-title">布尔开关状态</div>
            <div className="board-bool-summary">
              <div className="board-bool-track">
                <div className="board-bool-fill" style={{ width: `${trueBoolPercent}%` }} />
              </div>
              <div className="board-bool-labels">
                <span>开启 {trueBoolCount}</span>
                <span>关闭 {falseBoolCount}</span>
              </div>
            </div>
          </section>

          <section className="board-data-section">
            <div className="board-data-section-title">对象引用概览</div>
            <div className="board-reference-summary">
              <span>总引用 {pointerFields.length}</span>
              <span>已绑定 {livePointerCount}</span>
              <span>空引用 {nullPointerCount}</span>
            </div>
            <div className="board-reference-list">
              {referencePreview.length === 0 ? (
                <div className="board-data-empty board-data-empty-compact">暂无地址字段</div>
              ) : (
                referencePreview.map((field) => {
                  const address = field.Value;
                  const nullRef = isNullPointer(address);

                  return (
                    <div className="board-reference-item" key={`${field.Name}-${field.Offset}`}>
                      <div className="board-reference-head">
                        <div className="board-reference-name">{toFieldLabel(field.Name)}</div>
                        <div className="board-reference-tags">
                          <Tag color={field.Static ? 'processing' : 'default'}>
                            {field.Static ? '静态' : '实例'}
                          </Tag>
                          <Tag color={nullRef ? 'default' : 'success'}>
                            {nullRef ? '空引用' : '已绑定'}
                          </Tag>
                        </div>
                      </div>
                      <div className="board-reference-meta">{field.Type} · 偏移 {field.Offset}</div>
                      <Tooltip title={address}>
                        <div className="board-reference-address mono-text">
                          {nullRef ? '--' : formatAddressShort(address)}
                        </div>
                      </Tooltip>
                    </div>
                  );
                })
              )}
            </div>
          </section>
        </>
      )}
    </aside>
  );
}

export default BoardDataPanel;
