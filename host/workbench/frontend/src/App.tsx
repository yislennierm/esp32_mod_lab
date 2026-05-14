import {
  Alert,
  Badge,
  Button,
  Card,
  Collapse,
  Descriptions,
  Empty,
  Form,
  Input,
  InputNumber,
  Layout,
  List,
  Menu,
  Modal,
  Segmented,
  Select,
  Slider,
  Space,
  Statistic,
  Steps,
  Switch,
  Table,
  Tabs,
  Tag,
  Tooltip,
  Typography,
  Progress,
  Popconfirm,
  AlertProps
} from 'antd';
import {
  ApartmentOutlined,
  ApiOutlined,
  ArrowDownOutlined,
  ArrowUpOutlined,
  ClusterOutlined,
  BugOutlined,
  CheckCircleOutlined,
  DatabaseOutlined,
  DesktopOutlined,
  DisconnectOutlined,
  ExperimentOutlined,
  FileSearchOutlined,
  FilterOutlined,
  HddOutlined,
  HistoryOutlined,
  InfoCircleOutlined,
  PlayCircleOutlined,
  PlusOutlined,
  ProfileOutlined,
  ReloadOutlined,
  RocketOutlined,
  SaveOutlined,
  SafetyCertificateOutlined,
  ThunderboltOutlined,
  WarningOutlined,
  GithubOutlined,
  LinkOutlined,
  CheckSquareOutlined,
  LoadingOutlined
} from '@ant-design/icons';
import {
  Background,
  Controls,
  MarkerType,
  ReactFlow,
  type Edge,
  type Node
} from '@xyflow/react';
import { Application, Filter, GlProgram, Sprite, Texture } from 'pixi.js';
import { ReactNode, useEffect, useMemo, useRef, useState } from 'react';
import '@xyflow/react/dist/style.css';
import {
  api,
  ArtifactItem,
  DestinationProfile,
  EspressifRepo,
  FlashManifest,
  LabBlock,
  LabProject,
  PinRow,
  ProjectActionResult,
  ProjectValidation,
  SdkExample,
  SdkInventorySummary,
  SerialOwnershipResult,
  TargetProfile,
  WorkbenchStatus
} from './api';
import { runBrowserFlashSession, type BrowserFlashPhase } from './browserFlash';
import { graphLibraryEntries, resolveGraphBlockSpec, type BlockFieldSpec, type GraphLibraryEntry } from './graphBlockSpecs';
import { usePersistentState } from './usePersistentState';

const { Header, Sider, Content } = Layout;
const { Text } = Typography;
const { Search } = Input;

type DestinationPinDraft = {
  key: string;
  signal: string;
  role: string;
  gpio: number | null;
  notes: string;
};

type DestinationSettingsDraft = {
  controller_ic: string;
  width: number | null;
  height: number | null;
  pclk_hz_initial: number;
  mode: number;
  cmd_bits: number;
  param_bits: number;
  max_transfer_lines_initial: number;
  swap_xy: boolean;
  mirror_x: boolean;
  mirror_y: boolean;
  invert_color: boolean;
  color_order: string;
};

type OperationEntry = {
  id: string;
  kind: 'save' | 'validate' | 'build' | 'flash' | 'system';
  level: AlertProps['type'];
  title: string;
  detail: string;
  timestamp: string;
  projectId?: string;
  buildProfile?: string;
};

type OperationState = {
  active: boolean;
  kind: OperationEntry['kind'];
  level: AlertProps['type'];
  title: string;
  detail: string;
  progress?: number;
  phase?: string;
  nextStep?: string;
};

type ResourceCardStatus = 'idle' | 'reserved' | 'active' | 'warning' | 'error';

type ResourceCardData = {
  key: string;
  block: string;
  region: string;
  status: ResourceCardStatus;
  owner: string;
  mode: string;
  health: string;
  risk: string;
  facts: string[];
};

type GraphLaneId = 'intent' | 'io' | 'sdk' | 'resource' | 'observe';

type GraphLane = {
  id: GraphLaneId;
  label: string;
  color: string;
  x: number;
};

type GraphLibraryRoot = 'intent' | 'sdf' | 'sdk' | 'resource' | 'external';

const destinationOutputRoles = new Set([
  'spi_chip_select',
  'panel_reset',
  'data_command_select',
  'spi_mosi',
  'spi_clock'
]);

const destinationGpioOptions = Array.from({ length: 55 }, (_, gpio) => ({
  value: gpio,
  label: `GPIO${gpio}`
}));

const graphLanes: GraphLane[] = [
  { id: 'intent', label: 'Intent', color: 'gold', x: 40 },
  { id: 'io', label: 'I/O And Targets', color: 'green', x: 280 },
  { id: 'sdk', label: 'SDK And Runtime', color: 'blue', x: 560 },
  { id: 'resource', label: 'ESP32-P4 Blocks', color: 'purple', x: 860 },
  { id: 'observe', label: 'Telemetry', color: 'cyan', x: 1160 }
];

function graphLaneForNode(node: Record<string, unknown>, type: string): GraphLaneId {
  if (type === 'system_intent') return 'intent';
  if (type === 'lab_function_block') return 'sdk';
  if (type === 'esp32p4_resource') return 'resource';
  if (type === 'external_device') return 'io';
  if (type.includes('source') || type.includes('destination')) return 'io';
  const params = node.params && typeof node.params === 'object' ? node.params as Record<string, unknown> : {};
  if (params.api_group) return 'sdk';
  if (String(node.label || '').toLowerCase().includes('telemetry')) return 'observe';
  return 'sdk';
}

function sourceGpioOwnersFromPins(pins: PinRow[]) {
  const owners = new Map<number, string>();
  for (const pin of pins) {
    if (typeof pin.gpio === 'number') {
      owners.set(pin.gpio, pin.signal);
    }
  }
  return owners;
}

const navItems = [
  { key: 'dashboard', icon: <DatabaseOutlined />, label: 'Dashboard' },
  { key: 'project', icon: <ApartmentOutlined />, label: 'Projects' },
  { key: 'graph', icon: <ApiOutlined />, label: 'Graph' },
  { key: 'source', icon: <ExperimentOutlined />, label: 'Signals' },
  { key: 'processing', icon: <ApiOutlined />, label: 'Runtime' },
  { key: 'destination', icon: <DesktopOutlined />, label: 'I/O' },
  { key: 'live', icon: <PlayCircleOutlined />, label: 'Monitor' },
  { key: 'artifacts', icon: <DatabaseOutlined />, label: 'Artifacts' },
  { key: 'profile', icon: <ProfileOutlined />, label: 'Profiles' },
  { key: 'logs', icon: <FileSearchOutlined />, label: 'Logs' }
];

function statusColor(status?: WorkbenchStatus | null): 'success' | 'warning' | 'error' | 'default' {
  if (!status) return 'default';
  if (status.running && status.source_state === 'live') return 'success';
  if (status.running) return 'warning';
  if (status.error) return 'error';
  return 'default';
}

function JsonBlock({ value }: { value: unknown }) {
  return <pre className="jsonBlock">{JSON.stringify(value ?? {}, null, 2)}</pre>;
}

function MetricStrip({ items }: { items: Array<{ label: string; value: ReactNode; color?: string }> }) {
  return (
    <div className="metricStrip">
      {items.map((item) => (
        <div className="metricItem" key={item.label}>
          <Text type="secondary">{item.label}</Text>
          <div>{typeof item.value === 'string' || typeof item.value === 'number' ? <Text strong>{item.value}</Text> : item.value}</div>
        </div>
      ))}
    </div>
  );
}

function formatShortcut(label: string) {
  return <kbd className="shortcutKey">{label}</kbd>;
}

function OperationCenter({
  status,
  selectedProject,
  selectedBuildProfile,
  operation,
  history
}: {
  status: WorkbenchStatus | null;
  selectedProject: LabProject | null;
  selectedBuildProfile: string;
  operation: OperationState | null;
  history: OperationEntry[];
}) {
  const latest = history[0] || null;
  const tone = operation?.level || latest?.level || 'info';
  const progress = operation?.progress ?? (operation?.active ? 12 : 100);

  return (
    <Card size="small" className="operationCenterCard">
      <div className="operationCenterLayout">
        <div className="operationCenterLead">
          <Space size={8} wrap>
            <Tag color={status?.device_connected ? 'green' : 'orange'} icon={<ThunderboltOutlined />}>
              {status?.device_connected ? 'device online' : 'offline mode'}
            </Tag>
            <Tag color="blue" icon={<ApartmentOutlined />}>
              {selectedProject?.name || 'no project'}
            </Tag>
            <Tag icon={<RocketOutlined />}>{selectedBuildProfile}</Tag>
            <Tag icon={<ApiOutlined />} color={status?.serial_owner === 'browser_flash' ? 'gold' : 'default'}>
              {status?.serial_owner || 'backend'}
            </Tag>
          </Space>
          <Alert
            type={tone}
            showIcon
            message={operation?.title || latest?.title || 'Workbench ready'}
            description={operation?.detail || latest?.detail || 'Select a project, validate intent, then build or flash with the current profile.'}
          />
          {operation ? (
            <div className="operationProgressRow">
              <Progress percent={Math.max(0, Math.min(100, progress))} size="small" status={operation.level === 'error' ? 'exception' : operation.active ? 'active' : 'success'} />
              <Text type="secondary">{operation.phase || (operation.active ? 'running' : 'ready')}</Text>
              {operation.nextStep ? <Text type="secondary">Next: {operation.nextStep}</Text> : null}
            </div>
          ) : null}
        </div>
        <div className="operationCenterAside">
          <div className="shortcutPanel">
            <Text type="secondary">Shortcuts</Text>
            <Space wrap size={[6, 6]}>
              <Tag>{formatShortcut('Ctrl/Cmd+S')} save</Tag>
              <Tag>{formatShortcut('Ctrl/Cmd+B')} build</Tag>
              <Tag>{formatShortcut('Ctrl/Cmd+Shift+V')} validate</Tag>
              <Tag>{formatShortcut('Ctrl/Cmd+Shift+F')} flash</Tag>
            </Space>
          </div>
          <div className="historyPanel">
            <Space size={6}>
              <HistoryOutlined />
              <Text type="secondary">Recent operations</Text>
            </Space>
            {history.length === 0 ? (
              <Text type="secondary">No operations yet</Text>
            ) : (
              <div className="historyList">
                {history.slice(0, 4).map((entry) => (
                  <div className="historyItem" key={entry.id}>
                    <Tag color={entry.level === 'error' ? 'red' : entry.level === 'warning' ? 'gold' : 'blue'}>
                      {entry.kind}
                    </Tag>
                    <div className="historyCopy">
                      <Text strong>{entry.title}</Text>
                      <Text type="secondary">{entry.detail}</Text>
                    </div>
                    <Text type="secondary">{entry.timestamp}</Text>
                  </div>
                ))}
              </div>
            )}
          </div>
        </div>
      </div>
    </Card>
  );
}

function projectBlockRows(project: LabProject | null, registry: LabBlock[] = []) {
  const registryById = new Map(registry.map((block) => [block.id, block]));
  const rows: Array<{ key: string; id: string; name: string; kind: string; status: string; origin: string }> = [];
  const seen = new Set<string>();
  const add = (id: string, fallbackName: string, kind: string, status = 'project', origin = 'project') => {
    if (!id || seen.has(`${origin}:${id}`)) return;
    const registryBlock = registryById.get(id);
    rows.push({
      key: `${origin}:${id}`,
      id,
      name: registryBlock?.name || fallbackName || id,
      kind: registryBlock?.kind || kind,
      status: registryBlock?.status || status,
      origin
    });
    seen.add(`${origin}:${id}`);
  };

  if (!project) return rows;

  const graphNodes = Array.isArray(project.graph?.nodes) ? project.graph.nodes : [];
  for (const node of graphNodes) {
    const id = String(node.id || node.type || '');
    add(id, String(node.label || node.type || id), String(node.type || 'graph_block'), 'graph', 'graph');
  }

  add(project.source?.block || '', project.source?.block || '', 'source', 'declared', 'role');
  for (const block of project.processing || []) {
    add(block.block, block.block, 'processing', 'declared', 'role');
  }
  add(project.destination?.block || '', project.destination?.block || '', 'destination', 'declared', 'role');

  return rows;
}

function HeaderStatus({ selectedProject, status }: { selectedProject: LabProject | null; status: WorkbenchStatus | null }) {
  const liveActive = Boolean(status?.running);
  return (
    <Space size="middle" wrap className="headerStatusRow">
      <Text strong>ESP32-P4 Signal Lab</Text>
      <Tooltip title={status?.device_connected ? 'Device online and reachable by the workbench.' : 'No active device connection. The workbench is in offline mode.'}>
        <Tag className="headerIconTag" color={status?.device_connected ? 'green' : 'orange'} icon={<ThunderboltOutlined />} aria-label={status?.device_connected ? 'device online' : 'offline mode'} />
      </Tooltip>
      <Tooltip title={liveActive ? 'Live lab stream is active.' : 'Live lab stream is idle.'}>
        <Tag
          className="headerIconTag"
          color={liveActive ? 'success' : 'default'}
          icon={<PlayCircleOutlined />}
          aria-label={liveActive ? 'lab stream active' : 'lab idle'}
        />
      </Tooltip>
    </Space>
  );
}

function GlobalProjectBar({
  projects,
  selectedProject,
  selectedProjectId,
  onSelectProject,
  selectedBuildProfile,
  onSelectBuildProfile,
  onSave,
  onValidate,
  onBuild,
  onFlash,
  onOpenHistory,
  busy,
  browserSerialSupported,
  operation,
  latestHistory
}: {
  projects: LabProject[];
  selectedProject: LabProject | null;
  selectedProjectId: string;
  onSelectProject: (projectId: string) => void;
  selectedBuildProfile: string;
  onSelectBuildProfile: (profile: string) => void;
  onSave: () => void;
  onValidate: () => void;
  onBuild: () => void;
  onFlash: () => void;
  onOpenHistory: () => void;
  busy: string | null;
  browserSerialSupported: boolean;
  operation: OperationState | null;
  latestHistory: OperationEntry | null;
}) {
  const projectOptions = projects.map((project) => ({
    value: project.id,
    label: project.status === 'draft_local_only' ? `${project.name} (local)` : project.name
  }));
  const buildProfiles = Object.entries(selectedProject?.build_profiles || {});
  const profileOptions = buildProfiles.map(([id, entry]) => ({
    value: id,
    label: (
      <Space size={4}>
        {id === 'lab' ? <ExperimentOutlined /> : id === 'telemetry' ? <ApiOutlined /> : <RocketOutlined />}
        <span>{id === 'telemetry' ? 'Telem' : entry?.name || id}</span>
      </Space>
    )
  }));
  const summaryTitle = operation?.title || latestHistory?.title || 'Ready';
  const summaryDetail = operation?.detail || latestHistory?.detail || 'Select a project, then validate, build, or flash.';
  const summaryColor =
    operation?.level === 'error' || latestHistory?.level === 'error'
      ? 'error'
      : operation?.level === 'warning' || latestHistory?.level === 'warning'
        ? 'warning'
        : operation?.level === 'success' || latestHistory?.level === 'success'
          ? 'success'
          : 'default';

  return (
    <div className="globalProjectBar">
      <Select
        className="globalProjectSelect"
        size="small"
        value={selectedProjectId || undefined}
        options={projectOptions}
        onChange={onSelectProject}
      />
      <Segmented
        size="small"
        value={selectedBuildProfile}
        options={profileOptions}
        onChange={(value) => onSelectBuildProfile(String(value))}
      />
      <Space size={4} wrap>
        <Tooltip title="Save project">
          <Button size="small" icon={<SaveOutlined />} disabled={!selectedProject} loading={busy === 'save'} onClick={onSave} />
        </Tooltip>
        <Tooltip title="Validate project intent and pin conflicts">
          <Button size="small" icon={<CheckSquareOutlined />} disabled={!selectedProject || selectedProject.status === 'draft_local_only'} loading={busy === 'validate'} onClick={onValidate} />
        </Tooltip>
        <Tooltip title="Build selected profile">
          <Button size="small" icon={<BugOutlined />} disabled={!selectedProject || selectedProject.status === 'draft_local_only'} loading={busy === 'build'} onClick={onBuild} />
        </Tooltip>
        <Tooltip title="Flash selected profile in browser">
          <Button size="small" type="primary" icon={<ThunderboltOutlined />} disabled={!selectedProject || selectedProject.status === 'draft_local_only' || !browserSerialSupported} loading={busy === 'flash'} onClick={onFlash} />
        </Tooltip>
        <Tooltip title="Operation history">
          <Button size="small" icon={<HistoryOutlined />} onClick={onOpenHistory} />
        </Tooltip>
      </Space>
      <Tooltip title={`${summaryTitle}. ${summaryDetail}`}>
        <Tag className="globalOperationSummary" color={summaryColor}>
          <Space size={6}>
            {operation?.active ? <LoadingOutlined /> : <InfoCircleOutlined />}
            <span>{operation?.active ? summaryTitle : latestHistory?.title || 'Ready'}</span>
          </Space>
        </Tag>
      </Tooltip>
    </div>
  );
}

function createDraftProject(): LabProject {
  const stamp = new Date().toISOString().replace(/[-:]/g, '').slice(0, 15).toLowerCase();
  return {
    id: `draft_project_${stamp}`,
    name: `Draft Project ${stamp}`,
    status: 'draft_local_only',
    description: 'Unsaved local project draft for graph and UI planning.',
    source: { block: 'unassigned_source', profile: undefined },
    processing: [],
    destination: { block: 'unassigned_destination', profile: undefined },
    production: {
      build_script: null,
      flash_script: null,
      default_env: {},
      known_good_command: null
    },
    build_profiles: {
      lab: {
        name: 'Lab',
        role: 'lab',
        build_script: 'scripts/build_lab_firmware.sh',
        flash_script: 'scripts/flash_lab_firmware.sh',
        default_env: { LAB_BUILD_DIR: 'build_lab' }
      },
      telemetry: {
        name: 'Telemetry',
        role: 'telemetry',
        build_script: 'scripts/build_telemetry_firmware.sh',
        flash_script: 'scripts/flash_telemetry_firmware.sh',
        default_env: { TELEMETRY_BUILD_DIR: 'build_telemetry' }
      },
      production: {
        name: 'Production',
        role: 'production',
        build_script: null,
        flash_script: null,
        default_env: {}
      }
    },
    mcu_blocks: ['HP RISC-V x2', 'GPIO Matrix / IO MUX'],
    graph: {
      nodes: [],
      edges: []
    }
  };
}

