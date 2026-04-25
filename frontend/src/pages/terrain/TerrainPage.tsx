import { ReloadOutlined } from '@ant-design/icons';
import { Button, message, Select } from 'antd';
import { type CSSProperties, useCallback, useEffect, useMemo, useState } from 'react';
import './TerrainPage.css';

type TerrainPageProps = {
  isInjected: boolean;
};

type BoardField = {
  Name: string;
  Value: unknown;
};

type BoardSnapshot = {
  Fields: BoardField[];
};

type GridCell = {
  id: string;
  row: number;
  column: number;
};

type TerrainContextMenuState = {
  x: number;
  y: number;
  cell: GridCell;
};

type PlantListItem = {
  name: string;
  value: number;
};

const boardRefreshIntervalMs = 1200;
const maxGridCellCount = 2000;
const contextMenuWidthPx = 198;
const contextMenuHeightPx = 164;
const plantSelectionStorageKey = 'terrain:lastPlantType';

function resolveErrorMessage(error: unknown): string {
  if (error instanceof Error && error.message) {
    return error.message;
  }
  return String(error);
}

function toPositiveInteger(value: unknown): number | null {
  if (typeof value === 'number' && Number.isFinite(value)) {
    const parsed = Math.trunc(value);
    return parsed > 0 ? parsed : null;
  }

  if (typeof value === 'string') {
    const parsed = Number.parseInt(value, 10);
    if (Number.isFinite(parsed) && parsed > 0) {
      return parsed;
    }
  }

  return null;
}

function toInteger(value: unknown): number | null {
  if (typeof value === 'number' && Number.isFinite(value)) {
    return Math.trunc(value);
  }

  if (typeof value === 'string') {
    const parsed = Number.parseInt(value, 10);
    if (Number.isFinite(parsed)) {
      return parsed;
    }
  }

  return null;
}

function normalizeBoardSnapshot(rawData: unknown): BoardSnapshot {
  if (!rawData || typeof rawData !== 'object') {
    return { Fields: [] };
  }

  const candidate = rawData as Record<string, unknown>;
  const fields = Array.isArray(candidate.Fields)
    ? candidate.Fields
      .filter((item): item is Record<string, unknown> => !!item && typeof item === 'object')
      .map((item) => ({
        Name: typeof item.Name === 'string' ? item.Name : '',
        Value: item.Value ?? null,
      }))
      .filter((item) => item.Name.length > 0)
    : [];

  return { Fields: fields };
}

function normalizePlantList(rawData: unknown): PlantListItem[] {
  if (!rawData || typeof rawData !== 'object') {
    return [];
  }

  const candidate = rawData as Record<string, unknown>;
  if (!Array.isArray(candidate.Items)) {
    return [];
  }

  return candidate.Items
    .filter((item): item is Record<string, unknown> => !!item && typeof item === 'object')
    .map((item) => {
      const name = typeof item.name === 'string' ? item.name : '';
      const value = toInteger(item.value);
      if (name.length === 0 || value === null) {
        return null;
      }
      return { name, value };
    })
    .filter((item): item is PlantListItem => item !== null);
}

function readRememberedPlantValue(): number | null {
  if (typeof window === 'undefined') {
    return null;
  }

  try {
    const rawValue = window.localStorage.getItem(plantSelectionStorageKey);
    if (rawValue === null) {
      return null;
    }
    return toInteger(rawValue);
  } catch {
    return null;
  }
}

function resolvePlantValue(items: PlantListItem[], preferredValue: number | null): number | null {
  if (preferredValue !== null && items.some((item) => item.value === preferredValue)) {
    return preferredValue;
  }

  const fallback = items.find((item) => item.value >= 0) ?? items[0];
  return fallback ? fallback.value : null;
}

function formatDateTime(timestamp: number | null): string {
  if (timestamp === null) {
    return '--';
  }

  return new Date(timestamp).toLocaleString('zh-CN', {
    hour12: false,
  });
}

