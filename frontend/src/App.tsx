import {
  CodepenOutlined,
  DatabaseOutlined,
  ExperimentOutlined,
  InfoCircleOutlined,
  SettingOutlined,
  ToolOutlined,
} from '@ant-design/icons';
import { Call, Events } from '@wailsio/runtime';
import { Button, InputNumber, Switch, message } from 'antd';
import type { ReactNode } from 'react';
import { useEffect, useRef, useState } from 'react';
import { NavLink, Navigate, Route, Routes, useLocation } from 'react-router-dom';
import { GetProcessPID, InjectDLL, UnloadDLL } from '../bindings/changeme/processservice';
import { overlayVisibilityEventName, overlayVisibilityStorageKey } from './constants/overlay';
import OverlayPage from './OverlayPage';
import SettingRow from './components/SettingRow';
import HistoryPage from './pages/history/HistoryPage';
import TerrainPage from './pages/terrain/TerrainPage';
import './App.css';

type NavItem = {
  path: string;
  label: string;
  icon: ReactNode;
};

type PlaceholderPageProps = {
  title: string;
  description: string;
};



type CheatToggleConfig = {
  key: string;
  label: string;
  invokeMethod: string;
};

const cheatToggleConfigs: readonly CheatToggleConfig[] = [
  {
    key: 'freeCd',
    label: '无 CD',
    invokeMethod: 'SetFreeCD',
  },
  {
    key: 'randomCard',
    label: '随机卡槽',
    invokeMethod: 'SetRandomCard',
  }
];

const initialCheatToggleState: Record<string, boolean> = {
  freeCd: false,
  randomCard: false,
};

const processServiceNameCandidates = [
  'main.ProcessService.SetOverlayVisible',
  'changeme.ProcessService.SetOverlayVisible',
];

const syncOverlayMethodCandidates = [
  'main.ProcessService.SyncOverlayWindow',
  'changeme.ProcessService.SyncOverlayWindow',
];

function PlaceholderPage({ title, description }: PlaceholderPageProps) {
  return (
    <section className="settings-section">
      <h2 className="section-title">{title}</h2>
      <div className="settings-card">
        <SettingRow label="暂未实现" description={description} />
      </div>
    </section>
  );
}