function ProjectPage({
  status,
  blocks,
  projects,
  selectedProject,
  selectedProjectId,
  selectedBuildProfile,
  onSelectBuildProfile,
  onSelectProject,
  onCreateProject,
  onSaveProject,
  onDuplicateProject,
  onDeleteProject,
  onProjectsChanged,
  onOpenProject,
  onActivity,
  onOperationStateChange
}: {
  status: WorkbenchStatus | null;
  blocks: LabBlock[];
  projects: LabProject[];
  selectedProject: LabProject | null;
  selectedProjectId: string;
  selectedBuildProfile: string;
  onSelectBuildProfile: (profile: string) => void;
  onSelectProject: (projectId: string) => void;
  onCreateProject: () => Promise<void>;
  onSaveProject: (project: LabProject) => Promise<void>;
  onDuplicateProject: (project: LabProject) => Promise<void>;
  onDeleteProject: (project: LabProject) => Promise<void>;
  onProjectsChanged: (projects: LabProject[], selectedProjectId?: string) => void;
  onOpenProject: (projectId: string, page?: string) => void;
  onActivity: (entry: Omit<OperationEntry, 'id' | 'timestamp'>) => void;
  onOperationStateChange: (operation: OperationState | null) => void;
}) {
  const [validation, setValidation] = useState<ProjectValidation | null>(null);
  const [projectBusy, setProjectBusy] = useState<string | null>(null);
  const [projectResult, setProjectResult] = useState<Record<string, unknown> | ProjectActionResult | null>(null);
  const [flashManifest, setFlashManifest] = useState<FlashManifest | null>(null);
  const [browserFlashProgress, setBrowserFlashProgress] = useState(0);
  const [browserFlashState, setBrowserFlashState] = useState('idle');
  const [browserFlashLogs, setBrowserFlashLogs] = useState<string[]>([]);
  const [serialOwnership, setSerialOwnership] = useState<SerialOwnershipResult | null>(null);

  const buildProfileEntries = Object.entries(selectedProject?.build_profiles || {});
  const buildProfileOptions = buildProfileEntries.map(([id, entry]) => ({
    value: id,
    label: entry?.name || id
  }));
  const activeBuildProfile = (selectedProject?.build_profiles?.[selectedBuildProfile]
    || selectedProject?.build_profiles?.production
    || (buildProfileEntries.length ? buildProfileEntries[0]?.[1] : null));

  useEffect(() => {
    setFlashManifest(null);
    setBrowserFlashProgress(0);
    setBrowserFlashState('idle');
    setBrowserFlashLogs([]);
    setSerialOwnership(null);
  }, [selectedProject?.id, selectedBuildProfile]);

  const browserSerialSupported = typeof navigator !== 'undefined' && 'serial' in navigator;

  const appendBrowserFlashLog = (line: string) => {
    setBrowserFlashLogs((current) => [...current.slice(-79), line]);
  };

  const loadBrowserFlashManifest = async () => {
    if (!selectedProject || selectedProject.status === 'draft_local_only') {
      throw new Error('project_not_ready');
    }
    const manifest = await api.flashManifest(selectedProject.id, selectedBuildProfile);
    setFlashManifest(manifest);
    return manifest;
  };

  const validateProject = async () => {
    if (!selectedProject || selectedProject.status === 'draft_local_only') return;
    setProjectBusy('validate');
    onOperationStateChange({
      active: true,
      kind: 'validate',
      level: 'info',
      title: `Validating ${selectedProject.name}`,
      detail: 'Checking graph intent, profiles, and source/destination GPIO conflicts.',
      phase: 'validation',
      nextStep: 'Review warnings before building or flashing.'
    });
    try {
      const result = await api.validateProject(selectedProject.id);
      setValidation(result);
      setProjectResult(null);
      onActivity({
        kind: 'validate',
        level: result.ok ? 'success' : 'warning',
        title: result.ok ? `Validation passed for ${selectedProject.name}` : `Validation warnings for ${selectedProject.name}`,
        detail: result.ok
          ? 'Project is structurally valid for the selected graph and profiles.'
          : `${result.errors.length} errors, ${result.warnings.length} warnings`,
        projectId: selectedProject.id,
        buildProfile: selectedBuildProfile
      });
      onOperationStateChange({
        active: false,
        kind: 'validate',
        level: result.ok ? 'success' : 'warning',
        title: result.ok ? 'Validation complete' : 'Validation needs review',
        detail: result.ok
          ? 'No blocking errors were found.'
          : 'Open the Validation tab and resolve the blocking items before production flashing.',
        progress: 100,
        phase: result.ok ? 'ready' : 'review_required',
        nextStep: result.ok ? 'Build the selected profile.' : 'Fix listed conflicts and validate again.'
      });
    } catch (error) {
      setValidation({
        ok: false,
        project_id: selectedProject?.id || '',
        errors: [error instanceof Error ? error.message : String(error)],
        warnings: [],
        source_gpios: [],
        destination_gpios: []
      });
      const message = error instanceof Error ? error.message : String(error);
      onActivity({
        kind: 'validate',
        level: 'error',
        title: `Validation failed for ${selectedProject.name}`,
        detail: message,
        projectId: selectedProject.id,
        buildProfile: selectedBuildProfile
      });
      onOperationStateChange({
        active: false,
        kind: 'validate',
        level: 'error',
        title: 'Validation failed',
        detail: message,
        progress: 100,
        phase: 'error',
        nextStep: 'Retry validation after fixing the underlying issue.'
      });
    } finally {
      setProjectBusy(null);
    }
  };

  const prepareBrowserFlash = async () => {
    if (!selectedProject || selectedProject.status === 'draft_local_only') return;
    setProjectBusy('prepare-browser-flash');
    setBrowserFlashState('preparing');
    setBrowserFlashLogs([]);
    setBrowserFlashProgress(0);
    onOperationStateChange({
      active: true,
      kind: 'flash',
      level: 'info',
      title: `Preparing browser flash for ${selectedProject.name}`,
      detail: 'Loading the signed-off flash manifest and releasing backend serial ownership.',
      phase: 'preparing',
      nextStep: 'Choose the board port in Chrome or Edge.'
    });
    try {
      const manifest = await loadBrowserFlashManifest();
      appendBrowserFlashLog(`Loaded flash manifest for ${manifest.project_id}:${manifest.build_profile}`);
      const ownership = await api.releaseSerial();
      setSerialOwnership(ownership);
      appendBrowserFlashLog(`Released backend serial ownership on ${ownership.port || status?.serial_port || 'unknown port'}`);
      setBrowserFlashState('prepared');
      setProjectResult({
        ok: true,
        action: 'browser_flash_prepare',
        build_profile: selectedBuildProfile,
        manifest,
        ownership
      });
      onActivity({
        kind: 'flash',
        level: 'info',
        title: `Browser flash prepared for ${selectedProject.name}`,
        detail: `Backend released ${ownership.port || status?.serial_port || 'serial port'} for browser ownership.`,
        projectId: selectedProject.id,
        buildProfile: selectedBuildProfile
      });
      onOperationStateChange({
        active: false,
        kind: 'flash',
        level: 'success',
        title: 'Browser flash prepared',
        detail: 'The flash manifest is ready and the backend has released the serial device.',
        progress: 100,
        phase: 'prepared',
        nextStep: 'Click Flash in Browser to write the image.'
      });
    } catch (error) {
      const message = error instanceof Error ? error.message : String(error);
      appendBrowserFlashLog(`Prepare failed: ${message}`);
      setBrowserFlashState('error');
      setProjectResult({
        ok: false,
        action: 'browser_flash_prepare',
        build_profile: selectedBuildProfile,
        error: message
      });
      onActivity({
        kind: 'flash',
        level: 'error',
        title: `Browser flash prepare failed for ${selectedProject.name}`,
        detail: message,
        projectId: selectedProject.id,
        buildProfile: selectedBuildProfile
      });
      onOperationStateChange({
        active: false,
        kind: 'flash',
        level: 'error',
        title: 'Browser flash prepare failed',
        detail: message,
        progress: 100,
        phase: 'error',
        nextStep: 'Resolve the serial or build issue, then try again.'
      });
    } finally {
      setProjectBusy(null);
    }
  };

  const reconnectAfterBrowserFlash = async () => {
    setProjectBusy('reconnect-browser-flash');
    onOperationStateChange({
      active: true,
      kind: 'flash',
      level: 'info',
      title: 'Reconnecting lab backend',
      detail: 'Restoring backend serial access after browser flashing.',
      phase: 'reconnecting'
    });
    try {
      const ownership = await api.reconnectSerial();
      setSerialOwnership(ownership);
      appendBrowserFlashLog(ownership.ok ? 'Reconnected backend serial ownership' : `Reconnect failed: ${ownership.error || 'unknown error'}`);
      setBrowserFlashState(ownership.ok ? 'reconnected' : 'error');
      setProjectResult({
        ok: ownership.ok,
        action: 'browser_flash_reconnect',
        build_profile: selectedBuildProfile,
        ownership
      });
      onActivity({
        kind: 'flash',
        level: ownership.ok ? 'success' : 'warning',
        title: ownership.ok ? 'Lab backend reconnected' : 'Lab backend reconnect needs attention',
        detail: ownership.ok ? 'Interactive lab commands are available again.' : ownership.error || 'Reconnect failed.',
        projectId: selectedProject?.id,
        buildProfile: selectedBuildProfile
      });
      onOperationStateChange({
        active: false,
        kind: 'flash',
        level: ownership.ok ? 'success' : 'warning',
        title: ownership.ok ? 'Reconnect complete' : 'Reconnect needs attention',
        detail: ownership.ok ? 'The workbench owns the serial device again.' : ownership.error || 'Reconnect failed.',
        progress: 100,
        phase: ownership.ok ? 'ready' : 'review_required',
        nextStep: ownership.ok ? 'Resume monitoring or graph work.' : 'Reconnect manually or reflash lab firmware.'
      });
    } catch (error) {
      const message = error instanceof Error ? error.message : String(error);
      appendBrowserFlashLog(`Reconnect failed: ${message}`);
      setBrowserFlashState('error');
      setProjectResult({
        ok: false,
        action: 'browser_flash_reconnect',
        build_profile: selectedBuildProfile,
        error: message
      });
      onActivity({
        kind: 'flash',
        level: 'error',
        title: 'Reconnect failed',
        detail: message,
        projectId: selectedProject?.id,
        buildProfile: selectedBuildProfile
      });
      onOperationStateChange({
        active: false,
        kind: 'flash',
        level: 'error',
        title: 'Reconnect failed',
        detail: message,
        progress: 100,
        phase: 'error',
        nextStep: 'Try Reconnect Lab again or inspect the serial path.'
      });
    } finally {
      setProjectBusy(null);
    }
  };

  const runBrowserFlash = async () => {
    if (!selectedProject || selectedProject.status === 'draft_local_only') return;
    if (!browserSerialSupported) {
      setBrowserFlashState('error');
      setProjectResult({
        ok: false,
        action: 'browser_flash',
        build_profile: selectedBuildProfile,
        error: 'web_serial_not_supported_use_chrome_or_edge_desktop'
      });
      return;
    }
    setProjectBusy('browser-flash');
    setBrowserFlashProgress(0);
    setBrowserFlashState('flashing');
    try {
      const manifest = flashManifest || await loadBrowserFlashManifest();
      const phaseDescriptions: Record<BrowserFlashPhase, string> = {
        idle: 'idle',
        preparing: 'Preparing manifest and serial ownership',
        awaiting_port: 'Waiting for browser port selection',
        connecting: 'Connecting to the bootloader',
        downloading: 'Downloading build artifacts',
        writing: 'Writing flash images',
        resetting: 'Resetting the board',
        reconnecting: 'Reconnecting backend control',
        complete: 'Flash completed',
        error: 'Flash failed'
      };
      const session = await runBrowserFlashSession({
        manifest,
        buildProfile: selectedBuildProfile,
        releaseSerial: async () => {
          const ownership = serialOwnership?.serial_owner === 'browser_flash' ? serialOwnership : await api.releaseSerial();
          setSerialOwnership(ownership);
          return ownership;
        },
        reconnectSerial: async () => {
          const reconnect = await api.reconnectSerial();
          setSerialOwnership(reconnect);
          return reconnect;
        },
        onLog: appendBrowserFlashLog,
        onProgress: setBrowserFlashProgress,
        onPhase: (phase, detail) => {
          setBrowserFlashState(phase);
          onOperationStateChange({
            active: !['complete', 'error'].includes(phase),
            kind: 'flash',
            level: phase === 'error' ? 'error' : phase === 'complete' ? 'success' : 'info',
            title: `Flashing ${selectedProject.name} (${selectedBuildProfile})`,
            detail: detail || phaseDescriptions[phase],
            progress: phase === 'complete' ? 100 : undefined,
            phase,
            nextStep: phase === 'awaiting_port' ? 'Approve the browser serial prompt.' : phase === 'complete' ? 'Return to Monitor or Graph.' : undefined
          });
        }
      });
      appendBrowserFlashLog('Device reset complete');
      setBrowserFlashState('flashed');
      const resultPayload: Record<string, unknown> = {
        ok: true,
        action: 'browser_flash',
        build_profile: selectedBuildProfile,
        chip: session.chip,
        manifest
      };
      if (selectedBuildProfile !== 'production' && session.reconnect) {
        appendBrowserFlashLog(session.reconnect.ok ? 'Backend serial reconnected for lab monitoring' : `Reconnect failed: ${session.reconnect.error || 'unknown error'}`);
        if (session.reconnect.ok) setBrowserFlashState('reconnected');
        resultPayload.reconnect = session.reconnect;
      } else {
        appendBrowserFlashLog('Production flash leaves the board in product mode until lab firmware is flashed again');
      }
      setProjectResult(resultPayload);
      onActivity({
        kind: 'flash',
        level: 'success',
        title: `Flashed ${selectedProject.name}`,
        detail: `${selectedBuildProfile} image written via browser flow.`,
        projectId: selectedProject.id,
        buildProfile: selectedBuildProfile
      });
      onOperationStateChange({
        active: false,
        kind: 'flash',
        level: 'success',
        title: 'Flash complete',
        detail: selectedBuildProfile === 'production'
          ? 'Production firmware is now active on the board.'
          : 'Board flashed and backend control restored.',
        progress: 100,
        phase: 'complete',
        nextStep: selectedBuildProfile === 'production' ? 'Reflash lab firmware before using live monitor again.' : 'Open Monitor or continue graph work.'
      });
    } catch (error) {
      const message = error instanceof Error ? error.message : String(error);
      appendBrowserFlashLog(`Browser flash failed: ${message}`);
      setBrowserFlashState('error');
      setProjectResult({
        ok: false,
        action: 'browser_flash',
        build_profile: selectedBuildProfile,
        error: message
      });
      onActivity({
        kind: 'flash',
        level: 'error',
        title: `Flash failed for ${selectedProject.name}`,
        detail: message,
        projectId: selectedProject.id,
        buildProfile: selectedBuildProfile
      });
      onOperationStateChange({
        active: false,
        kind: 'flash',
        level: 'error',
        title: 'Flash failed',
        detail: message,
        progress: 100,
        phase: 'error',
        nextStep: 'Inspect Browser Flash logs and retry after resolving the cause.'
      });
    } finally {
      setProjectBusy(null);
    }
  };

  const runProjectAction = async (action: 'build' | 'flash') => {
    if (!selectedProject || selectedProject.status === 'draft_local_only') return;
    setProjectBusy(action);
    onOperationStateChange({
      active: true,
      kind: action,
      level: 'info',
      title: `${action === 'build' ? 'Building' : 'Flashing'} ${selectedProject.name}`,
      detail: `${action === 'build' ? 'Executing build script' : 'Running direct flash script'} for ${selectedBuildProfile}.`,
      phase: action
    });
    try {
      const result = action === 'build'
        ? await api.buildProject(selectedProject.id, selectedBuildProfile)
        : await api.flashProject(selectedProject.id, selectedBuildProfile);
      setProjectResult(result);
      onActivity({
        kind: action,
        level: result.ok ? 'success' : 'error',
        title: `${action === 'build' ? 'Build' : 'Flash'} ${result.ok ? 'completed' : 'failed'} for ${selectedProject.name}`,
        detail: result.ok
          ? `${selectedBuildProfile} ${action} script finished successfully.`
          : result.error || `${action} returned a non-zero status.`,
        projectId: selectedProject.id,
        buildProfile: selectedBuildProfile
      });
      onOperationStateChange({
        active: false,
        kind: action,
        level: result.ok ? 'success' : 'error',
        title: result.ok ? `${action === 'build' ? 'Build' : 'Flash'} complete` : `${action === 'build' ? 'Build' : 'Flash'} failed`,
        detail: result.ok
          ? `${selectedBuildProfile} ${action} path finished successfully.`
          : result.error || `${action} returned a non-zero status.`,
        progress: 100,
        phase: result.ok ? 'complete' : 'error',
        nextStep: result.ok ? (action === 'build' ? 'Flash from the browser or inspect build artifacts.' : 'Verify the board state in Monitor.') : 'Inspect the Result tab for command output.'
      });
    } catch (error) {
      const failure = {
        ok: false,
        project_id: selectedProject?.id || '',
        action,
        build_profile: selectedBuildProfile,
        error: error instanceof Error ? error.message : String(error)
      };
      setProjectResult(failure);
      onActivity({
        kind: action,
        level: 'error',
        title: `${action === 'build' ? 'Build' : 'Flash'} failed for ${selectedProject.name}`,
        detail: failure.error || 'Unknown error',
        projectId: selectedProject.id,
        buildProfile: selectedBuildProfile
      });
      onOperationStateChange({
        active: false,
        kind: action,
        level: 'error',
        title: `${action === 'build' ? 'Build' : 'Flash'} failed`,
        detail: failure.error || 'Unknown error',
        progress: 100,
        phase: 'error',
        nextStep: 'Inspect command output, then retry.'
      });
    } finally {
      setProjectBusy(null);
    }
  };

  const projectOptions = projects.map((project) => ({ value: project.id, label: project.status === 'draft_local_only' ? `${project.name} (local)` : project.name }));
  const buildProfileSegmented = buildProfileEntries.map(([id, entry]) => ({
    value: id,
    label: (
      <Space size={4}>
        {id === 'lab' ? <ExperimentOutlined /> : id === 'telemetry' ? <ApiOutlined /> : <RocketOutlined />}
        <span>{id === 'telemetry' ? 'Telem' : entry?.name || id}</span>
      </Space>
    )
  }));

  return (
    <Space direction="vertical" size="small" className="pageStack">
      <Card
        size="small"
        title={selectedProject?.name || 'Project'}
        extra={
          <Space size={4} wrap>
            <Tag color={selectedProject?.status.includes('validated') ? 'green' : selectedProject?.status === 'draft_local_only' ? 'orange' : 'blue'}>
              {selectedProject?.status || 'none'}
            </Tag>
            {selectedProject?.id ? <Tag>{selectedProject.id}</Tag> : null}
          </Space>
        }
      >
        <Space direction="vertical" size="small" className="fullWidth">
          <div className="projectToolbarRow">
            <Space size={6} wrap>
              <Tooltip title="Create project">
                <Button icon={<PlusOutlined />} onClick={onCreateProject}>New</Button>
              </Tooltip>
              <Button disabled={!selectedProject} onClick={() => selectedProject && onDuplicateProject(selectedProject)}>Duplicate</Button>
              <Popconfirm
                title="Delete project?"
                description="This removes the project profile from the lab workspace."
                okText="Delete"
                okButtonProps={{ danger: true }}
                onConfirm={() => selectedProject && onDeleteProject(selectedProject)}
              >
                <Button danger disabled={!selectedProject}>Delete</Button>
              </Popconfirm>
            </Space>
            <Space size={6} wrap>
              <Tooltip title="Validate project">
                <Button icon={<CheckCircleOutlined />} loading={projectBusy === 'validate'} disabled={!selectedProject || selectedProject.status === 'draft_local_only'} onClick={validateProject} />
              </Tooltip>
              <Text type="secondary">Build and flash stay in the header.</Text>
            </Space>
          </div>
          <div className="projectMetaGrid">
            <div className="projectMetaPanel">
              <Text type="secondary"><ExperimentOutlined /> Ingress</Text>
              <Text strong>{selectedProject?.source?.block || '?'}</Text>
            </div>
            <div className="projectMetaPanel">
              <Text type="secondary"><ClusterOutlined /> Transform</Text>
              <Text strong>{selectedProject?.processing?.length ? selectedProject.processing.map((item) => item.block).join(', ') : 'none'}</Text>
            </div>
            <div className="projectMetaPanel">
              <Text type="secondary"><DesktopOutlined /> Egress</Text>
              <Text strong>{selectedProject?.destination?.block || '?'}</Text>
            </div>
            <div className="projectMetaPanel">
              <Text type="secondary"><ClusterOutlined /> Graph</Text>
              <Space wrap size={[4, 4]}>
                <Tag>{selectedProject?.graph?.nodes?.length ?? 0} nodes</Tag>
                <Tag>{selectedProject?.graph?.edges?.length ?? 0} edges</Tag>
                <Tag>{projects.length} projects</Tag>
              </Space>
            </div>
          </div>
          <Collapse
            ghost
            size="small"
            items={[
              {
                key: 'advanced-actions',
                label: <Space size={6}><ApiOutlined /><span>Advanced</span></Space>,
                children: (
                  <Space wrap>
                    <Button type="primary" danger icon={<RocketOutlined />} loading={projectBusy === 'flash'} disabled={!selectedProject || selectedProject.status === 'draft_local_only'} onClick={() => runProjectAction('flash')}>{`Flash ${activeBuildProfile?.name || 'Build'}`}</Button>
                    <Button icon={<DisconnectOutlined />} loading={projectBusy === 'prepare-browser-flash'} disabled={!selectedProject || selectedProject.status === 'draft_local_only'} onClick={prepareBrowserFlash}>Prepare Browser Flash</Button>
                    <Button icon={<ReloadOutlined />} loading={projectBusy === 'reconnect-browser-flash'} disabled={!selectedProject || selectedProject.status === 'draft_local_only'} onClick={reconnectAfterBrowserFlash}>Reconnect Lab</Button>
                  </Space>
                )
              },
              {
                key: 'deploy-notes',
                label: <Space size={6}><SafetyCertificateOutlined /><span>Deploy</span></Space>,
                children: (
                  <Space direction="vertical" className="fullWidth">
                    <Alert
                      type="warning"
                      showIcon
                      message="Flashing crosses a trust boundary"
                      description="The backend must release serial ownership before the browser can flash the board. Production intentionally leaves the board in product mode; lab and telemetry are expected to reconnect into the interactive command path."
                    />
                    <MetricStrip items={[
                      { label: 'Browser Flash', value: browserSerialSupported ? 'supported' : 'Chrome/Edge required' },
                      { label: 'Serial Owner', value: serialOwnership?.serial_owner || status?.serial_owner || 'unknown' },
                      { label: 'Flash State', value: browserFlashState },
                      { label: 'Progress', value: `${browserFlashProgress}%` }
                    ]} />
                    {flashManifest ? <JsonBlock value={flashManifest} /> : null}
                    <JsonBlock value={activeBuildProfile} />
                  </Space>
                )
              }
            ]}
          />
          <Tabs
            size="small"
            items={[
              {
                key: 'sdk',
                label: <Space size={6}><GithubOutlined /><span>SDK</span></Space>,
                children: (
                  <SdkResearchPage
                    embedded
                    onProjectsChanged={onProjectsChanged}
                    onOpenProject={onOpenProject}
                  />
                )
              },
              {
                key: 'blocks',
                label: <Space size={6}><ApartmentOutlined /><span>Blocks</span></Space>,
                children: (
                  <Table
                    size="small"
                    pagination={false}
                    dataSource={projectBlockRows(selectedProject, blocks)}
                    columns={[
                      { title: '', dataIndex: 'kind', width: 54, render: (kind: string) => <Tooltip title={kind}><Tag>{kind.slice(0, 3)}</Tag></Tooltip> },
                      { title: 'Block', dataIndex: 'name' },
                      {
                        title: '',
                        dataIndex: 'status',
                        width: 68,
                        render: (blockStatus: string, row: { origin: string }) => (
                          <Tooltip title={`${row.origin} / ${blockStatus}`}>
                            <Tag color={blockStatus.includes('validated') || blockStatus.includes('working') ? 'green' : 'blue'}>
                              {row.origin === 'graph' ? 'G' : 'R'}
                            </Tag>
                          </Tooltip>
                        )
                      }
                    ]}
                  />
                )
              },
                {
                key: 'validation',
                label: <Space size={6}><CheckCircleOutlined /><span>Validation</span></Space>,
                children: validation ? (
                  <Space direction="vertical" className="fullWidth">
                    <Tag color={validation.ok ? 'green' : 'red'}>{validation.ok ? 'valid' : 'blocked'}</Tag>
                    {validation.errors.map((error) => <Alert key={error} type="error" showIcon message={error} />)}
                    {validation.warnings.map((warning) => <Alert key={warning} type="warning" showIcon message={warning} />)}
                    <MetricStrip items={[
                      { label: 'Source GPIOs', value: validation.source_gpios.length },
                      { label: 'Destination GPIOs', value: validation.destination_gpios.length },
                      { label: 'Errors', value: validation.errors.length },
                      { label: 'Warnings', value: validation.warnings.length }
                    ]} />
                    <JsonBlock value={{ source_gpios: validation.source_gpios, destination_gpios: validation.destination_gpios }} />
                  </Space>
                ) : <Empty image={Empty.PRESENTED_IMAGE_SIMPLE} description="Validate a project" />
              },
              {
                key: 'result',
                label: <Space size={6}><FileSearchOutlined /><span>Result</span></Space>,
                children: projectResult ? (
                  <Space direction="vertical" className="fullWidth">
                    <Alert
                      type={projectResult && typeof projectResult === 'object' && 'ok' in projectResult && projectResult.ok ? 'success' : 'error'}
                      showIcon
                      message={`Last action: ${String((projectResult as Record<string, unknown>).action || 'unknown')}`}
                      description={String((projectResult as Record<string, unknown>).error || 'Operation completed.')}
                    />
                    <JsonBlock value={projectResult} />
                  </Space>
                ) : <Empty image={Empty.PRESENTED_IMAGE_SIMPLE} description="No build or flash result yet" />
              },
              {
                key: 'browser-flash',
                label: <Space size={6}><ThunderboltOutlined /><span>Browser Flash</span></Space>,
                children: (
                  <Space direction="vertical" className="fullWidth">
                      {!browserSerialSupported ? <Alert type="warning" showIcon message="Browser flashing requires desktop Chrome or Edge with Web Serial support." /> : null}
                      <Tag color={browserFlashState === 'error' ? 'red' : browserFlashState === 'reconnected' || browserFlashState === 'flashed' ? 'green' : 'blue'}>{browserFlashState}</Tag>
                      <Progress percent={browserFlashProgress} size="small" status={browserFlashState === 'error' ? 'exception' : browserFlashState === 'flashed' || browserFlashState === 'reconnected' ? 'success' : 'active'} />
                      <JsonBlock value={{
                        serial_ownership: serialOwnership,
                        manifest: flashManifest,
                        progress_percent: browserFlashProgress,
                        logs: browserFlashLogs
                      }} />
                    </Space>
                  )
                }
            ]}
          />
        </Space>
      </Card>
    </Space>
  );
}