function TerrainPage({ isInjected }: TerrainPageProps) {
  const [rowCount, setRowCount] = useState<number>(0);
  const [columnCount, setColumnCount] = useState<number>(0);
  const [loading, setLoading] = useState(false);
  const [manualRefreshing, setManualRefreshing] = useState(false);
  const [errorMessage, setErrorMessage] = useState('');
  const [lastUpdatedAt, setLastUpdatedAt] = useState<number | null>(null);
  const [selectedCellId, setSelectedCellId] = useState('');
  const [contextMenu, setContextMenu] = useState<TerrainContextMenuState | null>(null);
  const [plantOptions, setPlantOptions] = useState<PlantListItem[]>([]);
  const [plantLoading, setPlantLoading] = useState(false);
  const [plantLoadingError, setPlantLoadingError] = useState('');
  const [planting, setPlanting] = useState(false);
  const [selectedPlantValue, setSelectedPlantValue] = useState<number | null>(() => readRememberedPlantValue());

  const closeContextMenu = useCallback(() => {
    setContextMenu(null);
  }, []);

  const fetchBoardDimensions = useCallback(async (manualRefresh: boolean) => {
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

      const rowField = parsedResult.Fields.find((field) => field.Name === 'rowNum');
      const columnField = parsedResult.Fields.find((field) => field.Name === 'columnNum');
      const nextRowCount = toPositiveInteger(rowField?.Value);
      const nextColumnCount = toPositiveInteger(columnField?.Value);

      if (!nextRowCount || !nextColumnCount) {
        throw new Error('未在 GetBoardFields 中解析到有效的 rowNum / columnNum');
      }

      setRowCount(nextRowCount);
      setColumnCount(nextColumnCount);
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

  const fetchPlantOptions = useCallback(async () => {
    if (!isInjected) {
      return;
    }

    setPlantLoading(true);
    setPlantLoadingError('');
    try {
      const rawResult = await 'GetPlantList'.invoke();
      const parsedResult =
        typeof rawResult === 'string'
          ? normalizePlantList(JSON.parse(rawResult) as unknown)
          : normalizePlantList(rawResult);

      if (parsedResult.length === 0) {
        throw new Error('GetPlantList 未返回可用植物列表');
      }

      setPlantOptions(parsedResult);
      setSelectedPlantValue((prev) => resolvePlantValue(parsedResult, prev ?? readRememberedPlantValue()));
    } catch (error) {
      setPlantLoadingError(resolveErrorMessage(error));
    } finally {
      setPlantLoading(false);
    }
  }, [isInjected]);

  useEffect(() => {
    if (!isInjected) {
      setRowCount(0);
      setColumnCount(0);
      setErrorMessage('');
      setLastUpdatedAt(null);
      setSelectedCellId('');
      setLoading(false);
      setManualRefreshing(false);
      closeContextMenu();
      setPlantOptions([]);
      setPlantLoading(false);
      setPlantLoadingError('');
      setPlanting(false);
      return;
    }

    setLoading(true);
    void fetchBoardDimensions(false);
    void fetchPlantOptions();
    const timer = window.setInterval(() => {
      void fetchBoardDimensions(false);
    }, boardRefreshIntervalMs);

    return () => {
      window.clearInterval(timer);
    };
  }, [closeContextMenu, fetchBoardDimensions, fetchPlantOptions, isInjected]);

  useEffect(() => {
    if (selectedPlantValue === null || typeof window === 'undefined') {
      return;
    }

    try {
      window.localStorage.setItem(plantSelectionStorageKey, String(selectedPlantValue));
    } catch {
    }
  }, [selectedPlantValue]);

  const totalCellCount = rowCount * columnCount;
  const exceedsCellLimit = totalCellCount > maxGridCellCount;

  const cells = useMemo<GridCell[]>(() => {
    if (rowCount <= 0 || columnCount <= 0 || exceedsCellLimit) {
      return [];
    }

    const result: GridCell[] = [];
    result.length = rowCount * columnCount;

    let index = 0;
    for (let row = 1; row <= rowCount; row += 1) {
      for (let column = 1; column <= columnCount; column += 1) {
        result[index] = {
          id: `${row}-${column}`,
          row,
          column,
        };
        index += 1;
      }
    }

    return result;
  }, [columnCount, exceedsCellLimit, rowCount]);

  useEffect(() => {
    if (cells.length === 0) {
      setSelectedCellId('');
      closeContextMenu();
      return;
    }

    const validCellIds = new Set(cells.map((cell) => cell.id));
    setSelectedCellId((prev) => (prev !== '' && validCellIds.has(prev) ? prev : ''));
  }, [cells, closeContextMenu]);

  useEffect(() => {
    if (!contextMenu) {
      return;
    }

    const handlePointerDown = (event: MouseEvent) => {
      const target = event.target as HTMLElement | null;
      if (target?.closest('.terrain-context-menu')) {
        return;
      }
      closeContextMenu();
    };

    const handleKeyDown = (event: KeyboardEvent) => {
      if (event.key === 'Escape') {
        closeContextMenu();
      }
    };

    window.addEventListener('pointerdown', handlePointerDown);
    window.addEventListener('keydown', handleKeyDown);
    window.addEventListener('resize', closeContextMenu);
    window.addEventListener('scroll', closeContextMenu, true);

    return () => {
      window.removeEventListener('pointerdown', handlePointerDown);
      window.removeEventListener('keydown', handleKeyDown);
      window.removeEventListener('resize', closeContextMenu);
      window.removeEventListener('scroll', closeContextMenu, true);
    };
  }, [closeContextMenu, contextMenu]);

  const handleCreatePlantAtCell = useCallback(async (cell: GridCell) => {
    if (selectedPlantValue === null) {
      message.warning('请选择要种植的植物');
      return;
    }

    const newColumn = cell.column - 1;
    const newRow = cell.row - 1;
    setPlanting(true);
    try {
      await 'CreatePlant'.invoke(newColumn, newRow, selectedPlantValue);
      const plantName = plantOptions.find((item) => item.value === selectedPlantValue)?.name ?? String(selectedPlantValue);
      message.success(`已在第 ${cell.row} 行，第 ${cell.column} 列种植 ${plantName}`);
    } catch (error) {
      message.error(`种植失败：${resolveErrorMessage(error)}`);
    } finally {
      setPlanting(false);
    }
  }, [plantOptions, selectedPlantValue]);



  const contextMenuStyle = useMemo<CSSProperties | null>(() => {
    if (!contextMenu) {
      return null;
    }

    if (typeof window === 'undefined') {
      return {
        left: contextMenu.x,
        top: contextMenu.y,
      };
    }

    const viewportPadding = 8;
    const maxLeft = Math.max(viewportPadding, window.innerWidth - contextMenuWidthPx - viewportPadding);
    const maxTop = Math.max(viewportPadding, window.innerHeight - contextMenuHeightPx - viewportPadding);

    return {
      left: Math.min(Math.max(viewportPadding, contextMenu.x), maxLeft),
      top: Math.min(Math.max(viewportPadding, contextMenu.y), maxTop),
    };
  }, [contextMenu]);

  const handleContextAction = useCallback(async (action: string) => {
    if (!contextMenu) {
      return;
    }

    if (planting) {
      message.warning('正在处理上一次种植，请稍候');
      closeContextMenu();
      return;
    }

    const cell = contextMenu.cell;
    if (action === 'SetPit') {
      "SetPit".invoke(cell.column - 1, cell.row - 1)
      closeContextMenu();
      return;
    }

    if (action === 'CreateFireLine') {
      "CreateFireLine".invoke(cell.row - 1)
      closeContextMenu();
      return;
    }

    if (action === 'createPlant') {
      if (plantLoading) {
        message.warning('植物列表加载中，请稍后再试');
        closeContextMenu();
        return;
      }

      if (plantOptions.length === 0) {
        if (plantLoadingError) {
          message.error(`植物列表不可用：${plantLoadingError}`);
        } else {
          message.warning('植物列表为空，请先刷新植物列表');
        }
        closeContextMenu();
        return;
      }

      await handleCreatePlantAtCell(cell);
      closeContextMenu();
      return;
    }
  }, [closeContextMenu, contextMenu, handleCreatePlantAtCell, plantLoading, plantLoadingError, plantOptions.length, planting]);

  return (
    <section className="settings-section">
      <div className="terrain-header">
        <div>
          <h2 className="section-title">地形操作</h2>
          <div className="terrain-subtitle">可以实现地形的一些操作</div>
        </div>
        <Button
          icon={<ReloadOutlined />}
          loading={manualRefreshing}
          onClick={() => {
            void fetchBoardDimensions(true);
          }}
        >
          刷新地形
        </Button>
      </div>

      {!isInjected ? (
        <div className="terrain-empty">请先在“常用功能”中启用插件注入。</div>
      ) : null}

      {isInjected && (
        <>
          <div className="terrain-summary-grid">
            <div className="terrain-summary-card">
              <div className="terrain-summary-label">行数 rowNum</div>
              <div className="terrain-summary-value">{rowCount || '--'}</div>
            </div>
            <div className="terrain-summary-card">
              <div className="terrain-summary-label">列数 columnNum</div>
              <div className="terrain-summary-value">{columnCount || '--'}</div>
            </div>
            <div className="terrain-summary-card">
              <div className="terrain-summary-label">格子总数</div>
              <div className="terrain-summary-value">{totalCellCount || '--'}</div>
            </div>
            <div className="terrain-summary-card">
              <div className="terrain-summary-label">更新时间</div>
              <div className="terrain-summary-value terrain-summary-time">{formatDateTime(lastUpdatedAt)}</div>
            </div>
          </div>

          {loading && rowCount === 0 && columnCount === 0 ? (
            <div className="terrain-empty">正在读取棋盘数据...</div>
          ) : null}

          {errorMessage ? (
            <div className="terrain-error">读取失败：{errorMessage}</div>
          ) : null}

          {exceedsCellLimit ? (
            <div className="terrain-error">
              当前格子数 {totalCellCount} 超过渲染上限 {maxGridCellCount}，请降低棋盘规模后再查看。
            </div>
          ) : null}

          {!exceedsCellLimit && cells.length > 0 ? (
            <div className="terrain-grid-card">
              <div className="terrain-grid-toolbar">
                <span>快捷功能</span>
                <Button onClick={async () => {
                  for (let row = 0; row < rowCount; row++) {
                    for (let column = 0; column < columnCount; column++) {
                      await 'CreatePlant'.invoke(column, row, selectedPlantValue);
                    }
                  }
                }}>全屏种植物🌳</Button>
                <Button onClick={() => {
                  for (let row = 0; row < rowCount; row++) {
                    for (let column = 0; column < columnCount; column++) {
                      "SetPit".invoke(column, row)
                    }
                  }
                }}>全屏种坑</Button>

                <Button onClick={() => {
                  for (let row = 0; row < rowCount; row++) {
                    "CreateFireLine".invoke(row)
                  }
                }}>全屏放火🔥</Button>
              </div>

              <div className="terrain-plant-toolbar">
                <span className="terrain-plant-label">种植植物</span>
                <Select<number>
                  loading={plantLoading}
                  onChange={(value) => {
                    setSelectedPlantValue(value);
                  }}
                  optionFilterProp="label"
                  options={plantOptions.map((item) => ({
                    value: item.value,
                    label: `${item.name} (${item.value})`,
                  }))}
                  placeholder="请选择植物"
                  showSearch
                  style={{ minWidth: 280, flex: '1 1 260px' }}
                  value={selectedPlantValue ?? undefined}
                />
                <Button
                  loading={plantLoading}
                  onClick={() => {
                    void fetchPlantOptions();
                  }}
                >
                  刷新植物列表
                </Button>
              </div>

              {plantLoadingError ? (
                <div className="terrain-error terrain-plant-error">
                  获取植物列表失败：{plantLoadingError}
                </div>
              ) : null}

              <div className="terrain-grid-scroll">
                <div
                  className="terrain-grid"
                  style={{ gridTemplateColumns: `repeat(${columnCount}, minmax(36px, 1fr))` }}
                >
                  {cells.map((cell) => {
                    const isSelected = selectedCellId === cell.id;

                    return (
                      <button
                        key={cell.id}
                        className={`terrain-cell${isSelected ? ' selected' : ''}`}
                        onClick={() => {
                          setSelectedCellId(cell.id);
                          closeContextMenu();
                        }}
                        onContextMenu={(event) => {
                          event.preventDefault();
                          setSelectedCellId(cell.id);
                          setContextMenu({
                            x: event.clientX,
                            y: event.clientY,
                            cell,
                          });
                        }}
                        type="button"
                        title={`第 ${cell.row} 行，第 ${cell.column} 列`}
                      >
                        <span className="terrain-cell-row-tag">R{cell.row}</span>
                        <span className="terrain-cell-column-tag">C{cell.column}</span>
                        <span className="terrain-cell-center">{cell.row}-{cell.column}</span>
                      </button>
                    );
                  })}
                </div>
              </div>
            </div>
          ) : null}

          {contextMenu && contextMenuStyle ? (
            <div
              className="terrain-context-menu"
              onContextMenu={(event) => {
                event.preventDefault();
              }}
              style={contextMenuStyle}
            >
              <div className="terrain-context-title">
                第 {contextMenu.cell.row} 行 · 第 {contextMenu.cell.column} 列
              </div>

              <button
                className="terrain-context-item"
                onClick={() => {
                  void handleContextAction('createPlant');
                }}
                type="button"
              >
                种植植物
              </button>
              <button
                className="terrain-context-item"
                onClick={() => {
                  void handleContextAction('SetPit');
                }}
                type="button"
              >
                放置坑洞
              </button>
              <button
                className="terrain-context-item"
                onClick={() => {
                  void handleContextAction('CreateFireLine');
                }}
                type="button"
              >
                放火🔥
              </button>
            </div>
          ) : null}
        </>
      )}
    </section>
  );
}

export default TerrainPage;