function App() {
  const location = useLocation();
  const gameName = 'PlantsVsZombiesRH.exe';
  const defaultSunValue = 9999;
  const isDebug = true;
  const dllPath = isDebug ? 'D:/Application/Code/Wails/pvz_rh_hack/payload/MyDLL.dll' : '';

  const navItems: NavItem[] = [
    { path: '/clipboard', label: '常用功能', icon: <CodepenOutlined /> },
    { path: '/history', label: '僵尸数据', icon: <ExperimentOutlined /> },
    { path: '/general', label: '地形操作', icon: <SettingOutlined /> },
    { path: '/shortcuts', label: '快捷操作', icon: <ToolOutlined /> },
    { path: '/backup', label: '存档备份', icon: <DatabaseOutlined /> },
    { path: '/about', label: '关于工具', icon: <InfoCircleOutlined /> },
  ];

  const [isInjected, setIsInjected] = useState(false);
  const [injecting, setInjecting] = useState(false);
  const [overlayVisible, setOverlayVisible] = useState(false);
  const [overlayLoading, setOverlayLoading] = useState(false);
  const [cheatToggles, setCheatToggles] = useState<Record<string, boolean>>(initialCheatToggleState);
  const [cheatToggleLoading, setCheatToggleLoading] = useState<Record<string, boolean>>(
    initialCheatToggleState,
  );
  const [sunValue, setSunValue] = useState<number>(defaultSunValue);
  const syncingOverlayRef = useRef(false);

  const resolveErrorMessage = (error: unknown): string => {
    if (error instanceof Error && error.message) {
      return error.message;
    }
    return String(error);
  };

  const handleInjectSwitchChange = async (checked: boolean) => {
    setInjecting(true);
    try {
      const pid = await GetProcessPID(gameName);
      if (checked) {
        await InjectDLL(pid, dllPath);
        message.success('注入成功');
      } else {
        await UnloadDLL(pid, dllPath);
        message.success('卸载成功');
      }
      setIsInjected(checked);
    } catch (error) {
      message.error(`操作失败：${resolveErrorMessage(error)}`);
    } finally {
      setInjecting(false);
    }
  };

  const handleCheatToggleChange = (config: CheatToggleConfig) => async (checked: boolean) => {
    setCheatToggleLoading((prev) => ({ ...prev, [config.key]: true }));
    try {
      await config.invokeMethod.invoke(checked);
      setCheatToggles((prev) => ({ ...prev, [config.key]: checked }));
      message.success(`${config.label}${checked ? '已启用' : '已关闭'}`);
    } catch (error) {
      message.error(`设置失败：${resolveErrorMessage(error)}`);
    } finally {
      setCheatToggleLoading((prev) => ({ ...prev, [config.key]: false }));
    }
  };

  const handleSetSun = async () => {
    const valueToSet = Math.trunc(sunValue);
    if (valueToSet < 0 || valueToSet > 2147483647) {
      message.error('阳光数值范围应在 0 ~ 2147483647');
      return;
    }

    try {
      await 'SetSun'.invoke(valueToSet);
      message.success(`阳光已设置为 ${valueToSet}`);
    } catch (error) {
      message.error(`设置失败：${resolveErrorMessage(error)}`);
    }
  };

  const callFirstAvailable = async (candidates: string[], ...args: unknown[]) => {
    let lastError: unknown;

    for (const methodName of candidates) {
      try {
        await Call.ByName(methodName, ...args);
        return;
      } catch (error: unknown) {
        const errorName =
          typeof error === 'object' && error !== null && 'name' in error
            ? String((error as { name: unknown }).name)
            : '';

        if (errorName === 'ReferenceError') {
          lastError = error;
          continue;
        }

        throw error;
      }
    }

    throw lastError ?? new Error('未找到可用的 ProcessService 方法');
  };

  const syncOverlayWindow = async () => {
    if (syncingOverlayRef.current) {
      return;
    }

    syncingOverlayRef.current = true;
    try {
      await callFirstAvailable(syncOverlayMethodCandidates, gameName);
    } finally {
      syncingOverlayRef.current = false;
    }
  };

  const handleOverlayVisibleChange = async (checked: boolean) => {
    setOverlayLoading(true);
    try {
      await callFirstAvailable(processServiceNameCandidates, gameName, checked);
      setOverlayVisible(checked);
      message.success(`僵尸射线${checked ? '已显示' : '已隐藏'}`);
      if (checked) {
        await syncOverlayWindow();
      }
    } catch (error) {
      message.error(`设置失败：${resolveErrorMessage(error)}`);
    } finally {
      setOverlayLoading(false);
    }
  };

  useEffect(() => {
    try {
      localStorage.setItem(overlayVisibilityStorageKey, overlayVisible ? '1' : '0');
    } catch {
    }

    void Events.Emit(overlayVisibilityEventName, overlayVisible);
  }, [overlayVisible]);

  useEffect(() => {
    if (!overlayVisible) {
      return;
    }

    void syncOverlayWindow();
    const timer = window.setInterval(() => {
      void syncOverlayWindow();
    }, 500);

    return () => {
      window.clearInterval(timer);
    };
  }, [overlayVisible]);

  if (location.pathname === '/overlay') {
    return <OverlayPage />;
  }

  return (
    <div className="settings-app">
      <aside className="settings-sidebar">
        <div className="sidebar-title">PVZ融合版辅助</div>
        <nav className="sidebar-nav" aria-label="设置导航">
          {navItems.map((item) => (
            <NavLink
              key={item.path}
              className={({ isActive }) => `nav-item${isActive ? ' active' : ''}`}
              to={item.path}
            >
              <span className="nav-item-icon">{item.icon}</span>
              <span>{item.label}</span>
            </NavLink>
          ))}
        </nav>
      </aside>

      <main className="settings-main">
        <div className="settings-content">
          <Routes>
            <Route path="/" element={<Navigate to="/clipboard" replace />} />
            <Route
              path="/clipboard"
              element={
                <>
                  <section className="settings-section">
                    <h2 className="section-title">游戏设置</h2>
                    <div className="settings-card">
                      <SettingRow label="进程名" description={gameName} descriptionClassName="mono-text" />
                      <SettingRow label="插件注入">
                        <Switch
                          checked={isInjected}
                          loading={injecting}
                          onChange={handleInjectSwitchChange}
                        />
                      </SettingRow>
                      <SettingRow label="僵尸射线">
                        <Switch
                          checked={overlayVisible}
                          loading={overlayLoading}
                          onChange={handleOverlayVisibleChange}
                        />
                      </SettingRow>
                      <SettingRow label="阳光">
                        <div className="setting-actions">
                          <InputNumber
                            className="sun-input"
                            min={0}
                            max={2147483647}
                            precision={0}
                            value={sunValue}
                            onChange={(value) => setSunValue(value ?? defaultSunValue)}
                          />
                          <Button type="primary" onClick={handleSetSun}>
                            应用
                          </Button>
                        </div>
                      </SettingRow>
                      <SettingRow label="下一波">
                        <div className="setting-actions">
                          <Button type="primary" onClick={async () => {
                            const raw = await "GetBoardFields".invoke()
                            console.log(JSON.parse(raw as string))
                          }}>
                            应用
                          </Button>
                        </div>
                      </SettingRow>
                      {cheatToggleConfigs.map((config) => (
                        <SettingRow key={config.key} label={config.label}>
                          <Switch
                            checked={cheatToggles[config.key]}
                            loading={cheatToggleLoading[config.key]}
                            onChange={handleCheatToggleChange(config)}
                          />
                        </SettingRow>
                      ))}
                    </div>
                  </section>
                </>
              }
            />
            <Route
              path="/history"
              element={<HistoryPage />}
            />
            <Route
              path="/general"
              element={<TerrainPage isInjected={isInjected} />}
            />
            <Route
              path="/shortcuts"
              element={<PlaceholderPage title="快捷操作" description="这里可以放热键绑定与快速动作。" />}
            />
            <Route
              path="/backup"
              element={<PlaceholderPage title="存档备份" description="这里可以放存档导入导出与备份策略。" />}
            />
            <Route
              path="/about"
              element={<PlaceholderPage title="关于工具" description="这里可以放版本信息和使用说明。" />}
            />
            <Route path="*" element={<Navigate to="/clipboard" replace />} />
          </Routes>
        </div>
      </main>
    </div>
  );
}

export default App;