const esp32p4Regions = [
  {
    title: 'HP Core System',
    blocks: ['HP RISC-V x2', 'L2MEM / Cache', 'GPIO Matrix / IO MUX', 'Interrupt Matrix', 'ETM', 'Timers']
  },
  {
    title: 'DMA / Memory Movement',
    blocks: ['GDMA', 'VDMA', '2D-DMA', 'PSRAM', 'Internal DMA RAM']
  },
  {
    title: 'Image / Video',
    blocks: ['LCD_CAM', 'ISP', 'PPA', 'JPEG', 'H264', 'MIPI CSI', 'MIPI DSI']
  },
  {
    title: 'Connectivity',
    blocks: ['SPI', 'I2C', 'I2S', 'UART', 'I3C', 'USB HS OTG', 'USB FS OTG', 'USB Serial/JTAG', 'EMAC', 'SD/MMC']
  },
  {
    title: 'Signal / Control',
    blocks: ['PARLIO', 'RMT', 'LEDC', 'MCPWM', 'PCNT', 'BitScrambler']
  },
  {
    title: 'LP / Analog / Security',
    blocks: ['LP CPU', 'LP GPIO', 'LP I2C', 'LP SPI', 'LP I2S', 'ADC', 'Touch', 'AES/SHA/RSA/ECC']
  }
];

function inferResourceCards({
  activeBlocks,
  selectedProject,
  status,
  profile,
  destinationProfile
}: {
  activeBlocks: Set<string>;
  selectedProject: LabProject | null;
  status: WorkbenchStatus | null;
  profile: TargetProfile | null;
  destinationProfile: DestinationProfile | null;
}): ResourceCardData[] {
  const cards: ResourceCardData[] = [];
  const sourceBlock = selectedProject?.source?.block || 'unassigned source';
  const destinationBlock = selectedProject?.destination?.block || 'unassigned destination';
  const processingBlocks = (selectedProject?.processing || []).map((item) => item.block);
  const graphNodes = Array.isArray(selectedProject?.graph?.nodes) ? selectedProject?.graph?.nodes.length : 0;
  const graphEdges = Array.isArray(selectedProject?.graph?.edges) ? selectedProject?.graph?.edges.length : 0;
  const captureProfile = profile?.current_capture_profile;
  const lcdMode = [captureProfile?.capture_peripheral, captureProfile?.data_mode].filter(Boolean).join(' · ') || 'capture path';
  const lcdFacts = [
    profile?.profile_id ? `profile ${profile.profile_id}` : 'profile unknown',
    status?.server_capture_fps ? `${status.server_capture_fps.toFixed(1)} fps` : 'fps unavailable',
    status?.server_frame_count ? `${status.server_frame_count} frames` : 'no frames yet'
  ];

  const push = (card: ResourceCardData) => cards.push(card);

  if (activeBlocks.has('LCD_CAM')) {
    push({
      key: 'LCD_CAM',
      block: 'LCD_CAM',
      region: 'Image / Video',
      status: status?.running ? 'active' : status?.device_connected ? 'reserved' : 'idle',
      owner: sourceBlock,
      mode: lcdMode,
      health: status?.running ? 'Capturing live source frames.' : 'Ready for capture when the lab stream starts.',
      risk: status?.error ? status.error : status?.running && !status?.device_connected ? 'Capture requested without a connected device.' : 'No immediate capture risk.',
      facts: lcdFacts
    });
  }

  if (activeBlocks.has('SPI')) {
    const spi = destinationProfile?.destination?.spi;
    push({
      key: 'SPI',
      block: 'SPI',
      region: 'Connectivity',
      status: destinationBlock.includes('spi') ? 'active' : 'reserved',
      owner: destinationBlock,
      mode: `${destinationProfile?.destination?.controller_ic || 'panel'} · mode ${spi?.mode ?? 0}`,
      health: spi?.pclk_hz_initial ? `Prepared for panel writes at ${Math.round(spi.pclk_hz_initial / 1000000)} MHz.` : 'SPI destination path configured.',
      risk: spi?.pclk_hz_initial && spi.pclk_hz_initial >= 70000000 ? 'High SPI clock; signal integrity margins matter.' : 'No active queue or draw telemetry in lab mode.',
      facts: [
        spi?.pclk_hz_initial ? `${Math.round(spi.pclk_hz_initial / 1000000)} MHz` : 'clk unknown',
        destinationProfile?.destination?.native_resolution ? `${destinationProfile.destination.native_resolution.width}x${destinationProfile.destination.native_resolution.height}` : 'resolution unknown',
        destinationProfile?.destination?.interface || 'display bus'
      ]
    });
  }

  if (activeBlocks.has('GDMA')) {
    push({
      key: 'GDMA',
      block: 'GDMA',
      region: 'DMA / Memory Movement',
      status: status?.running ? 'active' : 'reserved',
      owner: processingBlocks[0] || sourceBlock,
      mode: 'frame transport',
      health: status?.running ? 'DMA path active in the live capture loop.' : 'Reserved for frame movement and burst capture.',
      risk: status?.consecutive_errors ? `${status.consecutive_errors} recent transport errors.` : 'No recent DMA transport faults reported.',
      facts: [
        'frame ring',
        activeBlocks.has('Internal DMA RAM') ? 'internal DMA slots' : 'shared buffers',
        graphNodes ? `${graphNodes} graph nodes` : 'no graph nodes'
      ]
    });
  }

  if (activeBlocks.has('Internal DMA RAM')) {
    push({
      key: 'Internal DMA RAM',
      block: 'Internal DMA RAM',
      region: 'DMA / Memory Movement',
      status: 'reserved',
      owner: processingBlocks[0] || 'frame pipeline',
      mode: 'DMA-safe frame slots',
      health: 'Allocated for deterministic transfer ownership.',
      risk: activeBlocks.has('PSRAM') ? 'Watch contention with PSRAM-backed stages.' : 'Low risk with isolated DMA slots.',
      facts: ['low latency', 'DMA capable', graphEdges ? `${graphEdges} graph edges` : 'graph sparse']
    });
  }

  if (activeBlocks.has('PSRAM')) {
    push({
      key: 'PSRAM',
      block: 'PSRAM',
      region: 'DMA / Memory Movement',
      status: 'warning',
      owner: 'frame buffers / artifacts',
      mode: 'bulk storage',
      health: 'Available for larger capture or render buffers.',
      risk: 'Higher latency and contention risk than internal DMA RAM.',
      facts: ['external RAM', 'large buffers', 'avoid hot overlap']
    });
  }

  if (activeBlocks.has('GPIO Matrix / IO MUX')) {
    push({
      key: 'GPIO Matrix / IO MUX',
      block: 'GPIO Matrix / IO MUX',
      region: 'HP Core System',
      status: 'reserved',
      owner: sourceBlock,
      mode: 'pin routing',
      health: 'Source and destination profiles define active pin ownership.',
      risk: 'Pin conflicts are controlled by project validation.',
      facts: [
        profile?.signals?.timing_or_control ? `${profile.signals.timing_or_control.length} timing lines` : 'timing lines unknown',
        destinationProfile?.connector?.pins ? `${destinationProfile.connector.pins.length} destination pins` : 'destination pins unknown',
        'validation gated'
      ]
    });
  }

  if (activeBlocks.has('USB Serial/JTAG')) {
    push({
      key: 'USB Serial/JTAG',
      block: 'USB Serial/JTAG',
      region: 'Connectivity',
      status: status?.serial_owner === 'browser_flash' ? 'warning' : status?.device_connected ? 'active' : 'idle',
      owner: status?.serial_owner === 'browser_flash' ? 'browser flash' : 'workbench backend',
      mode: status?.serial_owner === 'browser_flash' ? 'handoff to browser' : 'lab transport',
      health: status?.device_connected ? 'Interactive control path is available.' : 'No board is currently attached to the lab transport.',
      risk: status?.serial_owner === 'browser_flash' ? 'Backend control is suspended until reconnect.' : 'No transport risk reported.',
      facts: [
        status?.serial_port || 'no port',
        status?.serial_owner || 'backend',
        status?.running ? 'streaming' : 'idle'
      ]
    });
  }

  const genericActive = Array.from(activeBlocks)
    .filter((block) => !cards.some((card) => card.block === block))
    .slice(0, 8);

  for (const block of genericActive) {
    const region = esp32p4Regions.find((entry) => entry.blocks.includes(block))?.title || 'Other';
    push({
      key: block,
      block,
      region,
      status: 'reserved',
      owner: processingBlocks[0] || selectedProject?.name || 'project runtime',
      mode: 'project scoped',
      health: 'Resource is claimed by the selected project.',
      risk: 'Detailed live telemetry for this block is not wired into the dashboard yet.',
      facts: ['owned', region, 'inspect graph for detail']
    });
  }

  return cards;
}

function ResourceCard({ card }: { card: ResourceCardData }) {
  const statusColor = card.status === 'active'
    ? 'green'
    : card.status === 'reserved'
      ? 'blue'
      : card.status === 'warning'
        ? 'gold'
        : card.status === 'error'
          ? 'red'
          : 'default';

  return (
    <Card size="small" className={`resourceCard resourceCard-${card.status}`}>
      <div className="resourceCardHeader">
        <Space size={6}>
          <span className={`resourceDot resourceDot-${card.status}`} />
          <Text strong>{card.block}</Text>
        </Space>
        <Tooltip title={`Owner: ${card.owner}`}>
          <Tag color={statusColor}>{card.owner}</Tag>
        </Tooltip>
      </div>
      <div className="resourceCardBody">
        <div className="resourceCardMode">
          <Tag>{card.mode}</Tag>
        </div>
        <div className="resourceFactRow">
          {card.facts.slice(0, 2).map((fact) => <Tag key={fact}>{fact}</Tag>)}
        </div>
      </div>
      <div className="resourceCardFooter">
        <Tooltip title={`Health: ${card.health}`}>
          <Tag className="resourceMetaTag" color={card.status === 'error' ? 'red' : card.status === 'warning' ? 'gold' : 'green'}>
            <CheckCircleOutlined />
          </Tag>
        </Tooltip>
        <Tooltip title={`Risk: ${card.risk}`}>
          <Tag className="resourceMetaTag" color={card.status === 'error' ? 'red' : card.status === 'warning' ? 'gold' : 'default'}>
            <WarningOutlined />
          </Tag>
        </Tooltip>
        {card.facts[2] ? (
          <Tooltip title={card.facts[2]}>
            <Tag className="resourceMetaTag">{card.facts[2]}</Tag>
          </Tooltip>
        ) : null}
        <Tooltip title={card.region}>
          <Tag className="resourceMetaTag">{card.region}</Tag>
        </Tooltip>
      </div>
    </Card>
  );
}

function Esp32p4ResourceMatrix({ activeBlocks }: { activeBlocks: Set<string> }) {
  return (
    <div className="resourceMatrix">
      {esp32p4Regions.map((region) => (
        <div className="resourceMatrixRegion" key={region.title}>
          <Text className="resourceMatrixTitle">{region.title}</Text>
          <div className="resourceMatrixGrid">
            {region.blocks.map((block) => {
              const active = activeBlocks.has(block);
              return (
                <div className={`resourceChip ${active ? 'resourceChipActive' : ''}`} key={block}>
                  <span className={`resourceDot resourceDot-${active ? 'active' : 'idle'}`} />
                  <span>{block}</span>
                </div>
              );
            })}
          </div>
        </div>
      ))}
    </div>
  );
}

function Esp32p4Dashboard({ cards }: { cards: ResourceCardData[] }) {
  return (
    <div className="resourceDashboard">
      {cards.map((card) => <ResourceCard key={card.key} card={card} />)}
    </div>
  );
}

function DashboardPage({
  profile,
  status,
  blocks,
  destinationProfile,
  selectedProject
}: {
  profile: TargetProfile | null;
  status: WorkbenchStatus | null;
  blocks: LabBlock[];
  destinationProfile: DestinationProfile | null;
  selectedProject: LabProject | null;
}) {
  const activeBlocks = useMemo(() => {
    const projectBlocks = Array.isArray(selectedProject?.mcu_blocks) ? selectedProject.mcu_blocks : [];
    return new Set([
      ...projectBlocks,
      status?.device_connected ? 'USB Serial/JTAG' : ''
    ].filter(Boolean));
  }, [selectedProject, status?.device_connected]);
  const activeProjectRows = useMemo(() => projectBlockRows(selectedProject, blocks), [selectedProject, blocks]);
  const resourceCards = useMemo(() => inferResourceCards({
    activeBlocks,
    selectedProject,
    status,
    profile,
    destinationProfile
  }), [activeBlocks, destinationProfile, profile, selectedProject, status]);

  return (
    <Space direction="vertical" size="small" className="pageStack">
      <Card size="small" title="MCU Resource Map">
        <Esp32p4ResourceMatrix activeBlocks={activeBlocks} />
      </Card>
      <Card size="small" title="Active Resources">
        {resourceCards.length ? <Esp32p4Dashboard cards={resourceCards} /> : <Empty image={Empty.PRESENTED_IMAGE_SIMPLE} description="No active resources inferred from the selected project" />}
      </Card>
      <Card size="small" title="Project Blocks">
        <Table
          size="small"
          pagination={false}
          dataSource={activeProjectRows}
          locale={{ emptyText: <Empty image={Empty.PRESENTED_IMAGE_SIMPLE} description="No blocks in selected project" /> }}
          columns={[
            { title: 'Kind', dataIndex: 'kind', width: 96, render: (kind: string) => <Tag>{kind}</Tag> },
            { title: 'Block', dataIndex: 'name' },
            { title: 'Origin', dataIndex: 'origin', width: 86, render: (origin: string) => <Tag color={origin === 'graph' ? 'blue' : 'purple'}>{origin}</Tag> },
            { title: 'Status', dataIndex: 'status', width: 100, render: (blockStatus: string) => <Tag color={blockStatus.includes('validated') || blockStatus.includes('working') ? 'green' : 'blue'}>{blockStatus}</Tag> }
          ]}
        />
      </Card>
    </Space>
  );
}

function GraphPage({
  status,
  blocks,
  selectedProject,
  destinationProfile,
  onProjectDraftChange,
  onSaveProject
}: {
  status: WorkbenchStatus | null;
  blocks: LabBlock[];
  selectedProject: LabProject | null;
  destinationProfile: DestinationProfile | null;
  onProjectDraftChange: (project: LabProject) => void;
  onSaveProject: (project: LabProject) => Promise<void>;
}) {
  const project = selectedProject;
  const [inspectorOpen, setInspectorOpen] = useState(false);
  const [paletteOpen, setPaletteOpen] = useState(true);
  const [graphTool, setGraphTool] = useState('select');
  const [selectedGraphNode, setSelectedGraphNode] = useState<Node | null>(null);
  const [graphDirty, setGraphDirty] = useState(false);
  const [graphLibraryQuery, setGraphLibraryQuery] = useState('');
  const [libraryRoot, setLibraryRoot] = useState<GraphLibraryRoot>('sdf');
  const libraryEntries = useMemo(() => graphLibraryEntries(), []);
  const libraryRoots = useMemo(() => ([
    { key: 'intent' as const, icon: <ProfileOutlined />, label: 'Intent' },
    { key: 'sdf' as const, icon: <ApiOutlined />, label: 'SDF' },
    { key: 'sdk' as const, icon: <BugOutlined />, label: 'SDK' },
    { key: 'resource' as const, icon: <ClusterOutlined />, label: 'ESP32-P4' },
    { key: 'external' as const, icon: <LinkOutlined />, label: 'I/O' }
  ]), []);

  const graphNodeSummary = (node: Record<string, unknown>, type: string) => {
    const params = node.params && typeof node.params === 'object' ? node.params as Record<string, unknown> : {};
    const overlay = params.overlay && typeof params.overlay === 'object' ? params.overlay as Record<string, unknown> : {};
    if (type === 'system_intent') return 'system intent';
    if (type === 'lab_function_block' && params.api_group === 'freertos') {
      return `task ${String(overlay.task_name || 'app_main')} | stack ${Number(overlay.stack_size_bytes || 4096)} | prio ${Number(overlay.priority || 5)}`;
    }
    if (type === 'lab_function_block') return String(params.api_group || 'functional block');
    if (type === 'esp32p4_resource') return String(params.resource_claim || 'resource');
    if (type === 'external_device') return 'external hardware';
    if (type.includes('source')) return 'ingress block';
    if (type.includes('destination')) return 'egress block';
    return type;
  };

  const graphNodeCategory = (node: Record<string, unknown>, type: string) => {
    const lane = graphLaneForNode(node, type);
    return graphLanes.find((entry) => entry.id === lane)?.label || lane;
  };

  const graphNodeColor = (type: string) => {
    if (type === 'system_intent') return 'gold';
    if (type === 'lab_function_block') return 'blue';
    if (type === 'esp32p4_resource') return 'purple';
    if (type === 'external_device') return 'green';
    if (type.includes('destination')) return 'green';
    if (type.includes('source')) return 'blue';
    return 'default';
  };

  const flowNodes: Node[] = useMemo(() => {
    const graphNodes = project?.graph?.nodes;
    if (Array.isArray(graphNodes) && graphNodes.length > 0) {
      const laneRows = new Map<GraphLaneId, number>();
      return graphNodes.map((node, index) => {
        const position = node.position && typeof node.position === 'object' ? node.position as { x?: unknown; y?: unknown } : {};
        const label = String(node.label || node.type || node.id || `node_${index}`);
        const type = String(node.type || 'block');
        const lane = graphLaneForNode(node, type);
        const laneMeta = graphLanes.find((entry) => entry.id === lane) || graphLanes[2];
        const laneRow = laneRows.get(lane) || 0;
        laneRows.set(lane, laneRow + 1);
        return {
          id: String(node.id || `node_${index}`),
          position: {
            x: laneMeta.x,
            y: typeof position.y === 'number' ? position.y : 140 + laneRow * 170
          },
          className: `flowNodeType-${type.replace(/[^A-Za-z0-9_-]+/g, '-')} flowNodeLane-${lane}`,
          data: {
            raw: node,
            type,
            lane,
            label: (
              <div className="flowNode">
                <div className="flowNodeMeta">
                  <Tag color={graphNodeColor(type)}>{type}</Tag>
                  <Tag>{graphNodeCategory(node, type)}</Tag>
                </div>
                <Text strong>{label}</Text>
                <Text type="secondary">{graphNodeSummary(node, type)}</Text>
              </div>
            )
          }
        };
      });
    }
    return [
      {
        id: 'source',
        position: { x: 0, y: 180 },
        data: { label: <div className="flowNode"><Tag color="blue">ingress</Tag><Text strong>{project?.source?.block || 'Input Block'}</Text><Text type="secondary">profile-defined stream</Text></div> }
      },
      {
        id: 'capture',
        position: { x: 300, y: 180 },
        data: { label: <div className="flowNode"><Tag color="purple">resource</Tag><Text strong>ESP32-P4 Peripheral Path</Text><Text type="secondary">DMA/resource ownership</Text></div> }
      },
      {
        id: 'mirror',
        position: { x: 620, y: 180 },
        data: { label: <div className="flowNode"><Tag color="gold">runtime</Tag><Text strong>Project Runtime</Text><Text type="secondary">mode {project?.production?.default_env?.PRODUCTION_MIRROR_MODE || 'not assigned'}</Text></div> }
      },
      {
        id: 'destination',
        position: { x: 940, y: 180 },
        data: { label: <div className="flowNode"><Tag color="green">egress</Tag><Text strong>{project?.destination?.block || 'Output Block'}</Text><Text type="secondary">{destinationProfile?.destination?.controller_ic || 'endpoint'} @ {destinationProfile?.destination?.spi?.pclk_hz_initial || '?'} Hz</Text></div> }
      },
      {
        id: 'telemetry',
        position: { x: 620, y: 360 },
        data: { label: <div className="flowNode"><Tag>telemetry</Tag><Text strong>Workbench</Text><Text type="secondary">{status?.running ? 'stream active' : 'idle'} / {status?.source_state || 'unknown'}</Text></div> }
      }
    ];
  }, [destinationProfile, project, status]);

  const flowEdges: Edge[] = useMemo(() => {
    const graphEdges = project?.graph?.edges;
    if (Array.isArray(graphEdges) && graphEdges.length > 0) {
      return graphEdges.map((edge, index) => {
        const from = String(edge.from || '');
        const to = String(edge.to || '');
        return {
          id: String(edge.id || `edge_${index}`),
          source: from.split('.')[0],
          target: to.split('.')[0],
          label: String(edge.label || ''),
          markerEnd: { type: MarkerType.ArrowClosed }
        };
      });
    }
    return [
      { id: 'source-capture', source: 'source', target: 'capture', label: 'pixels + sync tags', markerEnd: { type: MarkerType.ArrowClosed } },
      { id: 'capture-mirror', source: 'capture', target: 'mirror', label: 'frame_stream', markerEnd: { type: MarkerType.ArrowClosed } },
      { id: 'mirror-destination', source: 'mirror', target: 'destination', label: 'spi transactions', markerEnd: { type: MarkerType.ArrowClosed } },
      { id: 'mirror-telemetry', source: 'mirror', target: 'telemetry', label: 'counters/events', markerEnd: { type: MarkerType.ArrowClosed }, animated: true }
    ];
  }, [project]);

  const visibleNodeIds = useMemo(() => {
    if (graphTool === 'select') return new Set(flowNodes.map((node) => node.id));
    return new Set(flowNodes.filter((node) => {
      const type = String(node.data?.type || '');
      if (graphTool === 'resources') {
        return ['system_intent', 'lab_function_block', 'esp32p4_resource'].includes(type) || type.includes('capture');
      }
      if (graphTool === 'flow') {
        return type !== 'esp32p4_resource';
      }
      if (graphTool === 'build') {
        return ['system_intent', 'lab_function_block'].includes(type);
      }
      return true;
    }).map((node) => node.id));
  }, [flowNodes, graphTool]);

  const visibleFlowNodes = useMemo(() => flowNodes.filter((node) => visibleNodeIds.has(node.id)), [flowNodes, visibleNodeIds]);

  const visibleFlowEdges = useMemo(() => flowEdges.filter((edge) => {
    if (!visibleNodeIds.has(edge.source) || !visibleNodeIds.has(edge.target)) return false;
    const label = String(edge.label || '');
    if (graphTool === 'resources') return ['claims', 'uses'].includes(label) || label.includes('DMA') || label.includes('resource');
    if (graphTool === 'flow') return !['claims', 'uses'].includes(label);
    if (graphTool === 'build') return label === 'uses';
    return true;
  }), [flowEdges, graphTool, visibleNodeIds]);

  const selectedRawNode = useMemo(() => {
    if (!selectedGraphNode) return null;
    const raw = selectedGraphNode.data?.raw;
    return raw && typeof raw === 'object' ? raw as Record<string, unknown> : null;
  }, [selectedGraphNode]);

  const selectedNodeParams = selectedRawNode?.params && typeof selectedRawNode.params === 'object'
    ? selectedRawNode.params as Record<string, unknown>
    : {};
  const selectedNodeOverlay = selectedNodeParams.overlay && typeof selectedNodeParams.overlay === 'object'
    ? selectedNodeParams.overlay as Record<string, unknown>
    : {};
  const selectedNodeSpec = selectedRawNode ? resolveGraphBlockSpec(selectedRawNode) : null;
  const selectedNodeOverlayParams = selectedNodeOverlay.params && typeof selectedNodeOverlay.params === 'object'
    ? selectedNodeOverlay.params as Record<string, unknown>
    : {};
  const selectedNodeTelemetry = selectedNodeOverlay.telemetry && typeof selectedNodeOverlay.telemetry === 'object'
    ? selectedNodeOverlay.telemetry as Record<string, unknown>
    : {};
  const selectedNodeId = selectedRawNode ? String(selectedRawNode.id || selectedGraphNode?.id || '') : '';
  const selectedNodeType = selectedRawNode ? String(selectedRawNode.type || 'block') : '';
  const selectedNodeLabel = selectedRawNode ? String(selectedRawNode.label || selectedNodeId || 'Block') : '';
  const selectedNodeLane = selectedRawNode ? graphNodeCategory(selectedRawNode, selectedNodeType) : '';
  const selectedNodePorts = selectedRawNode?.ports && typeof selectedRawNode.ports === 'object'
    ? selectedRawNode.ports as Record<string, unknown>
    : {};
  const selectedComponents = Array.isArray(selectedNodeParams.components)
    ? selectedNodeParams.components.map((component) => String(component))
    : [];
  const selectedOverlayKeys = Object.keys(selectedNodeOverlay);
  const selectedConnections = selectedNodeId
    ? flowEdges
      .filter((edge) => edge.source === selectedNodeId || edge.target === selectedNodeId)
      .map((edge) => ({
        key: edge.id,
        direction: edge.source === selectedNodeId ? 'out' : 'in',
        peer: edge.source === selectedNodeId ? edge.target : edge.source,
        label: String(edge.label || 'edge')
      }))
    : [];
  const selectedProjectBlockRows = useMemo(() => projectBlockRows(project, blocks), [project, blocks]);
  const filteredLibraryEntries = useMemo(() => {
    const query = graphLibraryQuery.trim().toLowerCase();
    if (!query) return libraryEntries;
    return libraryEntries.filter((entry) =>
      entry.spec.title.toLowerCase().includes(query)
      || entry.spec.category.toLowerCase().includes(query)
      || entry.spec.summary.toLowerCase().includes(query)
      || entry.spec.capabilities.some((capability) => capability.toLowerCase().includes(query))
      || entry.nodeTemplate.type.toLowerCase().includes(query)
    );
  }, [graphLibraryQuery, libraryEntries]);
  const rootedLibraryEntries = useMemo(() => {
    const byRoot = filteredLibraryEntries.filter((entry) =>
      libraryRoot === 'intent'
        ? entry.family === 'intent'
        : libraryRoot === 'sdf'
          ? entry.family === 'sdf'
          : libraryRoot === 'sdk'
            ? entry.family === 'sdk'
            : libraryRoot === 'resource'
              ? entry.family === 'resource'
              : entry.family === 'external'
    );
    const grouped = new Map<string, GraphLibraryEntry[]>();
    for (const entry of byRoot) {
      const key = entry.spec.category;
      grouped.set(key, [...(grouped.get(key) || []), entry]);
    }
    return Array.from(grouped.entries()).map(([category, entries]) => ({ category, entries }));
  }, [filteredLibraryEntries, libraryRoot]);

  const updateSelectedNodeOverlay = (patch: Record<string, unknown>) => {
    if (!project?.graph?.nodes || !selectedGraphNode) return;
    let nextSelectedRaw: Record<string, unknown> | null = null;
    const nextProject: LabProject = {
      ...project,
      graph: {
        ...project.graph,
        nodes: project.graph.nodes.map((rawNode) => {
          if (String(rawNode.id) !== selectedGraphNode.id) return rawNode;
          const params = rawNode.params && typeof rawNode.params === 'object' ? rawNode.params as Record<string, unknown> : {};
          const overlay = params.overlay && typeof params.overlay === 'object' ? params.overlay as Record<string, unknown> : {};
          nextSelectedRaw = {
            ...rawNode,
            params: {
              ...params,
              overlay: {
                ...overlay,
                ...patch,
                source_write_policy: 'overlay_only'
              }
            }
          };
          return nextSelectedRaw;
        }),
        edges: project.graph.edges || []
      }
    };
    setGraphDirty(true);
    onProjectDraftChange(nextProject);
    if (nextSelectedRaw) {
      setSelectedGraphNode((current) => current && current.id === selectedGraphNode.id
        ? { ...current, data: { ...current.data, raw: nextSelectedRaw } }
        : current);
    }
  };

  const updateSelectedNodeOverlayParams = (patch: Record<string, unknown>) => {
    updateSelectedNodeOverlay({
      params: {
        ...selectedNodeOverlayParams,
        ...patch
      }
    });
  };

  const updateSelectedNodeTelemetry = (patch: Record<string, unknown>) => {
    updateSelectedNodeOverlay({
      telemetry: {
        ...selectedNodeTelemetry,
        ...patch
      }
    });
  };

  const fieldValueForSpec = (field: BlockFieldSpec) => {
    if (selectedNodeOverlayParams[field.key] !== undefined) return selectedNodeOverlayParams[field.key];
    if (selectedNodeOverlay[field.key] !== undefined) return selectedNodeOverlay[field.key];
    return field.defaultValue;
  };

  const updateNodePosition = (node: Node) => {
    if (!project?.graph?.nodes) return;
    const nextProject: LabProject = {
      ...project,
      graph: {
        ...project.graph,
        nodes: project.graph.nodes.map((rawNode) => String(rawNode.id) === node.id
          ? { ...rawNode, position: { x: Math.round(node.position.x), y: Math.round(node.position.y) } }
          : rawNode),
        edges: project.graph.edges || []
      }
    };
    setGraphDirty(true);
    onProjectDraftChange(nextProject);
  };

  const saveGraphLayout = async () => {
    if (!project) return;
    await onSaveProject(project);
    setGraphDirty(false);
  };

  useEffect(() => {
    if (graphTool !== 'select') {
      setPaletteOpen(false);
      return;
    }
    setPaletteOpen(true);
  }, [graphTool]);

  const addGraphNodeFromLibrary = (entry: GraphLibraryEntry) => {
    if (!project) return;
    const existingNodes = Array.isArray(project.graph?.nodes) ? project.graph.nodes : [];
    const existingEdges = Array.isArray(project.graph?.edges) ? project.graph.edges : [];
    const nextIndex = existingNodes.length + 1;
    const laneMeta = graphLanes.find((lane) => lane.id === entry.lane) || graphLanes[2];
    const laneNodes = existingNodes.filter((node) => {
      const type = String(node.type || '');
      const lane = graphLaneForNode(node, type);
      return lane === entry.lane;
    }).length;
    const baseId = `${entry.nodeTemplate.type}_${nextIndex}`;
    const nextNode = {
      id: baseId,
      type: entry.nodeTemplate.type,
      label: entry.nodeTemplate.label,
      position: {
        x: laneMeta.x,
        y: 140 + laneNodes * 170
      },
      ports: entry.nodeTemplate.ports || {},
      params: entry.nodeTemplate.params || {}
    };
    const nextProject: LabProject = {
      ...project,
      graph: {
        nodes: [...existingNodes, nextNode],
        edges: existingEdges
      }
    };
    setGraphDirty(true);
    onProjectDraftChange(nextProject);
    setSelectedGraphNode({
      id: baseId,
      position: nextNode.position,
      data: {
        raw: nextNode,
        type: entry.nodeTemplate.type,
        lane: entry.lane,
        label: (
          <div className="flowNode">
            <div className="flowNodeMeta">
              <Tag color={graphNodeColor(entry.nodeTemplate.type)}>{entry.nodeTemplate.type}</Tag>
              <Tag>{laneMeta.label}</Tag>
            </div>
            <Text strong>{entry.nodeTemplate.label}</Text>
            <Text type="secondary">{entry.spec.summary}</Text>
          </div>
        )
      }
    } as Node);
    setInspectorOpen(true);
  };

  const renderSpecField = (field: BlockFieldSpec) => {
    const value = fieldValueForSpec(field);
    const commonLabel = <Text className="editorLabel">{field.label}</Text>;

    if (field.type === 'switch') {
      return (
        <Space className="fullWidth" align="center" key={field.key}>
          {commonLabel}
          <Switch
            checked={Boolean(value)}
            onChange={(checked) => updateSelectedNodeOverlayParams({ [field.key]: checked })}
          />
        </Space>
      );
    }

    if (field.type === 'textarea') {
      return (
        <div key={field.key}>
          {commonLabel}
          <Input.TextArea
            rows={3}
            value={String(value ?? '')}
            onChange={(event) => updateSelectedNodeOverlayParams({ [field.key]: event.target.value })}
            placeholder={field.description || ''}
          />
        </div>
      );
    }

    if (field.type === 'number') {
      return (
        <div key={field.key}>
          <Space className="editorHeaderRow">
            {commonLabel}
            <InputNumber
              min={field.min}
              max={field.max}
              step={field.step}
              value={typeof value === 'number' ? value : Number(value ?? field.defaultValue ?? 0)}
              addonAfter={field.unit}
              onChange={(next) => updateSelectedNodeOverlayParams({ [field.key]: Number(next ?? field.defaultValue ?? 0) })}
            />
          </Space>
        </div>
      );
    }

    if (field.type === 'select') {
      return (
        <div key={field.key}>
          {commonLabel}
          <Select
            className="fullWidth"
            value={String(value ?? field.defaultValue ?? '')}
            options={field.options || []}
            onChange={(next) => updateSelectedNodeOverlayParams({ [field.key]: next })}
          />
        </div>
      );
    }

    return (
      <div key={field.key}>
        {commonLabel}
        <Input
          value={String(value ?? '')}
          onChange={(event) => updateSelectedNodeOverlayParams({ [field.key]: event.target.value })}
          placeholder={field.description || ''}
        />
      </div>
    );
  };

  return (
    <div className={`graphWorkspace ${inspectorOpen ? 'inspectorOpen' : ''}`}>
      <div className="graphTopBar">
        <Space>
          <Text strong>Project Flowgraph</Text>
          <Tag color="purple">{graphTool}</Tag>
          {graphDirty ? <Tag color="orange">layout draft</Tag> : null}
        </Space>
        <Space.Compact>
          <Tooltip title="Select and inspect">
            <Button size="small" type={graphTool === 'select' ? 'primary' : 'default'} icon={<CheckCircleOutlined />} onClick={() => setGraphTool('select')} />
          </Tooltip>
          <Tooltip title="Resource view">
            <Button size="small" type={graphTool === 'resources' ? 'primary' : 'default'} icon={<DatabaseOutlined />} onClick={() => setGraphTool('resources')} />
          </Tooltip>
          <Tooltip title="Block library">
            <Button size="small" type={paletteOpen && graphTool === 'select' ? 'primary' : 'default'} icon={<ApartmentOutlined />} onClick={() => {
              setGraphTool('select');
              setPaletteOpen((open) => !open);
            }} />
          </Tooltip>
          <Tooltip title="Signal/dataflow view">
            <Button size="small" type={graphTool === 'flow' ? 'primary' : 'default'} icon={<ApiOutlined />} onClick={() => setGraphTool('flow')} />
          </Tooltip>
          <Tooltip title="Build/config view">
            <Button size="small" type={graphTool === 'build' ? 'primary' : 'default'} icon={<BugOutlined />} onClick={() => setGraphTool('build')} />
          </Tooltip>
          <Tooltip title="Inspector">
            <Button size="small" type={inspectorOpen ? 'primary' : 'default'} icon={<FileSearchOutlined />} onClick={() => setInspectorOpen((open) => !open)} />
          </Tooltip>
          <Tooltip title="Save graph layout">
            <Button size="small" icon={<SaveOutlined />} disabled={!graphDirty || !project} onClick={saveGraphLayout} />
          </Tooltip>
        </Space.Compact>
      </div>
      <div className={`graphPaletteDrawer ${paletteOpen ? 'open' : ''}`}>
        <div className="drawerTitle">
          <Text strong>Select And Build</Text>
          <Button size="small" onClick={() => setPaletteOpen(false)}>Close</Button>
        </div>
        <Search
          allowClear
          size="small"
          placeholder="Search blocks, resources, APIs"
          value={graphLibraryQuery}
          onChange={(event) => setGraphLibraryQuery(event.target.value)}
        />
        <div className="graphLibraryWorkbench">
          <div className="graphLibraryRoots">
            {libraryRoots.map((root) => (
              <Tooltip title={root.label} key={root.key}>
                <Button
                  size="small"
                  type={libraryRoot === root.key ? 'primary' : 'default'}
                  icon={root.icon}
                  onClick={() => setLibraryRoot(root.key)}
                />
              </Tooltip>
            ))}
          </div>
          <div className="graphLibraryTree">
            <Collapse
              size="small"
              ghost
              items={rootedLibraryEntries.map((group) => ({
                key: group.category,
                label: group.category,
                children: (
                  <div className="graphLibraryList">
                    {group.entries.map((entry) => (
                      <Card
                        key={entry.id}
                        size="small"
                        className="graphLibraryCard"
                        extra={<Button size="small" type="text" onClick={() => addGraphNodeFromLibrary(entry)}>Add</Button>}
                      >
                        <Space direction="vertical" size={2} className="fullWidth">
                          <Space wrap size={[4, 4]}>
                            <Tag>{entry.nodeTemplate.type}</Tag>
                            <Tag>{entry.spec.category}</Tag>
                          </Space>
                          <Text strong>{entry.spec.title}</Text>
                          <Text type="secondary">{entry.spec.summary}</Text>
                        </Space>
                      </Card>
                    ))}
                  </div>
                )
              }))}
            />
            {rootedLibraryEntries.length === 0 ? <Empty image={Empty.PRESENTED_IMAGE_SIMPLE} description="No matching blocks" /> : null}
          </div>
        </div>
      </div>
      <div className="graphSurface">
        <div className="graphLaneOverlay" aria-hidden="true">
          {graphLanes.map((lane) => (
            <div className="graphLanePill" key={lane.id}>
              <Tag color={lane.color}>{lane.label}</Tag>
            </div>
          ))}
        </div>
        <div className="flowCanvas">
          <ReactFlow
            nodes={visibleFlowNodes}
            edges={visibleFlowEdges}
            fitView
            nodesDraggable
            nodesConnectable={false}
            elementsSelectable
            onNodeClick={(_event, node) => {
              setSelectedGraphNode(node);
              setInspectorOpen(true);
            }}
            onNodeDragStop={(_event, node) => updateNodePosition(node)}
          >
            <Background />
            <Controls showInteractive={false} />
          </ReactFlow>
        </div>
      </div>
      <div className="graphInspectorDrawer">
        <div className="drawerTitle">
          <Text strong>Inspector</Text>
          <Button size="small" onClick={() => setInspectorOpen(false)}>Close</Button>
        </div>
        <Tabs
          size="small"
          items={[
            {
              key: 'selection',
              label: 'Block',
              children: selectedRawNode ? (
                <Space direction="vertical" size="middle" className="fullWidth">
                  <Card size="small" className="inspectorSummaryCard">
                    <Space direction="vertical" size="small" className="fullWidth">
                      <Space wrap>
                        <Tag color={graphNodeColor(selectedNodeType)}>{selectedNodeType}</Tag>
                        {selectedNodeLane ? <Tag>{selectedNodeLane}</Tag> : null}
                        <Text strong>{selectedNodeLabel}</Text>
                      </Space>
                      <Text type="secondary">{selectedNodeId}</Text>
                      <Space wrap>
                        <Tag color={selectedNodeSpec ? 'green' : 'default'}>{selectedNodeSpec ? 'typed editor' : 'read-only inspector'}</Tag>
                        {selectedNodeOverlay.source_write_policy ? <Tag color="blue">{String(selectedNodeOverlay.source_write_policy)}</Tag> : null}
                        {graphDirty ? <Tag color="orange">unsaved overlay/layout</Tag> : null}
                      </Space>
                    </Space>
                  </Card>
                  {selectedNodeSpec ? (
                    <Space direction="vertical" size="small" className="fullWidth">
                      <Card size="small" title={selectedNodeSpec.title} className="inspectorEditorCard">
                        <Space direction="vertical" size="small" className="fullWidth">
                          <Text type="secondary">{selectedNodeSpec.summary}</Text>
                          <div className="tagSection">
                            <Text type="secondary">Capabilities</Text>
                            <Space wrap size={[4, 4]}>
                              {selectedNodeSpec.capabilities.map((capability) => <Tag key={capability}>{capability}</Tag>)}
                            </Space>
                          </div>
                        </Space>
                      </Card>
                      <Card size="small" title="Parameters" className="inspectorEditorCard">
                        <Space direction="vertical" size="middle" className="fullWidth">
                          {selectedNodeSpec.parameterFields.map((field) => renderSpecField(field))}
                        </Space>
                      </Card>
                      <Card size="small" title="Telemetry" className="inspectorEditorCard">
                        <Space direction="vertical" size="middle" className="fullWidth">
                          <Space className="fullWidth" align="center">
                            <Text className="editorLabel">Enabled</Text>
                            <Switch
                              checked={Boolean(selectedNodeTelemetry.enabled)}
                              onChange={(enabled) => updateSelectedNodeTelemetry({ enabled })}
                            />
                            <Tag color={selectedNodeTelemetry.enabled ? 'green' : 'default'}>{selectedNodeTelemetry.enabled ? 'armed' : 'off'}</Tag>
                          </Space>
                          <div>
                            <Space className="editorHeaderRow">
                              <Text className="editorLabel">Window</Text>
                              <InputNumber
                                min={50}
                                max={5000}
                                step={50}
                                addonAfter="ms"
                                value={typeof selectedNodeTelemetry.sample_window_ms === 'number' ? selectedNodeTelemetry.sample_window_ms : 250}
                                onChange={(value) => updateSelectedNodeTelemetry({ sample_window_ms: Number(value || 250) })}
                              />
                            </Space>
                          </div>
                          <div>
                            <Text className="editorLabel">Emit mode</Text>
                            <Select
                              className="fullWidth"
                              value={String(selectedNodeTelemetry.emit_mode || 'delta')}
                              options={[
                                { value: 'delta', label: 'Delta' },
                                { value: 'snapshot', label: 'Snapshot' },
                                { value: 'threshold', label: 'Threshold' }
                              ]}
                              onChange={(emit_mode) => updateSelectedNodeTelemetry({ emit_mode })}
                            />
                          </div>
                          <div>
                            <Text className="editorLabel">Telemetry notes</Text>
                            <Input.TextArea
                              rows={2}
                              value={String(selectedNodeTelemetry.notes || '')}
                              onChange={(event) => updateSelectedNodeTelemetry({ notes: event.target.value })}
                              placeholder="What this block should expose during telemetry builds."
                            />
                          </div>
                          <div className="tagSection">
                            <Text type="secondary">Channels</Text>
                            <Space direction="vertical" size="small" className="fullWidth">
                              {selectedNodeSpec.telemetryChannels.map((channel) => (
                                <Space key={channel.key} className="fullWidth" align="center">
                                  <Switch
                                    checked={Boolean(selectedNodeTelemetry[channel.key])}
                                    onChange={(enabled) => updateSelectedNodeTelemetry({ [channel.key]: enabled })}
                                  />
                                  <Text strong>{channel.label}</Text>
                                  <Text type="secondary">{channel.description}</Text>
                                </Space>
                              ))}
                            </Space>
                          </div>
                        </Space>
                      </Card>
                    </Space>
                  ) : (
                    <Alert
                      type="info"
                      showIcon
                      message="No typed editor yet"
                      description="This block is still useful for resource and connection review. Editing will be added with an overlay contract before any generated source writes are allowed."
                    />
                  )}
                  <Collapse
                    size="small"
                    className="compactCollapse"
                    items={[
                      {
                        key: 'interfaces',
                        label: 'Interfaces and connections',
                        children: (
                          <Space direction="vertical" size="small" className="fullWidth">
                            {Object.keys(selectedNodePorts).length > 0 ? <JsonBlock value={selectedNodePorts} /> : <Text type="secondary">No explicit ports in this block yet.</Text>}
                            <Table
                              size="small"
                              pagination={false}
                              dataSource={selectedConnections}
                              columns={[
                                { title: 'Dir', dataIndex: 'direction', width: 48, render: (direction: string) => <Tag>{direction}</Tag> },
                                { title: 'Peer', dataIndex: 'peer' },
                                { title: 'Kind', dataIndex: 'label', width: 92 }
                              ]}
                            />
                          </Space>
                        )
                      },
                      {
                        key: 'metadata',
                        label: 'Metadata',
                        children: (
                          <Space direction="vertical" size="small" className="fullWidth">
                            <Descriptions size="small" bordered column={1}>
                              <Descriptions.Item label="API group">{String(selectedNodeParams.api_group || 'n/a')}</Descriptions.Item>
                              <Descriptions.Item label="Overlay fields">{selectedOverlayKeys.length ? selectedOverlayKeys.join(', ') : 'none'}</Descriptions.Item>
                            </Descriptions>
                            {selectedComponents.length > 0 ? (
                              <div className="tagSection">
                                <Text type="secondary">Components</Text>
                                <Space wrap size={[4, 4]}>
                                  {selectedComponents.map((component) => <Tag key={component}>{component}</Tag>)}
                                </Space>
                              </div>
                            ) : null}
                          </Space>
                        )
                      },
                      {
                        key: 'debug',
                        label: 'Debug JSON',
                        children: <JsonBlock value={selectedRawNode} />
                      }
                    ]}
                  />
                </Space>
              ) : <Empty image={Empty.PRESENTED_IMAGE_SIMPLE} description="Select a graph node" />
            },
            {
              key: 'project',
              label: 'Context',
              children: (
                <Space direction="vertical" size="small" className="fullWidth">
                  <Descriptions bordered size="small" column={1}>
                    <Descriptions.Item label="Project">{project?.name || '?'}</Descriptions.Item>
                    <Descriptions.Item label="Status">{project?.status || '?'}</Descriptions.Item>
                    <Descriptions.Item label="Ingress role">{project?.source?.block || '?'}</Descriptions.Item>
                    <Descriptions.Item label="Egress role">{project?.destination?.block || '?'}</Descriptions.Item>
                    <Descriptions.Item label="Graph nodes">{project?.graph?.nodes?.length ?? 0}</Descriptions.Item>
                    <Descriptions.Item label="Graph edges">{project?.graph?.edges?.length ?? 0}</Descriptions.Item>
                    <Descriptions.Item label="Device">{status?.device_connected ? 'online' : 'offline mode'}</Descriptions.Item>
                  </Descriptions>
                  <Text type="secondary">Context is project-level metadata. Block edits stay in node overlays until generation is explicit.</Text>
                </Space>
              )
            },
            {
              key: 'sdk',
              label: 'SDK',
              children: project?.sdk_example ? (
                <Space direction="vertical" size="small" className="fullWidth">
                  <Descriptions bordered size="small" column={1}>
                    <Descriptions.Item label="Example">{String(project.sdk_example.id || '?')}</Descriptions.Item>
                    <Descriptions.Item label="Path">{String(project.sdk_example.path || '?')}</Descriptions.Item>
                    <Descriptions.Item label="Read only">{String(project.sdk_example.read_only ?? true)}</Descriptions.Item>
                  </Descriptions>
                  <Collapse
                    size="small"
                    className="compactCollapse"
                    items={[
                      { key: 'source', label: 'Source files', children: <JsonBlock value={project.sdk_example.source_files || []} /> },
                      { key: 'config', label: 'CMake and sdkconfig', children: <JsonBlock value={{ cmake_files: project.sdk_example.cmake_files || [], sdkconfig_defaults: project.sdk_example.sdkconfig_defaults || [] }} /> },
                      { key: 'raw', label: 'SDK metadata JSON', children: <JsonBlock value={project.sdk_example} /> }
                    ]}
                  />
                </Space>
              ) : <Empty image={Empty.PRESENTED_IMAGE_SIMPLE} description="No SDK example metadata" />
            },
            {
              key: 'blocks',
              label: 'Library',
              children: (
                <Space direction="vertical" size="small" className="fullWidth">
                  <Text type="secondary">Blocks from the selected project graph and declared ingress, transform, and egress roles.</Text>
                  <Table
                    size="small"
                    pagination={false}
                    dataSource={selectedProjectBlockRows}
                    locale={{ emptyText: <Empty image={Empty.PRESENTED_IMAGE_SIMPLE} description="No blocks in selected project" /> }}
                    columns={[
                      { title: 'Kind', dataIndex: 'kind', width: 92, render: (kind: string) => <Tag>{kind}</Tag> },
                      { title: 'Block', dataIndex: 'name' },
                      { title: 'Origin', dataIndex: 'origin', width: 82, render: (origin: string) => <Tag color={origin === 'graph' ? 'blue' : 'purple'}>{origin}</Tag> }
                    ]}
                  />
                  <Collapse
                    size="small"
                    className="compactCollapse"
                    items={[
                      {
                        key: 'registry',
                        label: 'Reusable registry candidates',
                        children: (
                          <Table
                            size="small"
                            pagination={false}
                            dataSource={blocks.map((block) => ({ ...block, key: block.id }))}
                            columns={[
                              { title: 'Kind', dataIndex: 'kind', width: 82, render: (kind: string) => <Tag>{kind}</Tag> },
                              { title: 'Block', dataIndex: 'name' },
                              { title: 'Status', dataIndex: 'status', width: 92, render: (blockStatus: string) => <Tag color={blockStatus.includes('validated') || blockStatus.includes('working') ? 'green' : 'blue'}>{blockStatus}</Tag> }
                            ]}
                          />
                        )
                      }
                    ]}
                  />
                </Space>
              )
            },
            {
              key: 'model',
              label: 'Model',
              children: (
                <Space direction="vertical" size="small">
                  <Text type="secondary">Inspector rules:</Text>
                  <Text type="secondary">1. Blocks expose typed overlay parameters first.</Text>
                  <Text type="secondary">2. Imported SDK files remain read-only until generation is explicitly requested.</Text>
                  <Text type="secondary">3. Connections, resource claims, and evidence stay visible even before a block has a full editor.</Text>
                  <Text type="secondary">4. Debug JSON is available, but it is not the primary authoring surface.</Text>
                  <Text type="secondary">5. Inspector Library is project-first; global blocks are only reusable candidates.</Text>
                </Space>
              )
            }
          ]}
        />
      </div>
    </div>
  );
}

function SdkResearchPage({
  embedded = false,
  onProjectsChanged,
  onOpenProject
}: {
  embedded?: boolean;
  onProjectsChanged: (projects: LabProject[], selectedProjectId?: string) => void;
  onOpenProject: (projectId: string, page?: string) => void;
}) {
  const [activeBrowser, setActiveBrowser] = useState<'examples' | 'repos'>('examples');
  const [sdk, setSdk] = useState<SdkInventorySummary | null>(null);
  const [examples, setExamples] = useState<SdkExample[]>([]);
  const [repos, setRepos] = useState<EspressifRepo[]>([]);
  const [repoSummary, setRepoSummary] = useState<Record<string, unknown> | null>(null);
  const [selectedExample, setSelectedExample] = useState<Record<string, unknown> | null>(null);
  const [selectedRepo, setSelectedRepo] = useState<EspressifRepo | null>(null);
  const [sdkBusy, setSdkBusy] = useState(false);
  const [repoBusy, setRepoBusy] = useState(false);
  const [importBusy, setImportBusy] = useState(false);
  const [importResult, setImportResult] = useState<Record<string, unknown> | null>(null);
  const [sdkError, setSdkError] = useState('');
  const [repoError, setRepoError] = useState('');
  const [exampleFilters, setExampleFilters] = useState({
    q: '',
    relevance: 'high',
    category: '',
    api_group: '',
    mcu_block: ''
  });
  const [repoFilters, setRepoFilters] = useState({
    q: '',
    relevance: 'high',
    category: ''
  });

  const loadSdk = async () => {
    setSdkBusy(true);
    try {
      const [summary, exampleResult] = await Promise.all([
        api.sdkIdf(),
        api.sdkExamples({ ...exampleFilters, limit: 250 })
      ]);
      setSdk(summary);
      setExamples(exampleResult.examples);
      setSdkError('');
    } catch (error) {
      setSdkError(error instanceof Error ? error.message : String(error));
    } finally {
      setSdkBusy(false);
    }
  };

  const loadRepos = async () => {
    setRepoBusy(true);
    try {
      const result = await api.espressifRepos({ ...repoFilters, limit: 250 });
      setRepos(result.repositories);
      setRepoSummary(result.summary);
      setRepoError('');
    } catch (error) {
      setRepoError(error instanceof Error ? error.message : String(error));
    } finally {
      setRepoBusy(false);
    }
  };

  useEffect(() => {
    loadSdk();
  }, [exampleFilters.relevance, exampleFilters.category, exampleFilters.api_group, exampleFilters.mcu_block]);

  useEffect(() => {
    loadRepos();
  }, [repoFilters.relevance, repoFilters.category]);

  const loadExampleDetail = async (id: string) => {
    setSdkBusy(true);
    try {
      const result = await api.sdkExample(id);
      setSelectedExample(result.example);
      setSdkError('');
    } catch (error) {
      setSdkError(error instanceof Error ? error.message : String(error));
    } finally {
      setSdkBusy(false);
    }
  };

  const importSelectedExample = async () => {
    const exampleId = String(selectedExample?.id || '');
    if (!exampleId) return;
    setImportBusy(true);
    try {
      const result = await api.importIdfExample(exampleId);
      setImportResult({ ok: true, project: result.project?.id, example: exampleId });
      onProjectsChanged(result.projects, result.project?.id);
      if (result.project?.id) {
        onOpenProject(result.project.id, embedded ? 'project' : 'graph');
      }
    } catch (error) {
      setImportResult({ ok: false, example: exampleId, error: error instanceof Error ? error.message : String(error) });
    } finally {
      setImportBusy(false);
    }
  };

  const sdkCategoryOptions = useMemo(() => {
    const categories = Object.keys(sdk?.summary.examples_by_category || {});
    return [{ value: '', label: 'All categories' }, ...categories.map((category) => ({ value: category, label: category }))];
  }, [sdk]);

  const sdkApiOptions = useMemo(() => {
    const apiGroups = Object.keys(sdk?.summary.example_api_group_hits || {});
    return [{ value: '', label: 'All APIs' }, ...apiGroups.map((group) => ({ value: group, label: group }))];
  }, [sdk]);

  const sdkBlockOptions = useMemo(() => {
    const blocks = Object.keys(sdk?.summary.example_mcu_block_hits || {});
    return [{ value: '', label: 'All MCU blocks' }, ...blocks.map((block) => ({ value: block, label: block }))];
  }, [sdk]);

  const repoCategoryOptions = [
    { value: '', label: 'All categories' },
    { value: 'core_sdk', label: 'core_sdk' },
    { value: 'ai_agent', label: 'ai_agent' },
    { value: 'display_camera_video', label: 'display_camera_video' },
    { value: 'usb_transport', label: 'usb_transport' },
    { value: 'examples_components', label: 'examples_components' },
    { value: 'tooling_ide_ci', label: 'tooling_ide_ci' },
    { value: 'hardware_boards', label: 'hardware_boards' }
  ];

  const exampleTable = (
    <Space direction="vertical" size="small" className="fullWidth">
      <div className="sdkFilterRow">
        <Search
          allowClear
          placeholder="Search examples, APIs, blocks"
          onSearch={(q) => setExampleFilters((current) => ({ ...current, q }))}
        />
        <Select value={exampleFilters.relevance} options={[
          { value: '', label: 'All relevance' },
          { value: 'high', label: 'High' },
          { value: 'medium', label: 'Medium' },
          { value: 'track', label: 'Track' }
        ]} onChange={(relevance) => setExampleFilters((current) => ({ ...current, relevance }))} />
        <Select showSearch value={exampleFilters.category} options={sdkCategoryOptions} onChange={(category) => setExampleFilters((current) => ({ ...current, category }))} />
        <Select showSearch value={exampleFilters.api_group} options={sdkApiOptions} onChange={(api_group) => setExampleFilters((current) => ({ ...current, api_group }))} />
        <Select showSearch value={exampleFilters.mcu_block} options={sdkBlockOptions} onChange={(mcu_block) => setExampleFilters((current) => ({ ...current, mcu_block }))} />
        <Tooltip title="Apply filters">
          <Button icon={<FilterOutlined />} loading={sdkBusy} onClick={loadSdk} aria-label="Apply example filters" />
        </Tooltip>
      </div>
      <Table
        size="small"
        rowKey="id"
        loading={sdkBusy}
        dataSource={examples}
        pagination={{ pageSize: 14, size: 'small', showSizeChanger: false }}
        rowClassName={(record) => record.id === selectedExample?.id ? 'selectedTableRow' : 'clickableTableRow'}
        onRow={(record) => ({ onClick: () => loadExampleDetail(record.id) })}
        columns={[
          {
            title: 'Example',
            dataIndex: 'id',
            render: (id: string, row: SdkExample) => (
              <Space direction="vertical" size={0}>
                <Text strong>{id}</Text>
                <Text type="secondary">{row.path}</Text>
              </Space>
            )
          },
          { title: 'Rel', dataIndex: 'relevance', width: 66, render: (value: string) => <Tag color={value === 'high' ? 'green' : value === 'medium' ? 'blue' : 'default'}>{value}</Tag> },
          { title: 'Files', dataIndex: 'source_file_count', width: 58 },
          { title: 'APIs', dataIndex: 'api_groups', width: 176, render: (items: string[]) => <Space wrap size={2}>{items.slice(0, 3).map((item) => <Tag key={item}>{item}</Tag>)}</Space> },
          { title: 'MCU', dataIndex: 'mcu_blocks', width: 210, render: (items: string[]) => <Space wrap size={2}>{items.slice(0, 3).map((item) => <Tag color="purple" key={item}>{item}</Tag>)}</Space> }
        ]}
      />
    </Space>
  );

  const repoTable = (
    <Space direction="vertical" size="small" className="fullWidth">
      <div className="repoFilterRow">
        <Search
          allowClear
          placeholder="Search repos"
          onSearch={(q) => setRepoFilters((current) => ({ ...current, q }))}
        />
        <Select value={repoFilters.relevance} options={[
          { value: '', label: 'All relevance' },
          { value: 'high', label: 'High' },
          { value: 'medium', label: 'Medium' },
          { value: 'track', label: 'Track' }
        ]} onChange={(relevance) => setRepoFilters((current) => ({ ...current, relevance }))} />
        <Select showSearch value={repoFilters.category} options={repoCategoryOptions} onChange={(category) => setRepoFilters((current) => ({ ...current, category }))} />
        <Tooltip title="Apply filters">
          <Button icon={<FilterOutlined />} loading={repoBusy} onClick={loadRepos} aria-label="Apply repository filters" />
        </Tooltip>
      </div>
      <Table
        size="small"
        rowKey="full_name"
        loading={repoBusy}
        dataSource={repos}
        pagination={{ pageSize: 12, size: 'small', showSizeChanger: false }}
        rowClassName={(record) => record.full_name === selectedRepo?.full_name ? 'selectedTableRow' : 'clickableTableRow'}
        onRow={(record) => ({ onClick: () => setSelectedRepo(record) })}
        columns={[
          {
            title: 'Repository',
            dataIndex: 'full_name',
            render: (name: string, row: EspressifRepo) => (
              <Space direction="vertical" size={0}>
                <Text strong>{name}</Text>
                <Text type="secondary">{row.description || 'No description'}</Text>
              </Space>
            )
          },
          { title: 'Rel', dataIndex: 'relevance', width: 66, render: (value: string) => <Tag color={value === 'high' ? 'green' : value === 'medium' ? 'blue' : 'default'}>{value}</Tag> },
          { title: 'Stars', dataIndex: 'stars', width: 66 },
          { title: 'Tags', dataIndex: 'categories', width: 190, render: (items: string[]) => <Space wrap size={2}>{items.slice(0, 2).map((item) => <Tag key={item}>{item}</Tag>)}</Space> }
        ]}
      />
    </Space>
  );

  const inspector = activeBrowser === 'examples' ? (
    selectedExample ? (
      <Tabs
        size="small"
        items={[
          {
            key: 'summary',
            label: 'Summary',
            children: (
              <Space direction="vertical" size="small" className="fullWidth">
                <Descriptions size="small" bordered column={1}>
                  <Descriptions.Item label="ID">{String(selectedExample.id || '')}</Descriptions.Item>
                  <Descriptions.Item label="Path">{String(selectedExample.path || '')}</Descriptions.Item>
                  <Descriptions.Item label="Source files">{String(selectedExample.source_file_count || 0)}</Descriptions.Item>
                </Descriptions>
                <Space.Compact className="fullWidth">
                  <Button type="primary" icon={<RocketOutlined />} loading={importBusy} onClick={importSelectedExample}>Import</Button>
                  <Button icon={<ApiOutlined />} disabled={!selectedExample} onClick={() => selectedExample && loadExampleDetail(String(selectedExample.id || ''))}>Refresh</Button>
                </Space.Compact>
                {importResult ? <JsonBlock value={importResult} /> : null}
                <div className="tagSection">
                  <Text type="secondary">Components</Text>
                  <Space wrap>{((selectedExample.components as string[]) || []).slice(0, 18).map((item) => <Tag key={item}>{item}</Tag>)}</Space>
                </div>
                <div className="tagSection">
                  <Text type="secondary">MCU blocks</Text>
                  <Space wrap>{((selectedExample.mcu_blocks as string[]) || []).map((item) => <Tag color="purple" key={item}>{item}</Tag>)}</Space>
                </div>
              </Space>
            )
          },
          { key: 'files', label: 'Files', children: <JsonBlock value={{ source_files: selectedExample.source_files, sdkconfig_defaults: selectedExample.sdkconfig_defaults, cmake_files: selectedExample.cmake_files }} /> },
          { key: 'raw', label: 'Raw', children: <JsonBlock value={selectedExample} /> }
        ]}
      />
    ) : <Empty image={Empty.PRESENTED_IMAGE_SIMPLE} description="Select an ESP-IDF example" />
  ) : (
    selectedRepo ? (
      <Space direction="vertical" size="small" className="fullWidth">
        <Descriptions size="small" bordered column={1}>
          <Descriptions.Item label="Repo">{selectedRepo.full_name}</Descriptions.Item>
          <Descriptions.Item label="Description">{selectedRepo.description || 'No description'}</Descriptions.Item>
          <Descriptions.Item label="Language">{selectedRepo.language || '?'}</Descriptions.Item>
          <Descriptions.Item label="Stars">{selectedRepo.stars}</Descriptions.Item>
          <Descriptions.Item label="Forks">{selectedRepo.forks}</Descriptions.Item>
        </Descriptions>
        <Space wrap>{selectedRepo.categories.map((item) => <Tag key={item}>{item}</Tag>)}</Space>
        <Button type="primary" icon={<LinkOutlined />} href={selectedRepo.html_url} target="_blank" rel="noreferrer">Open</Button>
      </Space>
    ) : <Empty image={Empty.PRESENTED_IMAGE_SIMPLE} description="Select an Espressif repository" />
  );

  const content = (
    <Space direction="vertical" size="small" className="pageStack">
      <Card size="small" className="sdkSummaryCard">
        <div className="sdkSummaryBar">
          <MetricStrip items={[
            { label: 'Target', value: sdk?.source.target || 'esp32p4' },
            { label: 'Examples', value: sdk?.summary.example_count ?? '?' },
            { label: 'Components', value: sdk?.summary.component_count ?? '?' },
            { label: 'GitHub Repos', value: Number(repoSummary?.repo_count ?? repos.length) || '?' }
          ]} />
          <Space wrap className="sdkSummaryActions">
            <Tag>{sdk?.source.version?.git_describe || 'ESP-IDF'}</Tag>
            <Tooltip title={sdk?.source.idf_path || ''}><Tag color="blue">local SDK</Tag></Tooltip>
            <Tooltip title="Refresh inventories">
              <Button size="small" icon={<ReloadOutlined />} loading={sdkBusy || repoBusy} onClick={() => { loadSdk(); loadRepos(); }} aria-label="Refresh inventories" />
            </Tooltip>
          </Space>
        </div>
        {(sdkError || repoError) ? (
          <Space direction="vertical" size="small" className="fullWidth">
            {sdkError ? <Alert type="error" showIcon message="SDK inventory error" description={sdkError} /> : null}
            {repoError ? <Alert type="error" showIcon message="Repository inventory error" description={repoError} /> : null}
          </Space>
        ) : null}
      </Card>

      <div className="sdkWorkbench">
        <Card
          size="small"
          className="sdkBrowserCard"
          title={<Segmented value={activeBrowser} options={[
            { icon: <DatabaseOutlined />, label: `${examples.length}`, value: 'examples' },
            { icon: <GithubOutlined />, label: `${repos.length}`, value: 'repos' }
          ]} onChange={(value) => setActiveBrowser(value as 'examples' | 'repos')} />}
          extra={activeBrowser === 'examples'
            ? <Tag color="green">{exampleFilters.relevance || 'all'}</Tag>
            : <Tag color="green">{repoFilters.relevance || 'all'}</Tag>}
        >
          {activeBrowser === 'examples' ? exampleTable : repoTable}
        </Card>

        <Card size="small" className="sdkInspectorCard" title={activeBrowser === 'examples' ? 'Example' : 'Repository'}>
          {inspector}
        </Card>
      </div>
    </Space>
  );

  if (embedded) {
    return content;
  }

  return content;
}

function SourcePage({ profile, pins }: { profile: TargetProfile | null; pins: PinRow[] }) {
  const timingRows = (profile?.signals?.timing_or_control || []).map((row, index) => ({
    key: index,
    signal: row.name,
    gpio: row.current_esp32p4_gpio,
    bus: row.display_bus_pin ?? (row.display_bus_pins as unknown[] | undefined)?.join(', '),
    roles: (row.candidate_roles as string[] | undefined)?.join(', ')
  }));
  return (
    <Space direction="vertical" size="small" className="pageStack">
      <MetricStrip items={[
        { label: 'Profile', value: profile?.profile_id || 'unknown' },
        { label: 'GPIOs', value: pins.length },
        { label: 'Timing', value: timingRows.length },
        { label: 'Peripheral', value: profile?.current_capture_profile?.capture_peripheral || '?' }
      ]} />
      <Card size="small" title="Signal And Pin Profile">
        <Tabs
          size="small"
          items={[
            {
              key: 'gpios',
              label: 'GPIOs',
              children: (
                <Table
                  size="small"
                  pagination={false}
                  dataSource={pins.map((pin) => ({ ...pin, key: pin.gpio }))}
                  columns={[
                    { title: 'Signal', dataIndex: 'signal' },
                    { title: 'GPIO', dataIndex: 'gpio', width: 90 },
                    { title: 'Bus Pin', dataIndex: 'bus_pin', width: 100 },
                    { title: 'Role', dataIndex: 'role' }
                  ]}
                />
              )
            },
            {
              key: 'timing',
              label: 'Timing',
              children: (
                <Table
                  size="small"
                  pagination={false}
                  dataSource={timingRows}
                  columns={[
                    { title: 'Signal', dataIndex: 'signal', width: 100 },
                    { title: 'GPIO', dataIndex: 'gpio', width: 90 },
                    { title: 'Bus Pin', dataIndex: 'bus', width: 100 },
                    { title: 'Candidate Roles', dataIndex: 'roles' }
                  ]}
                />
              )
            },
            {
              key: 'safety',
              label: 'Safety',
              children: (
                <Space direction="vertical" className="fullWidth">
                  <Alert type="warning" showIcon message="Known concerns" description={(profile?.safety?.known_concerns || []).join(' ')} />
                  <Space wrap>
                    {(profile?.safety?.dangerous_rails || []).map((rail) => <Tag color="red" key={rail}>{rail}</Tag>)}
                  </Space>
                </Space>
              )
            }
          ]}
        />
      </Card>
    </Space>
  );
}

function ProcessingPage({ profile }: { profile: TargetProfile | null }) {
  return (
    <Space direction="vertical" size="middle" className="pageStack">
      <Alert type="info" showIcon message="No standalone runtime graph is active for this project" description="Runtime and transform blocks are authored as graph nodes with typed overlays, validation, and generated firmware previews." />
      <Card title="Active Lab Runtime">
        <Descriptions bordered size="small" column={1}>
          <Descriptions.Item label="Block">profile runtime adapter</Descriptions.Item>
          <Descriptions.Item label="Backend">{profile?.current_capture_profile?.capture_peripheral || 'LCD_CAM'}</Descriptions.Item>
          <Descriptions.Item label="Transport"><JsonBlock value={profile?.current_capture_profile?.transport} /></Descriptions.Item>
        </Descriptions>
      </Card>
    </Space>
  );
}

function DestinationPage({ destinationProfile, sourcePins }: { destinationProfile: DestinationProfile | null; sourcePins: PinRow[] }) {
  const destination = destinationProfile?.destination || {};
  const spi = destination.spi || {};
  const orientation = destination.orientation || {};
  const color = destination.color || {};
  const unknowns = destinationProfile?.unknowns || [];
  const [pinRows, setPinRows] = useState<DestinationPinDraft[]>([]);
  const [gpioStatus, setGpioStatus] = useState<Record<string, unknown> | null>(null);
  const [destinationBusy, setDestinationBusy] = useState<string | null>(null);
  const [destinationResult, setDestinationResult] = useState<Record<string, unknown> | null>(null);
  const [saveResult, setSaveResult] = useState<Record<string, unknown> | null>(null);
  const [settingsDraft, setSettingsDraft] = useState<DestinationSettingsDraft>({
    controller_ic: 'unknown',
    width: null,
    height: null,
    pclk_hz_initial: 10000000,
    mode: 0,
    cmd_bits: 8,
    param_bits: 8,
    max_transfer_lines_initial: 40,
    swap_xy: false,
    mirror_x: false,
    mirror_y: false,
    invert_color: false,
    color_order: 'unknown'
  });
  const sourceGpioOwners = useMemo(() => sourceGpioOwnersFromPins(sourcePins), [sourcePins]);
  const destinationGpioOptionsForSelect = useMemo(() => destinationGpioOptions.map((option) => {
    const owner = sourceGpioOwners.get(option.value);
    return {
      ...option,
      disabled: owner !== undefined,
      label: owner ? `${option.label} - input ${owner}` : option.label
    };
  }), [sourceGpioOwners]);
  const destinationDuplicateGpios = useMemo(() => {
    const counts = new Map<number, number>();
    for (const row of pinRows) {
      if (row.gpio !== null) counts.set(row.gpio, (counts.get(row.gpio) || 0) + 1);
    }
    return new Set(Array.from(counts.entries()).filter(([, count]) => count > 1).map(([gpio]) => gpio));
  }, [pinRows]);

  useEffect(() => {
    const pins = destinationProfile?.connector?.pins || [];
    setPinRows(pins.map((pin, index) => ({
      key: `${pin.name || index}`,
      signal: pin.name || '?',
      role: pin.role || '?',
      gpio: pin.esp32p4_gpio ?? null,
      notes: pin.notes || ''
    })));
    const nextDestination = destinationProfile?.destination || {};
    const nextSpi = nextDestination.spi || {};
    const nextOrientation = nextDestination.orientation || {};
    const nextColor = nextDestination.color || {};
    setSettingsDraft({
      controller_ic: nextDestination.controller_ic || 'unknown',
      width: nextDestination.native_resolution?.width ?? null,
      height: nextDestination.native_resolution?.height ?? null,
      pclk_hz_initial: nextSpi.pclk_hz_initial || 10000000,
      mode: nextSpi.mode ?? 0,
      cmd_bits: nextSpi.cmd_bits || 8,
      param_bits: nextSpi.param_bits || 8,
      max_transfer_lines_initial: nextSpi.max_transfer_lines_initial || 40,
      swap_xy: Boolean(nextOrientation.swap_xy),
      mirror_x: Boolean(nextOrientation.mirror_x),
      mirror_y: Boolean(nextOrientation.mirror_y),
      invert_color: Boolean(nextColor.invert_color),
      color_order: nextColor.color_order || 'unknown'
    });
  }, [destinationProfile]);

  const updatePinGpio = (key: string, gpio: number | null) => {
    if (gpio !== null && sourceGpioOwners.has(gpio)) {
      return;
    }
    setPinRows((current) => current.map((row) => row.key === key ? { ...row, gpio } : row));
  };

  const addDraftPin = () => {
    const nextIndex = pinRows.length + 1;
    setPinRows((current) => [
      ...current,
      {
        key: `custom_${Date.now()}`,
        signal: `AUX${nextIndex}`,
        role: 'aux_output_probe',
        gpio: null,
        notes: 'Custom lab probe pin.'
      }
    ]);
  };

  const claimedSignals = useMemo(() => {
    const claims = Array.isArray(gpioStatus?.claims) ? gpioStatus?.claims : [];
    return new Set(claims.map((claim) => String((claim as Record<string, unknown>).signal)));
  }, [gpioStatus]);

  const refreshDestinationStatus = async () => {
    const status = await api.destinationGpioStatus();
    setGpioStatus(status as Record<string, unknown>);
    return status;
  };

  useEffect(() => {
    refreshDestinationStatus().catch((error) => setDestinationResult({ ok: false, error: error.message }));
  }, []);

  const runDestinationAction = async (key: string, action: () => Promise<unknown>) => {
    setDestinationBusy(key);
    try {
      const result = await action();
      setDestinationResult(result as Record<string, unknown>);
      await refreshDestinationStatus();
    } catch (error) {
      setDestinationResult({ ok: false, error: error instanceof Error ? error.message : String(error) });
    } finally {
      setDestinationBusy(null);
    }
  };

  const runDestinationSpiAction = async (key: string, action: () => Promise<unknown>) => {
    setDestinationBusy(key);
    try {
      const result = await action();
      setDestinationResult(result as Record<string, unknown>);
    } catch (error) {
      setDestinationResult({ ok: false, error: error instanceof Error ? error.message : String(error) });
    } finally {
      setDestinationBusy(null);
    }
  };

  const resetPinDraft = () => {
    const pins = destinationProfile?.connector?.pins || [];
    setPinRows(pins.map((pin, index) => ({
      key: `${pin.name || index}`,
      signal: pin.name || '?',
      role: pin.role || '?',
      gpio: pin.esp32p4_gpio ?? null,
      notes: pin.notes || ''
    })));
  };

  const pinDraftJson = {
    profile_id: destinationProfile?.profile_id || 'spi_lcd_destination',
    pin_mapping: pinRows.map((row) => ({
      panel_pin: row.signal,
      role: row.role,
      esp32p4_gpio: row.gpio,
      notes: row.notes
    })),
    destination: {
      controller_ic: settingsDraft.controller_ic,
      native_resolution: settingsDraft.width && settingsDraft.height ? {
        width: settingsDraft.width,
        height: settingsDraft.height
      } : null,
      spi: {
        pclk_hz_initial: settingsDraft.pclk_hz_initial,
        mode: settingsDraft.mode,
        cmd_bits: settingsDraft.cmd_bits,
        param_bits: settingsDraft.param_bits,
        max_transfer_lines_initial: settingsDraft.max_transfer_lines_initial
      },
      orientation: {
        swap_xy: settingsDraft.swap_xy,
        mirror_x: settingsDraft.mirror_x,
        mirror_y: settingsDraft.mirror_y
      },
      color: {
        color_order: settingsDraft.color_order,
        invert_color: settingsDraft.invert_color
      }
    }
  };

  const updateSettingsDraft = (patch: Partial<DestinationSettingsDraft>) => {
    setSettingsDraft((current) => ({ ...current, ...patch }));
  };

  const savePinDraft = async () => {
    setDestinationBusy('save-profile');
    try {
      const result = await api.saveDestinationProfile(pinDraftJson);
      setSaveResult({ ok: true, path: result.path });
      setDestinationResult({ ok: true, command: 'SAVE_DESTINATION_PROFILE', path: result.path });
    } catch (error) {
      setSaveResult({ ok: false, error: error instanceof Error ? error.message : String(error) });
      setDestinationResult({ ok: false, command: 'SAVE_DESTINATION_PROFILE', error: error instanceof Error ? error.message : String(error) });
    } finally {
      setDestinationBusy(null);
    }
  };

  return (
    <div className="destinationGrid">
      <Space direction="vertical" size="middle" className="fullWidth">
        <Card size="small" title="Hardware I/O Profile">
          <Descriptions bordered size="small" column={2}>
            <Descriptions.Item label="Profile">{destinationProfile?.profile_id || 'not loaded'}</Descriptions.Item>
            <Descriptions.Item label="Status">{destinationProfile?.status || '?'}</Descriptions.Item>
            <Descriptions.Item label="Interface">{destination.interface || '?'}</Descriptions.Item>
            <Descriptions.Item label="Driver">{destination.driver_family || '?'}</Descriptions.Item>
            <Descriptions.Item label="Boot policy" span={2}>{destination.boot_policy || '?'}</Descriptions.Item>
          </Descriptions>
          <Collapse
            ghost
            size="small"
            className="compactCollapse"
            items={[
              {
                key: 'safety',
                label: 'Safety and bring-up sequence',
                children: (
                  <Space direction="vertical" size="middle" className="fullWidth">
                    <Alert
                      type="warning"
                      showIcon
                      message="Output pins are disabled by default"
                      description="GPIO edits are draft-only. Claim drives a no-pull output; Release returns it to disabled/no-pull."
                    />
                    <Steps
                      size="small"
                      current={1}
                      items={[
                        { title: 'Identify', description: 'Controller, resolution, logic voltage' },
                        { title: 'Map', description: 'CS, RESET, D/C, SDI, SCK' },
                        { title: 'Pattern', description: 'Standalone output' },
                        { title: 'Frame', description: 'Show frame sample' },
                        { title: 'Runtime', description: 'Project mode' }
                      ]}
                    />
                  </Space>
                )
              }
            ]}
          />
        </Card>

        <Card
          size="small"
          title="Pin Map And Control"
          extra={
            <Space>
              <Tooltip title="GPIO edits are draft-only. Claim drives a no-pull output; Release returns it to disabled/no-pull.">
                <Tag color="blue">explicit claim</Tag>
              </Tooltip>
              {destinationResult && (
                <Tooltip title={<JsonBlock value={destinationResult} />}>
                  <Tag color={destinationResult.ok ? 'green' : 'red'}>
                    {destinationResult.ok ? 'last ok' : 'last error'}
                  </Tag>
                </Tooltip>
              )}
              {saveResult && (
                <Tooltip title={<JsonBlock value={saveResult} />}>
                  <Tag color={saveResult.ok ? 'green' : 'red'}>{saveResult.ok ? 'saved' : 'save failed'}</Tag>
                </Tooltip>
              )}
              <Button size="small" type="primary" loading={destinationBusy === 'save-profile'} onClick={savePinDraft}>Save</Button>
              <Button size="small" icon={<PlusOutlined />} onClick={addDraftPin}>Add</Button>
              <Button size="small" icon={<ReloadOutlined />} onClick={resetPinDraft}>Reset</Button>
            </Space>
          }
        >
          <Table
            size="small"
            pagination={false}
            dataSource={pinRows}
            scroll={{ x: 900 }}
            columns={[
              {
                title: 'Pin',
                dataIndex: 'signal',
                width: 112,
                render: (value: string, row: DestinationPinDraft) => (
                  <Select
                    className="pinSignalSelect"
                    value={value}
                    options={[
                      { value: 'CS', label: 'CS' },
                      { value: 'RESET', label: 'RESET' },
                      { value: 'D/C', label: 'D/C' },
                      { value: 'SDI', label: 'SDI' },
                      { value: 'SCK', label: 'SCK' },
                      { value: 'VCC', label: 'VCC' },
                      { value: 'GND', label: 'GND' },
                      { value: 'LED', label: 'LED' },
                      { value, label: value }
                    ]}
                    onChange={(signal) => setPinRows((current) => current.map((pin) => pin.key === row.key ? { ...pin, signal } : pin))}
                  />
                )
              },
              {
                title: 'Role',
                dataIndex: 'role',
                width: 150,
                render: (value: string, row: DestinationPinDraft) => (
                  <Select
                    className="pinRoleSelect"
                    value={value}
                    options={[
                      { value: 'spi_chip_select', label: 'SPI CS' },
                      { value: 'panel_reset', label: 'Reset' },
                      { value: 'data_command_select', label: 'D/C' },
                      { value: 'spi_mosi', label: 'SPI MOSI' },
                      { value: 'spi_clock', label: 'SPI SCK' },
                      { value: 'aux_output_probe', label: 'Aux probe' },
                      { value: 'power', label: 'Power' },
                      { value: 'ground', label: 'Ground' },
                      { value: 'backlight_power', label: 'Backlight' }
                    ]}
                    onChange={(role) => setPinRows((current) => current.map((pin) => pin.key === row.key ? { ...pin, role, gpio: destinationOutputRoles.has(role) || role === 'aux_output_probe' ? pin.gpio : null } : pin))}
                  />
                )
              },
              {
                title: 'GPIO',
                dataIndex: 'gpio',
                width: 104,
                render: (value: number | null, row: DestinationPinDraft) => (
                  <Select
                    allowClear
                    showSearch
                    value={value ?? undefined}
                    placeholder="TBD"
                    disabled={row.role === 'power' || row.role === 'ground' || row.role === 'backlight_power'}
                    className="gpioInput"
                    options={destinationGpioOptionsForSelect}
                    optionFilterProp="label"
                    onChange={(nextValue) => updatePinGpio(row.key, typeof nextValue === 'number' ? nextValue : null)}
                  />
                )
              },
              {
                title: 'State',
                width: 130,
                render: (_value: unknown, row: DestinationPinDraft) => {
                  if (row.role === 'power' || row.role === 'ground' || row.role === 'backlight_power') return <Tag>external</Tag>;
                  if (row.gpio === null) return <Tag>TBD</Tag>;
                  const sourceOwner = sourceGpioOwners.get(row.gpio);
                  if (sourceOwner) return <Tooltip title={`Owned by active input profile ${sourceOwner}`}><Tag color="red">input</Tag></Tooltip>;
                  if (destinationDuplicateGpios.has(row.gpio)) return <Tag color="orange">duplicate</Tag>;
                  if (claimedSignals.has(row.signal)) return <Tag color="green">claimed</Tag>;
                  return <Tag color="blue">free</Tag>;
                }
              },
              {
                title: 'Actions',
                width: 238,
                render: (_value: unknown, row: DestinationPinDraft) => {
                  if (row.role === 'power' || row.role === 'ground' || row.role === 'backlight_power') {
                    return <Text type="secondary">external only</Text>;
                  }
                  const gpio = row.gpio;
                  const unavailable = gpio === null || (gpio !== null && sourceGpioOwners.has(gpio)) || (gpio !== null && destinationDuplicateGpios.has(gpio));
                  const claimed = claimedSignals.has(row.signal);
                  return (
                    <Space.Compact className="destinationActions">
                      <Tooltip title="Validate GPIO without driving">
                        <Button
                          size="small"
                          icon={<CheckCircleOutlined />}
                          loading={destinationBusy === `${row.key}:validate`}
                          disabled={unavailable}
                          aria-label="Validate"
                          onClick={() => runDestinationAction(`${row.key}:validate`, () => api.destinationGpioValidate(row.signal, gpio as number))}
                        />
                      </Tooltip>
                      <Tooltip title="Claim output">
                        <Button
                          size="small"
                          type="primary"
                          icon={<ApiOutlined />}
                          loading={destinationBusy === `${row.key}:claim`}
                          disabled={unavailable}
                          aria-label="Claim"
                          onClick={() => runDestinationAction(`${row.key}:claim`, () => api.destinationGpioClaim(row.signal, gpio as number))}
                        />
                      </Tooltip>
                      <Tooltip title="Drive low">
                        <Button
                          size="small"
                          icon={<ArrowDownOutlined />}
                          loading={destinationBusy === `${row.key}:low`}
                          disabled={!claimed}
                          aria-label="Drive low"
                          onClick={() => runDestinationAction(`${row.key}:low`, () => api.destinationGpioSet(row.signal, 0))}
                        />
                      </Tooltip>
                      <Tooltip title="Drive high">
                        <Button
                          size="small"
                          icon={<ArrowUpOutlined />}
                          loading={destinationBusy === `${row.key}:high`}
                          disabled={!claimed}
                          aria-label="Drive high"
                          onClick={() => runDestinationAction(`${row.key}:high`, () => api.destinationGpioSet(row.signal, 1))}
                        />
                      </Tooltip>
                      <Tooltip title="Pulse 100 ms">
                        <Button
                          size="small"
                          icon={<ThunderboltOutlined />}
                          loading={destinationBusy === `${row.key}:pulse`}
                          disabled={!claimed}
                          aria-label="Pulse"
                          onClick={() => runDestinationAction(`${row.key}:pulse`, () => api.destinationGpioPulse(row.signal, row.signal === 'RESET' ? 0 : 1, 100))}
                        />
                      </Tooltip>
                      <Tooltip title="Release to disabled/no-pull">
                        <Button
                          size="small"
                          danger
                          icon={<DisconnectOutlined />}
                          loading={destinationBusy === `${row.key}:release`}
                          disabled={!claimed}
                          aria-label="Release"
                          onClick={() => runDestinationAction(`${row.key}:release`, () => api.destinationGpioRelease(row.signal))}
                        />
                      </Tooltip>
                    </Space.Compact>
                  );
                }
              },
              { title: 'Notes', dataIndex: 'notes', ellipsis: true }
            ]}
          />
          <details className="draftJsonDetails">
            <summary>Draft mapping JSON</summary>
            <JsonBlock value={pinDraftJson} />
          </details>
        </Card>

        <Collapse
          size="small"
          items={[
            {
              key: 'unknowns',
              label: 'Open Unknowns',
              children: (
                <Space wrap>
                  {unknowns.length > 0 ? unknowns.map((unknown) => <Tag key={unknown}>{unknown}</Tag>) : <Empty image={Empty.PRESENTED_IMAGE_SIMPLE} description="No unknowns listed" />}
                </Space>
              )
            }
          ]}
        />
      </Space>

      <Space direction="vertical" size="middle" className="fullWidth">
        <Card size="small" title="Hardware I/O Workbench">
          <Tabs
            size="small"
            items={[
              {
                key: 'panel',
                label: 'Panel',
                children: (
                  <Form layout="vertical" className="compactForm">
                    <Form.Item label="Controller">
                      <Select
                        value={settingsDraft.controller_ic}
                        options={[
                          { value: 'unknown', label: 'Unknown' },
                          { value: 'st7789', label: 'ST7789' },
                          { value: 'st7796s', label: 'ST7796S' },
                          { value: 'st7735', label: 'ST7735' },
                          { value: 'ili9486', label: 'ILI9486' },
                          { value: 'ili9341', label: 'ILI9341' },
                          { value: 'gc9a01', label: 'GC9A01' }
                        ]}
                        onChange={(controller_ic) => updateSettingsDraft({ controller_ic })}
                      />
                    </Form.Item>
                    <Space className="fullWidth" size="middle">
                      <Form.Item label="Width" className="fullWidth">
                        <InputNumber min={1} max={4096} value={settingsDraft.width ?? undefined} placeholder="TBD" className="fullWidth" onChange={(width) => updateSettingsDraft({ width: typeof width === 'number' ? width : null })} />
                      </Form.Item>
                      <Form.Item label="Height" className="fullWidth">
                        <InputNumber min={1} max={4096} value={settingsDraft.height ?? undefined} placeholder="TBD" className="fullWidth" onChange={(height) => updateSettingsDraft({ height: typeof height === 'number' ? height : null })} />
                      </Form.Item>
                    </Space>
                    <Form.Item label="SPI clock">
                      <InputNumber min={100000} max={80000000} value={settingsDraft.pclk_hz_initial} addonAfter="Hz" className="fullWidth" onChange={(pclk_hz_initial) => updateSettingsDraft({ pclk_hz_initial: typeof pclk_hz_initial === 'number' ? pclk_hz_initial : 10000000 })} />
                    </Form.Item>
                    <Space wrap>
                      <Form.Item label="SPI mode"><InputNumber min={0} max={3} value={settingsDraft.mode} onChange={(mode) => updateSettingsDraft({ mode: typeof mode === 'number' ? mode : 0 })} /></Form.Item>
                      <Form.Item label="Command bits"><InputNumber min={8} max={16} value={settingsDraft.cmd_bits} onChange={(cmd_bits) => updateSettingsDraft({ cmd_bits: typeof cmd_bits === 'number' ? cmd_bits : 8 })} /></Form.Item>
                      <Form.Item label="Param bits"><InputNumber min={8} max={16} value={settingsDraft.param_bits} onChange={(param_bits) => updateSettingsDraft({ param_bits: typeof param_bits === 'number' ? param_bits : 8 })} /></Form.Item>
                    </Space>
                    <Form.Item label="Transfer lines">
                      <InputNumber min={1} max={512} value={settingsDraft.max_transfer_lines_initial} className="fullWidth" onChange={(max_transfer_lines_initial) => updateSettingsDraft({ max_transfer_lines_initial: typeof max_transfer_lines_initial === 'number' ? max_transfer_lines_initial : 40 })} />
                    </Form.Item>
                    <Space wrap>
                      <Form.Item label="Swap XY"><Switch checked={settingsDraft.swap_xy} onChange={(swap_xy) => updateSettingsDraft({ swap_xy })} /></Form.Item>
                      <Form.Item label="Mirror X"><Switch checked={settingsDraft.mirror_x} onChange={(mirror_x) => updateSettingsDraft({ mirror_x })} /></Form.Item>
                      <Form.Item label="Mirror Y"><Switch checked={settingsDraft.mirror_y} onChange={(mirror_y) => updateSettingsDraft({ mirror_y })} /></Form.Item>
                      <Form.Item label="Invert"><Switch checked={settingsDraft.invert_color} onChange={(invert_color) => updateSettingsDraft({ invert_color })} /></Form.Item>
                    </Space>
                    <Form.Item label="Color order">
                      <Select
                        value={settingsDraft.color_order}
                        options={[
                          { value: 'unknown', label: 'Unknown' },
                          { value: 'rgb', label: 'RGB' },
                          { value: 'bgr', label: 'BGR' }
                        ]}
                        onChange={(color_order) => updateSettingsDraft({ color_order })}
                      />
                    </Form.Item>
                    <Button block type="primary" loading={destinationBusy === 'save-profile'} onClick={savePinDraft}>Save I/O Profile</Button>
                  </Form>
                )
              },
              {
                key: 'actions',
                label: 'Actions',
                children: (
                  <Space direction="vertical" className="fullWidth">
                    <Button block loading={destinationBusy === 'spi-status'} onClick={() => runDestinationSpiAction('spi-status', api.destinationSpiStatus)}>Read output status</Button>
                    <Button block type="primary" loading={destinationBusy === 'spi-init'} onClick={() => runDestinationSpiAction('spi-init', api.destinationSpiInit)}>Initialize output</Button>
                    <Button block loading={destinationBusy === 'spi-pattern-orientation'} onClick={() => runDestinationSpiAction('spi-pattern-orientation', () => api.destinationSpiTestPattern('orientation'))}>Orientation pattern</Button>
                    <Button block loading={destinationBusy === 'spi-pattern-bars'} onClick={() => runDestinationSpiAction('spi-pattern-bars', () => api.destinationSpiTestPattern('color_bars'))}>Color bars</Button>
                    <Button block loading={destinationBusy === 'spi-pattern565'} onClick={() => runDestinationSpiAction('spi-pattern565', api.destinationSpiTestPattern565)}>16-bit color bars</Button>
                    <Button block loading={destinationBusy === 'spi-burst'} onClick={() => runDestinationSpiAction('spi-burst', () => api.destinationSpiSignalBurst(5000))}>Signal burst 5s</Button>
                    <Button block loading={destinationBusy === 'spi-clear'} onClick={() => runDestinationSpiAction('spi-clear', () => api.destinationSpiClear('0000'))}>Clear black</Button>
                    <Button block loading={destinationBusy === 'spi-show-source'} onClick={() => runDestinationSpiAction('spi-show-source', api.destinationSpiShowSourceFrame)}>Show frame sample once</Button>
                    <Button block danger loading={destinationBusy === 'spi-safe-off'} onClick={() => runDestinationSpiAction('spi-safe-off', api.destinationSpiSafeOff)}>Safe off</Button>
                    <Text type="secondary">Output actions are command-gated. Nothing runs at boot.</Text>
                  </Space>
                )
              },
              {
                key: 'response',
                label: 'Response',
                children: destinationResult ? <JsonBlock value={destinationResult} /> : <Empty image={Empty.PRESENTED_IMAGE_SIMPLE} description="Run an action to see the latest response" />
              }
            ]}
          />
        </Card>
      </Space>
    </div>
  );
}

type LiveFrameMeta = {
  dataMode: string;
  bytes: number;
  frame: number | string;
  captureMs: number | string;
  receivedAt: string;
};

type VisualMode = 'clean' | 'gbc';

type VisualOptions = {
  mode: VisualMode;
  tint: number;
  contrast: number;
  persistence: number;
  pixelGap: number;
  lens: boolean;
  lensOpacity: number;
};

type PixiLiveRenderer = {
  app: Application;
  sourceCanvas: HTMLCanvasElement;
  texture: Texture;
  sprite: Sprite;
  glassFilter: Filter;
};

const filterVertexShader = `
in vec2 aPosition;
out vec2 vTextureCoord;

uniform vec4 uInputSize;
uniform vec4 uOutputFrame;
uniform vec4 uOutputTexture;

vec4 filterVertexPosition(void)
{
    vec2 position = aPosition * uOutputFrame.zw + uOutputFrame.xy;
    position.x = position.x * (2.0 / uOutputTexture.x) - 1.0;
    position.y = position.y * (2.0 * uOutputTexture.z / uOutputTexture.y) - uOutputTexture.z;
    return vec4(position, 0.0, 1.0);
}

vec2 filterTextureCoord(void)
{
    return aPosition * (uOutputFrame.zw * uInputSize.zw);
}

void main(void)
{
    gl_Position = filterVertexPosition();
    vTextureCoord = filterTextureCoord();
}`;

const lcdGlassFragmentShader = `
in vec2 vTextureCoord;
out vec4 finalColor;

uniform sampler2D uTexture;

void main(void)
{
    vec4 texColor = texture(uTexture, vTextureCoord);
    float pb = 0.4;
    vec4 lcdColor = vec4(pb, pb, pb, 1.0);

    int px = int(mod(gl_FragCoord.x, 3.0));
    if (px == 1) {
        lcdColor.r = 1.0;
    } else if (px == 2) {
        lcdColor.g = 1.0;
    } else {
        lcdColor.b = 1.0;
    }

    float scanline = 0.25;
    if (int(mod(gl_FragCoord.y, 3.0)) == 0) {
        lcdColor.rgb = vec3(scanline, scanline, scanline);
    }

    finalColor = texColor * lcdColor;
}`;

function drawMessage(canvas: HTMLCanvasElement, message: string) {
  const ctx = canvas.getContext('2d');
  if (!ctx) return;
  ctx.fillStyle = '#050607';
  ctx.fillRect(0, 0, canvas.width, canvas.height);
  ctx.fillStyle = '#9aabb7';
  ctx.font = '10px ui-monospace, SFMono-Regular, Menlo, Consolas, monospace';
  ctx.textBaseline = 'top';
  ctx.fillText(message, 6, 6);
}

function scale(value: number, max: number) {
  return Math.round((value * 255) / max);
}

function clampByte(value: number) {
  return Math.max(0, Math.min(255, Math.round(value)));
}

function applyVisualColor(r: number, g: number, b: number, options: VisualOptions) {
  const contrast = options.contrast / 100;
  let nr = (r - 128) * contrast + 128;
  let ng = (g - 128) * contrast + 128;
  let nb = (b - 128) * contrast + 128;

  if (options.mode === 'gbc') {
    const tint = options.tint / 100;
    nr = nr * (1 - tint) + 188 * tint;
    ng = ng * (1 - tint) + 205 * tint;
    nb = nb * (1 - tint) + 117 * tint;
  }

  return [clampByte(nr), clampByte(ng), clampByte(nb)];
}

function pixelRgb565(raw: Uint8Array, offset: number) {
  const word = (raw[offset] || 0) | ((raw[offset + 1] || 0) << 8);
  return [scale((word >> 11) & 31, 31), scale((word >> 5) & 63, 63), scale(word & 31, 31)];
}

function pixelRgb664(raw: Uint8Array, offset: number) {
  const word = (raw[offset] || 0) | ((raw[offset + 1] || 0) << 8);
  return [scale(word & 63, 63), scale((word >> 6) & 63, 63), scale((word >> 12) & 15, 15)];
}

function pixelRgb666(raw: Uint8Array, offset: number) {
  return [scale(raw[offset] & 63, 63), scale(raw[offset + 1] & 63, 63), scale(raw[offset + 2] & 63, 63)];
}

function drawFrame(canvas: HTMLCanvasElement, raw: Uint8Array, dataMode: string, options: VisualOptions) {
  const ctx = canvas.getContext('2d');
  if (!ctx) return;
  if (!['RGB565', 'RGB664', 'RGB666'].includes(dataMode)) {
    drawMessage(canvas, `unsupported frame mode ${dataMode || 'unknown'}`);
    return;
  }
  const visibleWidth = 160;
  const visibleHeight = 144;
  const streamWidth = 161;
  const visibleLinearShiftPixels = -4;
  const bytesPerPixel = dataMode === 'RGB666' ? 3 : 2;

  const displayScale = 1;
  const sourcePixelSize = 1;
  const renderWidth = visibleWidth * displayScale;
  const renderHeight = visibleHeight * displayScale;
  const previous = options.persistence > 0 && canvas.width === renderWidth && canvas.height === renderHeight
    ? ctx.getImageData(0, 0, renderWidth, renderHeight)
    : null;

  if (canvas.width !== renderWidth || canvas.height !== renderHeight) {
    canvas.width = renderWidth;
    canvas.height = renderHeight;
  }

  const image = ctx.createImageData(renderWidth, renderHeight);
  const gapColor = options.mode === 'gbc'
    ? [116, 127, 88]
    : [0, 0, 0];
  for (let offset = 0; offset < image.data.length; offset += 4) {
    image.data[offset] = gapColor[0];
    image.data[offset + 1] = gapColor[1];
    image.data[offset + 2] = gapColor[2];
    image.data[offset + 3] = 255;
  }
  const persistence = options.persistence / 100;

  for (let y = 0; y < visibleHeight; y += 1) {
    for (let x = 0; x < visibleWidth; x += 1) {
      let srcPixel = (y * streamWidth + x) + visibleLinearShiftPixels;
      if (srcPixel < 0) srcPixel = 0;
      const src = srcPixel * bytesPerPixel;
      const rgb = dataMode === 'RGB565'
        ? pixelRgb565(raw, src)
        : dataMode === 'RGB664'
          ? pixelRgb664(raw, src)
          : pixelRgb666(raw, src);
      const [r, g, b] = applyVisualColor(rgb[0], rgb[1], rgb[2], options);
      let sr = r;
      let sg = g;
      let sb = b;

      if (options.mode === 'gbc') {
        const edgeX = Math.min(x, visibleWidth - 1 - x) / visibleWidth;
        const edgeY = Math.min(y, visibleHeight - 1 - y) / visibleHeight;
        const vignette = 0.88 + Math.min(edgeX, edgeY) * 1.6;
        sr *= Math.min(1, vignette);
        sg *= Math.min(1, vignette);
        sb *= Math.min(1, vignette);
      }

      for (let py = 0; py < sourcePixelSize; py += 1) {
        for (let px = 0; px < sourcePixelSize; px += 1) {
          const target = (((y * displayScale) + py) * renderWidth + (x * displayScale) + px) * 4;
          if (previous && persistence > 0) {
            image.data[target] = clampByte(sr * (1 - persistence) + previous.data[target] * persistence);
            image.data[target + 1] = clampByte(sg * (1 - persistence) + previous.data[target + 1] * persistence);
            image.data[target + 2] = clampByte(sb * (1 - persistence) + previous.data[target + 2] * persistence);
          } else {
            image.data[target] = clampByte(sr);
            image.data[target + 1] = clampByte(sg);
            image.data[target + 2] = clampByte(sb);
          }
          image.data[target + 3] = 255;
        }
      }
    }
  }
  ctx.putImageData(image, 0, 0);
}

async function createPixiLiveRenderer(host: HTMLDivElement): Promise<PixiLiveRenderer> {
  const app = new Application();
  await app.init({
    width: 640,
    height: 576,
    backgroundColor: 0x050607,
    antialias: false,
    autoDensity: false,
    resolution: 1,
    preference: 'webgl'
  });

  app.canvas.className = 'nativeLiveCanvas';
  host.replaceChildren(app.canvas);

  const sourceCanvas = document.createElement('canvas');
  sourceCanvas.width = 160;
  sourceCanvas.height = 144;

  const texture = Texture.from(sourceCanvas, true);
  texture.source.scaleMode = 'nearest';
  const sprite = new Sprite({ texture, roundPixels: true });
  const glassFilter = Filter.from({
    gl: {
      vertex: filterVertexShader,
      fragment: lcdGlassFragmentShader
    },
    antialias: false,
    padding: 0,
    resolution: 1
  });
  app.stage.addChild(sprite);

  return { app, sourceCanvas, texture, sprite, glassFilter };
}

function presentPixiLiveFrame(renderer: PixiLiveRenderer, options: VisualOptions) {
  renderer.texture.source.resize(renderer.sourceCanvas.width, renderer.sourceCanvas.height);
  renderer.texture.source.update();
  renderer.texture.update();
  renderer.sprite.scale.set(renderer.sourceCanvas.width > 320 ? 1 : 4);
  renderer.sprite.filters = options.mode === 'gbc' && options.pixelGap > 0
    ? [renderer.glassFilter]
    : [];
  renderer.app.renderer.resize(
    Math.round(renderer.sourceCanvas.width * renderer.sprite.scale.x),
    Math.round(renderer.sourceCanvas.height * renderer.sprite.scale.y)
  );
}

function LivePage({ status, onStart, onStop, onRecover, onSafeIdle }: {
  status: WorkbenchStatus | null;
  onStart: () => void;
  onStop: () => void;
  onRecover: () => void;
  onSafeIdle: () => void;
}) {
  const pixiHostRef = useRef<HTMLDivElement | null>(null);
  const pixiRendererRef = useRef<PixiLiveRenderer | null>(null);
  const [pixiReady, setPixiReady] = useState(false);
  const [frameMeta, setFrameMeta] = useState<LiveFrameMeta | null>(null);
  const [frameError, setFrameError] = useState('');
  const [visualOptions, setVisualOptions] = useState<VisualOptions>({
    mode: 'clean',
    tint: 26,
    contrast: 92,
    persistence: 0,
    pixelGap: 0,
    lens: false,
    lensOpacity: 88
  });

  useEffect(() => {
    if (!pixiHostRef.current) return;
    let cancelled = false;
    let renderer: PixiLiveRenderer | null = null;

    createPixiLiveRenderer(pixiHostRef.current).then((created) => {
      if (cancelled) {
        created.app.destroy(true);
        return;
      }
      renderer = created;
      pixiRendererRef.current = created;
      drawMessage(created.sourceCanvas, 'live capture stopped');
      presentPixiLiveFrame(created, visualOptions);
      setPixiReady(true);
    }).catch((error) => {
      setFrameError((error as Error).message);
    });

    return () => {
      cancelled = true;
      if (renderer) {
        renderer.app.destroy(true);
      }
      if (pixiRendererRef.current === renderer) {
        pixiRendererRef.current = null;
      }
    };
  }, []);

  useEffect(() => {
    const renderer = pixiRendererRef.current;
    if (!renderer) return;
    if (!status?.running) {
      drawMessage(renderer.sourceCanvas, 'live capture stopped');
      presentPixiLiveFrame(renderer, visualOptions);
      return;
    }
    let cancelled = false;
    let busy = false;
    const fetchFrame = async () => {
      if (busy || cancelled) return;
      busy = true;
      try {
        const frame = await api.frame();
        if (cancelled || !pixiRendererRef.current) return;
        const activeRenderer = pixiRendererRef.current;
        const dataMode = String(frame.metadata.pixel_format || frame.metadata.data_mode || 'RGB565');
        drawFrame(activeRenderer.sourceCanvas, frame.bytes, dataMode, visualOptions);
        presentPixiLiveFrame(activeRenderer, visualOptions);
        setFrameMeta({
          dataMode,
          bytes: frame.bytes.length,
          frame: String(frame.metadata.server_frame_count ?? '?'),
          captureMs: String(frame.metadata.server_last_capture_ms ?? '?'),
          receivedAt: new Date().toLocaleTimeString()
        });
        setFrameError('');
      } catch (error) {
        if (!cancelled && pixiRendererRef.current) {
          const message = (error as Error).message.trim();
          drawMessage(pixiRendererRef.current.sourceCanvas, message.includes('waiting') ? 'waiting for input' : 'no current live frame');
          presentPixiLiveFrame(pixiRendererRef.current, visualOptions);
          setFrameError(message);
        }
      } finally {
        busy = false;
      }
    };
    fetchFrame();
    const timer = window.setInterval(fetchFrame, 33);
    return () => {
      cancelled = true;
      window.clearInterval(timer);
    };
  }, [pixiReady, status?.running, visualOptions]);

  return (
    <div className="liveGrid">
      <Card className="liveCard" title="Live Monitor" extra={<Badge status={statusColor(status)} text={status?.source_state || 'unknown'} />}>
        <div className="nativeLiveSurface">
          <div className={`nativeLiveFrame ${visualOptions.lens ? 'withLens' : 'withoutLens'}`}>
            <div ref={pixiHostRef} className="pixiLiveHost" />
            {visualOptions.lens ? (
              <img
                className="gbcLensMask"
                src="/assets/game_boy_color_lense_mask.png"
                alt=""
                style={{ opacity: visualOptions.lensOpacity / 100 }}
              />
            ) : null}
          </div>
        </div>
      </Card>
      <Space direction="vertical" size="small" className="fullWidth">
        <Card size="small" title="Live Workbench">
          <Tabs
            size="small"
            items={[
              {
                key: 'controls',
                label: 'Control',
                children: (
                  <Space direction="vertical" className="fullWidth">
                    <Space.Compact block>
                      <Button type="primary" onClick={onStart}>Start</Button>
                      <Button danger onClick={onStop}>Stop</Button>
                      <Button onClick={onRecover}>Recover</Button>
                      <Button onClick={onSafeIdle}>Idle</Button>
                    </Space.Compact>
                    <MetricStrip items={[
                      { label: 'Running', value: String(status?.running ?? false) },
                      { label: 'FPS', value: status?.server_capture_fps ?? '?' },
                      { label: 'Age', value: `${status?.server_frame_age_ms ?? '?'} ms` },
                      { label: 'Frame', value: frameMeta?.frame || '?' }
                    ]} />
                    <Descriptions size="small" bordered column={1}>
                      <Descriptions.Item label="Bytes">{frameMeta?.bytes ?? '?'}</Descriptions.Item>
                      <Descriptions.Item label="Frame">{frameMeta?.frame ?? '?'}</Descriptions.Item>
                      <Descriptions.Item label="Received">{frameMeta?.receivedAt || '?'}</Descriptions.Item>
                      <Descriptions.Item label="Error">{status?.error || ''}</Descriptions.Item>
                    </Descriptions>
                    {frameError ? <Alert type="warning" showIcon message="Live frame issue" description={frameError} /> : null}
                  </Space>
                )
              },
              {
                key: 'visuals',
                label: 'Visuals',
                children: (
                  <Space direction="vertical" className="fullWidth">
                    <Segmented
                      block
                      value={visualOptions.mode}
                      options={[
                        { label: 'Raw', value: 'clean' },
                        { label: 'Profile Glass', value: 'gbc' }
                      ]}
                      onChange={(mode) => setVisualOptions((current) => ({ ...current, mode: mode as VisualMode }))}
                    />
                    <div>
                      <Text type="secondary">LCD tint</Text>
                      <Slider min={0} max={70} value={visualOptions.tint} onChange={(tint) => setVisualOptions((current) => ({ ...current, tint }))} />
                    </div>
                    <div>
                      <Text type="secondary">Contrast</Text>
                      <Slider min={70} max={130} value={visualOptions.contrast} onChange={(contrast) => setVisualOptions((current) => ({ ...current, contrast }))} />
                    </div>
                    <div>
                      <Text type="secondary">Persistence</Text>
                      <Slider min={0} max={40} value={visualOptions.persistence} onChange={(persistence) => setVisualOptions((current) => ({ ...current, persistence }))} />
                    </div>
                    <div>
                      <Text type="secondary">LCD subpixel shader</Text>
                      <Slider
                        min={0}
                        max={1}
                        marks={{ 0: 'Off', 1: 'On' }}
                        value={visualOptions.pixelGap > 0 ? 1 : 0}
                        onChange={(pixelGap) => setVisualOptions((current) => ({ ...current, pixelGap }))}
                      />
                    </div>
                    <Segmented
                      block
                      value={visualOptions.lens ? 'on' : 'off'}
                      options={[
                        { label: 'No Mask', value: 'off' },
                        { label: 'Profile Mask', value: 'on' }
                      ]}
                      onChange={(lens) => setVisualOptions((current) => ({ ...current, lens: lens === 'on' }))}
                    />
                    <div>
                      <Text type="secondary">Lens opacity</Text>
                      <Slider min={20} max={100} value={visualOptions.lensOpacity} onChange={(lensOpacity) => setVisualOptions((current) => ({ ...current, lensOpacity }))} />
                    </div>
                  </Space>
                )
              }
            ]}
          />
        </Card>
      </Space>
    </div>
  );
}

function ArtifactsPage({ items }: { items: ArtifactItem[] }) {
  return (
    <Card title="Recent Experiment Artifacts">
      <List
        dataSource={items}
        locale={{ emptyText: <Empty description="No artifacts found" /> }}
        renderItem={(item) => (
          <List.Item>
            <List.Item.Meta
              avatar={<DatabaseOutlined />}
              title={<Text>{item.name}</Text>}
              description={
                <Space direction="vertical" size={2}>
                  <Text type="secondary">{item.modified_utc}</Text>
                  <Space wrap>
                    {item.manifest ? <Tag color="green">manifest</Tag> : <Tag>legacy</Tag>}
                    <Tag>{item.file_count} files</Tag>
                    {item.files.slice(0, 5).map((file) => <Tag key={file}>{file}</Tag>)}
                  </Space>
                </Space>
              }
            />
          </List.Item>
        )}
      />
    </Card>
  );
}

function ProfilePage({ profile }: { profile: TargetProfile | null }) {
  return (
    <Space direction="vertical" size="middle" className="pageStack">
      <Card title="Identity">
        <Descriptions bordered size="small" column={2}>
          <Descriptions.Item label="Profile ID">{profile?.profile_id}</Descriptions.Item>
          <Descriptions.Item label="Schema">{profile?.schema_version}</Descriptions.Item>
          <Descriptions.Item label="Status">{profile?.status}</Descriptions.Item>
          <Descriptions.Item label="Target type">{profile?.target_type}</Descriptions.Item>
        </Descriptions>
      </Card>
      <Card title="Active Runtime Profile">
        <JsonBlock value={profile?.current_capture_profile} />
      </Card>
      <Card title="Raw Profile">
        <JsonBlock value={profile} />
      </Card>
    </Space>
  );
}

function LogsPage({ logs }: { logs: string[] }) {
  return (
    <Card title="Session Log">
      <pre className="logBlock">{logs.join('\n')}</pre>
    </Card>
  );
}

export default function App() {
  const [selected, setSelected] = usePersistentState<string>('signal-lab:selected-page', 'dashboard');
  const [status, setStatus] = useState<WorkbenchStatus | null>(null);
  const [profile, setProfile] = useState<TargetProfile | null>(null);
  const [destinationProfile, setDestinationProfile] = useState<DestinationProfile | null>(null);
  const [blocks, setBlocks] = useState<LabBlock[]>([]);
  const [projects, setProjects] = useState<LabProject[]>([]);
  const [selectedProjectId, setSelectedProjectId] = usePersistentState<string>('signal-lab:selected-project-id', '');
  const [artifacts, setArtifacts] = useState<ArtifactItem[]>([]);
  const [pins, setPins] = useState<PinRow[]>([]);
  const [logs, setLogs] = useState<string[]>([]);
  const [selectedBuildProfile, setSelectedBuildProfile] = usePersistentState<string>('signal-lab:selected-build-profile', 'production');
  const [globalProjectBusy, setGlobalProjectBusy] = useState<string | null>(null);
  const [operationState, setOperationState] = useState<OperationState | null>(null);
  const [operationHistory, setOperationHistory] = useState<OperationEntry[]>([]);
  const [historyOpen, setHistoryOpen] = useState(false);
  const browserSerialSupported = typeof navigator !== 'undefined' && 'serial' in navigator;

  const log = (message: string) => setLogs((current) => [`${new Date().toISOString()} ${message}`, ...current].slice(0, 200));
  const pushActivity = ({ kind, level, title, detail, projectId, buildProfile }: Omit<OperationEntry, 'id' | 'timestamp'>) => {
    const timestamp = new Date().toLocaleTimeString();
    setOperationHistory((current) => [
      {
        id: `${Date.now()}-${Math.random().toString(16).slice(2, 8)}`,
        kind,
        level,
        title,
        detail,
        timestamp,
        projectId,
        buildProfile
      },
      ...current
    ].slice(0, 16));
    log(`${kind.toUpperCase()} ${title} ${detail}`);
  };

  const refresh = async () => {
    try {
      const nextStatus = await api.status();
      setStatus(nextStatus);
    } catch (error) {
      log(`status error ${(error as Error).message}`);
    }
  };

  useEffect(() => {
    api.profile().then(setProfile).catch((error) => log(`profile error ${error.message}`));
    api.destinationProfile().then(setDestinationProfile).catch((error) => log(`destination profile error ${error.message}`));
    api.blocks().then((data) => setBlocks(data.blocks)).catch((error) => log(`blocks error ${error.message}`));
    api.projects().then((data) => {
      setProjects(data.projects);
      setSelectedProjectId((current) => current || data.projects[0]?.id || '');
    }).catch((error) => log(`projects error ${error.message}`));
    api.artifacts().then((data) => setArtifacts(data.items)).catch((error) => log(`artifacts error ${error.message}`));
    api.gpios().then((data) => setPins(data.gpios)).catch((error) => log(`gpios error ${error.message}`));
    refresh();
    const timer = window.setInterval(refresh, 2000);
    return () => window.clearInterval(timer);
  }, []);

  const actions = useMemo(() => ({
    start: () => api.start().then(setStatus).then(() => log('START')).catch((error) => log(`START error ${error.message}`)),
    stop: () => api.stop().then(setStatus).then(() => log('STOP')).catch((error) => log(`STOP error ${error.message}`)),
    recover: () => api.recover().then(() => refresh()).then(() => log('RECOVER')).catch((error) => log(`RECOVER error ${error.message}`)),
    safeIdle: () => api.safeIdle().then(() => refresh()).then(() => log('SAFE_IDLE')).catch((error) => log(`SAFE_IDLE error ${error.message}`))
  }), []);

  const selectedProject = projects.find((project) => project.id === selectedProjectId) || projects[0] || null;

  useEffect(() => {
    if (!selectedProject?.build_profiles) {
      setSelectedBuildProfile('production');
      return;
    }
    if (!selectedProject.build_profiles[selectedBuildProfile]) {
      if (selectedProject.build_profiles.production) {
        setSelectedBuildProfile('production');
        return;
      }
      const firstProfile = Object.keys(selectedProject.build_profiles)[0];
      if (firstProfile) setSelectedBuildProfile(firstProfile);
    }
  }, [selectedProject, selectedBuildProfile]);

  const replaceProjects = (nextProjects: LabProject[], preferredId?: string) => {
    setProjects(nextProjects);
    setSelectedProjectId((current) => preferredId || (nextProjects.some((project) => project.id === current) ? current : nextProjects[0]?.id || ''));
  };

  const createLocalProject = async () => {
    const draft = createDraftProject();
    try {
      const result = await api.createProject({ ...draft, status: 'draft' });
      replaceProjects(result.projects, result.project?.id || draft.id);
      setSelected('project');
      log(`CREATE_PROJECT ${result.project?.id || draft.id}`);
    } catch (error) {
      setProjects((current) => [...current, draft]);
      setSelectedProjectId(draft.id);
      setSelected('project');
      log(`CREATE_PROJECT local fallback ${draft.id}: ${(error as Error).message}`);
    }
  };

  const saveProject = async (project: LabProject) => {
    try {
      const result = await api.saveProject({ ...project, status: project.status === 'draft_local_only' ? 'draft' : project.status });
      replaceProjects(result.projects, result.project?.id || project.id);
      log(`SAVE_PROJECT ${project.id}`);
    } catch (error) {
      log(`SAVE_PROJECT error ${(error as Error).message}`);
      throw error;
    }
  };

  const updateProjectDraft = (project: LabProject) => {
    setProjects((current) => current.map((item) => item.id === project.id ? project : item));
  };

  const duplicateProject = async (project: LabProject) => {
    const stamp = new Date().toISOString().replace(/[-:]/g, '').slice(0, 15).toLowerCase();
    const nextId = `${project.id}_copy_${stamp}`;
    try {
      const result = await api.duplicateProject(project.id, nextId, `${project.name} Copy`);
      replaceProjects(result.projects, result.project?.id || nextId);
      setSelected('project');
      log(`DUPLICATE_PROJECT ${project.id} -> ${nextId}`);
    } catch (error) {
      log(`DUPLICATE_PROJECT error ${(error as Error).message}`);
    }
  };

  const deleteProject = async (project: LabProject) => {
    try {
      const result = await api.deleteProject(project.id, project.id === 'gbc_spi_lcd_mirror' ? project.id : undefined);
      replaceProjects(result.projects);
      log(`DELETE_PROJECT ${project.id}`);
    } catch (error) {
      log(`DELETE_PROJECT error ${(error as Error).message}`);
    }
  };

  const globalSaveProject = async () => {
    if (!selectedProject) return;
    setGlobalProjectBusy('save');
    setOperationState({
      active: true,
      kind: 'save',
      level: 'info',
      title: `Saving ${selectedProject.name}`,
      detail: 'Persisting the current project definition and graph overlays.',
      phase: 'save'
    });
    try {
      await saveProject(selectedProject);
      pushActivity({
        kind: 'save',
        level: 'success',
        title: `Saved ${selectedProject.name}`,
        detail: 'Project metadata and graph draft were persisted.',
        projectId: selectedProject.id,
        buildProfile: selectedBuildProfile
      });
      setOperationState({
        active: false,
        kind: 'save',
        level: 'success',
        title: 'Project saved',
        detail: 'The current project state is on disk and available to build and flash flows.',
        progress: 100,
        phase: 'complete',
        nextStep: 'Validate or build the selected profile.'
      });
    } catch (error) {
      const message = error instanceof Error ? error.message : String(error);
      pushActivity({
        kind: 'save',
        level: 'error',
        title: `Save failed for ${selectedProject.name}`,
        detail: message,
        projectId: selectedProject.id,
        buildProfile: selectedBuildProfile
      });
      setOperationState({
        active: false,
        kind: 'save',
        level: 'error',
        title: 'Save failed',
        detail: message,
        progress: 100,
        phase: 'error',
        nextStep: 'Retry saving after resolving the underlying issue.'
      });
    } finally {
      setGlobalProjectBusy(null);
    }
  };

  const globalValidateProject = async () => {
    if (!selectedProject || selectedProject.status === 'draft_local_only') return;
    setGlobalProjectBusy('validate');
    setOperationState({
      active: true,
      kind: 'validate',
      level: 'info',
      title: `Validating ${selectedProject.name}`,
      detail: 'Checking source/destination pin conflicts and project structure.',
      phase: 'validation'
    });
    try {
      const result = await api.validateProject(selectedProject.id);
      pushActivity({
        kind: 'validate',
        level: result.ok ? 'success' : 'warning',
        title: result.ok ? `Validation passed for ${selectedProject.name}` : `Validation review required for ${selectedProject.name}`,
        detail: result.ok ? 'No blocking conflicts were found.' : `${result.errors.length} errors and ${result.warnings.length} warnings found.`,
        projectId: selectedProject.id,
        buildProfile: selectedBuildProfile
      });
      setOperationState({
        active: false,
        kind: 'validate',
        level: result.ok ? 'success' : 'warning',
        title: result.ok ? 'Validation complete' : 'Validation found issues',
        detail: result.ok ? 'The project is structurally ready for build and flash.' : 'Review the validation details in the Projects workspace before production flashing.',
        progress: 100,
        phase: result.ok ? 'complete' : 'review_required',
        nextStep: result.ok ? 'Build or flash the selected profile.' : 'Open Projects -> Validation for details.'
      });
    } catch (error) {
      const message = error instanceof Error ? error.message : String(error);
      pushActivity({
        kind: 'validate',
        level: 'error',
        title: `Validation failed for ${selectedProject.name}`,
        detail: message,
        projectId: selectedProject.id,
        buildProfile: selectedBuildProfile
      });
      setOperationState({
        active: false,
        kind: 'validate',
        level: 'error',
        title: 'Validation failed',
        detail: message,
        progress: 100,
        phase: 'error',
        nextStep: 'Retry validation after resolving the error.'
      });
    } finally {
      setGlobalProjectBusy(null);
    }
  };

  const globalBuildProject = async () => {
    if (!selectedProject || selectedProject.status === 'draft_local_only') return;
    setGlobalProjectBusy('build');
    setOperationState({
      active: true,
      kind: 'build',
      level: 'info',
      title: `Building ${selectedProject.name}`,
      detail: `Producing the ${selectedBuildProfile} firmware artifact set.`,
      phase: 'build'
    });
    try {
      const result = await api.buildProject(selectedProject.id, selectedBuildProfile);
      log(`BUILD ${selectedProject.id}:${selectedBuildProfile} ${result.ok ? 'ok' : 'failed'}`);
      pushActivity({
        kind: 'build',
        level: result.ok ? 'success' : 'error',
        title: `${selectedProject.name} ${result.ok ? 'build complete' : 'build failed'}`,
        detail: result.ok ? `${selectedBuildProfile} build artifacts are ready.` : result.error || 'Build returned a non-zero status.',
        projectId: selectedProject.id,
        buildProfile: selectedBuildProfile
      });
      setOperationState({
        active: false,
        kind: 'build',
        level: result.ok ? 'success' : 'error',
        title: result.ok ? 'Build complete' : 'Build failed',
        detail: result.ok ? 'The artifact set is ready for browser flash or inspection.' : result.error || 'Build returned a non-zero status.',
        progress: 100,
        phase: result.ok ? 'complete' : 'error',
        nextStep: result.ok ? 'Flash in browser or inspect the Result tab in Projects.' : 'Inspect build output and retry.'
      });
    } catch (error) {
      const message = (error as Error).message;
      log(`BUILD error ${message}`);
      pushActivity({
        kind: 'build',
        level: 'error',
        title: `Build failed for ${selectedProject.name}`,
        detail: message,
        projectId: selectedProject.id,
        buildProfile: selectedBuildProfile
      });
      setOperationState({
        active: false,
        kind: 'build',
        level: 'error',
        title: 'Build failed',
        detail: message,
        progress: 100,
        phase: 'error',
        nextStep: 'Inspect build output and retry.'
      });
    } finally {
      setGlobalProjectBusy(null);
    }
  };

  const confirmProductionFlash = async () => {
    if (selectedBuildProfile !== 'production') return true;
    return new Promise<boolean>((resolve) => {
      Modal.confirm({
        title: 'Flash production firmware?',
        icon: <WarningOutlined />,
        content: 'Production flashing intentionally leaves the board in product mode and disconnects live lab tooling until a lab or telemetry build is flashed again.',
        okText: 'Flash production',
        okButtonProps: { danger: true },
        cancelText: 'Cancel',
        onOk: () => resolve(true),
        onCancel: () => resolve(false)
      });
    });
  };

  const globalBrowserFlash = async () => {
    if (!selectedProject || selectedProject.status === 'draft_local_only' || !browserSerialSupported) return;
    if (!(await confirmProductionFlash())) {
      pushActivity({
        kind: 'flash',
        level: 'info',
        title: 'Production flash canceled',
        detail: 'The browser flash was canceled before serial ownership changed.',
        projectId: selectedProject.id,
        buildProfile: selectedBuildProfile
      });
      return;
    }
    setGlobalProjectBusy('flash');
    try {
      const manifest = await api.flashManifest(selectedProject.id, selectedBuildProfile);
      await runBrowserFlashSession({
        manifest,
        buildProfile: selectedBuildProfile,
        releaseSerial: () => api.releaseSerial(),
        reconnectSerial: () => api.reconnectSerial(),
        onLog: (line) => {
          if (line.trim()) log(`FLASH ${line}`);
        },
        onProgress: (progress) => {
          setOperationState((current) => current ? { ...current, progress } : current);
        },
        onPhase: (phase, detail) => {
          setOperationState({
            active: !['complete', 'error'].includes(phase),
            kind: 'flash',
            level: phase === 'error' ? 'error' : phase === 'complete' ? 'success' : 'info',
            title: `Flashing ${selectedProject.name}`,
            detail: detail || 'Browser flash in progress.',
            progress: phase === 'complete' ? 100 : undefined,
            phase,
            nextStep: phase === 'awaiting_port' ? 'Approve the Chrome/Edge serial prompt.' : phase === 'complete'
              ? (selectedBuildProfile === 'production' ? 'Reflash lab firmware before using the monitor again.' : 'Return to Graph or Monitor.')
              : undefined
          });
        }
      });
      log(`FLASH ${selectedProject.id}:${selectedBuildProfile} complete`);
      pushActivity({
        kind: 'flash',
        level: 'success',
        title: `Flashed ${selectedProject.name}`,
        detail: `${selectedBuildProfile} image written successfully via browser flash.`,
        projectId: selectedProject.id,
        buildProfile: selectedBuildProfile
      });
      if (selectedBuildProfile !== 'production') {
        const refreshed = await api.status();
        setStatus(refreshed);
      }
    } catch (error) {
      const message = (error as Error).message;
      log(`FLASH error ${message}`);
      pushActivity({
        kind: 'flash',
        level: 'error',
        title: `Flash failed for ${selectedProject.name}`,
        detail: message,
        projectId: selectedProject.id,
        buildProfile: selectedBuildProfile
      });
      setOperationState({
        active: false,
        kind: 'flash',
        level: 'error',
        title: 'Flash failed',
        detail: message,
        progress: 100,
        phase: 'error',
        nextStep: 'Inspect the operation log and browser-flash details, then retry.'
      });
    } finally {
      setGlobalProjectBusy(null);
      refresh();
    }
  };

  useEffect(() => {
    const handler = (event: KeyboardEvent) => {
      const target = event.target as HTMLElement | null;
      const tagName = target?.tagName || '';
      const editing = target?.isContentEditable || ['INPUT', 'TEXTAREA', 'SELECT'].includes(tagName);
      if (editing) return;
      if (!(event.ctrlKey || event.metaKey)) return;

      const key = event.key.toLowerCase();
      if (key === 's') {
        event.preventDefault();
        globalSaveProject();
        return;
      }
      if (key === 'b' && !event.shiftKey) {
        event.preventDefault();
        globalBuildProject();
        return;
      }
      if (key === 'v' && event.shiftKey) {
        event.preventDefault();
        globalValidateProject();
        return;
      }
      if (key === 'f' && event.shiftKey) {
        event.preventDefault();
        globalBrowserFlash();
      }
    };
    window.addEventListener('keydown', handler);
    return () => window.removeEventListener('keydown', handler);
  }, [globalBrowserFlash, globalBuildProject, globalSaveProject, globalValidateProject]);

  const page = selected === 'dashboard' ? <DashboardPage profile={profile} status={status} blocks={blocks} destinationProfile={destinationProfile} selectedProject={selectedProject} /> :
    selected === 'project' ? <ProjectPage status={status} blocks={blocks} projects={projects} selectedProject={selectedProject} selectedProjectId={selectedProjectId} selectedBuildProfile={selectedBuildProfile} onSelectBuildProfile={setSelectedBuildProfile} onSelectProject={setSelectedProjectId} onCreateProject={createLocalProject} onSaveProject={saveProject} onDuplicateProject={duplicateProject} onDeleteProject={deleteProject} onProjectsChanged={replaceProjects} onOpenProject={(projectId, pageName = 'project') => { setSelectedProjectId(projectId); setSelected(pageName); }} onActivity={pushActivity} onOperationStateChange={setOperationState} /> :
    selected === 'graph' ? <GraphPage status={status} blocks={blocks} selectedProject={selectedProject} destinationProfile={destinationProfile} onProjectDraftChange={updateProjectDraft} onSaveProject={saveProject} /> :
    selected === 'source' ? <SourcePage profile={profile} pins={pins} /> :
    selected === 'processing' ? <ProcessingPage profile={profile} /> :
    selected === 'destination' ? <DestinationPage destinationProfile={destinationProfile} sourcePins={pins} /> :
    selected === 'live' ? <LivePage status={status} onStart={actions.start} onStop={actions.stop} onRecover={actions.recover} onSafeIdle={actions.safeIdle} /> :
    selected === 'artifacts' ? <ArtifactsPage items={artifacts} /> :
    selected === 'profile' ? <ProfilePage profile={profile} /> :
    <LogsPage logs={logs} />;

  return (
    <Layout className="appShell">
      <Header className="appHeader">
        <HeaderStatus selectedProject={selectedProject} status={status} />
        <GlobalProjectBar
          projects={projects}
          selectedProject={selectedProject}
          selectedProjectId={selectedProjectId}
          onSelectProject={setSelectedProjectId}
          selectedBuildProfile={selectedBuildProfile}
          onSelectBuildProfile={setSelectedBuildProfile}
          onSave={globalSaveProject}
          onValidate={globalValidateProject}
          onBuild={globalBuildProject}
          onFlash={globalBrowserFlash}
          onOpenHistory={() => setHistoryOpen(true)}
          busy={globalProjectBusy}
          browserSerialSupported={browserSerialSupported}
          operation={operationState}
          latestHistory={operationHistory[0] || null}
        />
      </Header>
      <Layout>
        <Sider width={220} className="appSider">
          <Menu
            mode="inline"
            selectedKeys={[selected]}
            items={navItems}
            onClick={(item) => setSelected(item.key)}
          />
        </Sider>
        <Content className="appContent">{page}</Content>
      </Layout>
      <Modal
        open={historyOpen}
        title="Operation History"
        footer={null}
        onCancel={() => setHistoryOpen(false)}
        width={760}
      >
        <Space direction="vertical" className="fullWidth" size="small">
          {operationHistory.length === 0 ? (
            <Empty image={Empty.PRESENTED_IMAGE_SIMPLE} description="No operations yet" />
          ) : operationHistory.map((entry) => (
            <Alert
              key={entry.id}
              type={entry.level}
              showIcon
              message={`${entry.title}${entry.buildProfile ? ` · ${entry.buildProfile}` : ''}`}
              description={`${entry.timestamp}${entry.projectId ? ` · ${entry.projectId}` : ''} · ${entry.detail}`}
            />
          ))}
        </Space>
      </Modal>
    </Layout>
  );
}
