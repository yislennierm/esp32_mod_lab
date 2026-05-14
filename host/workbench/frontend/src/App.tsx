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
  Typography
} from 'antd';
import {
  ApartmentOutlined,
  ApiOutlined,
  ArrowDownOutlined,
  ArrowUpOutlined,
  BugOutlined,
  CheckCircleOutlined,
  DatabaseOutlined,
  DesktopOutlined,
  DisconnectOutlined,
  ExperimentOutlined,
  FileSearchOutlined,
  FilterOutlined,
  PlayCircleOutlined,
  PlusOutlined,
  ProfileOutlined,
  ReloadOutlined,
  RocketOutlined,
  SaveOutlined,
  ThunderboltOutlined,
  GithubOutlined,
  LinkOutlined
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
  LabBlock,
  LabProject,
  PinRow,
  ProjectActionResult,
  ProjectValidation,
  SdkExample,
  SdkInventorySummary,
  TargetProfile,
  WorkbenchStatus
} from './api';

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
  { key: 'sdk', icon: <GithubOutlined />, label: 'SDK' },
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
  return (
    <Space size="middle" wrap>
      <Text strong>ESP32-P4 Signal Lab</Text>
      <Tag color="blue">{selectedProject?.name || 'no project'}</Tag>
      <Tag color={status?.device_connected ? 'green' : 'orange'}>{status?.device_connected ? 'device online' : 'offline mode'}</Tag>
      <Badge status={statusColor(status)} text={status?.running ? 'lab stream active' : 'lab idle'} />
    </Space>
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
    mcu_blocks: ['HP RISC-V x2', 'GPIO Matrix / IO MUX'],
    graph: {
      nodes: [],
      edges: []
    }
  };
}

function ProjectPage({
  profile,
  status,
  blocks,
  projects,
  selectedProject,
  selectedProjectId,
  onSelectProject,
  onCreateProject,
  onSaveProject,
  onDuplicateProject,
  onDeleteProject
}: {
  profile: TargetProfile | null;
  status: WorkbenchStatus | null;
  blocks: LabBlock[];
  projects: LabProject[];
  selectedProject: LabProject | null;
  selectedProjectId: string;
  onSelectProject: (projectId: string) => void;
  onCreateProject: () => Promise<void>;
  onSaveProject: (project: LabProject) => Promise<void>;
  onDuplicateProject: (project: LabProject) => Promise<void>;
  onDeleteProject: (project: LabProject) => Promise<void>;
}) {
  const [validation, setValidation] = useState<ProjectValidation | null>(null);
  const [projectBusy, setProjectBusy] = useState<string | null>(null);
  const [projectResult, setProjectResult] = useState<ProjectActionResult | null>(null);

  const validateProject = async () => {
    if (!selectedProject || selectedProject.status === 'draft_local_only') return;
    setProjectBusy('validate');
    try {
      const result = await api.validateProject(selectedProject.id);
      setValidation(result);
      setProjectResult(null);
    } catch (error) {
      setValidation({
        ok: false,
        project_id: selectedProject?.id || '',
        errors: [error instanceof Error ? error.message : String(error)],
        warnings: [],
        source_gpios: [],
        destination_gpios: []
      });
    } finally {
      setProjectBusy(null);
    }
  };

  const runProjectAction = async (action: 'build' | 'flash') => {
    if (!selectedProject || selectedProject.status === 'draft_local_only') return;
    setProjectBusy(action);
    try {
      const result = action === 'build'
        ? await api.buildProject(selectedProject.id)
        : await api.flashProject(selectedProject.id);
      setProjectResult(result);
    } catch (error) {
      setProjectResult({
        ok: false,
        project_id: selectedProject?.id || '',
        action,
        error: error instanceof Error ? error.message : String(error)
      });
    } finally {
      setProjectBusy(null);
    }
  };

  const projectOptions = projects.map((project) => ({ value: project.id, label: project.status === 'draft_local_only' ? `${project.name} (local)` : project.name }));

  return (
    <Space direction="vertical" size="small" className="pageStack">
      <div className="projectGrid">
        <Space direction="vertical" size="small" className="fullWidth">
          <Card
            size="small"
            title="Project Composer"
            extra={<Tag color={selectedProject?.status.includes('validated') ? 'green' : selectedProject?.status === 'draft_local_only' ? 'orange' : 'blue'}>{selectedProject?.status || 'none'}</Tag>}
          >
            <Space direction="vertical" size="small" className="fullWidth">
              <Space.Compact className="fullWidth">
                <Select className="fullWidth" value={selectedProjectId || undefined} options={projectOptions} onChange={onSelectProject} />
                <Button icon={<PlusOutlined />} onClick={onCreateProject}>New</Button>
              </Space.Compact>
              <MetricStrip items={[
                { label: 'Open Project', value: selectedProject?.id || 'none' },
                { label: 'Graph Nodes', value: selectedProject?.graph?.nodes?.length ?? 0 },
                { label: 'Projects', value: projects.length },
                { label: 'Device', value: status?.device_connected ? 'online' : 'offline' }
              ]} />
              <Descriptions bordered size="small" column={2}>
                <Descriptions.Item label="Ingress role">{selectedProject?.source?.block || '?'}</Descriptions.Item>
                <Descriptions.Item label="Transform roles">{selectedProject?.processing?.length ? selectedProject.processing.map((item) => item.block).join(', ') : 'none'}</Descriptions.Item>
                <Descriptions.Item label="Egress role">{selectedProject?.destination?.block || '?'}</Descriptions.Item>
                <Descriptions.Item label="Deploy mode">{selectedProject?.production?.default_env?.PRODUCTION_MIRROR_MODE ? `mode ${selectedProject.production.default_env.PRODUCTION_MIRROR_MODE}` : '?'}</Descriptions.Item>
              </Descriptions>
              <Space wrap>
                <Button icon={<CheckCircleOutlined />} loading={projectBusy === 'validate'} disabled={!selectedProject || selectedProject.status === 'draft_local_only'} onClick={validateProject}>Validate</Button>
                <Button icon={<BugOutlined />} loading={projectBusy === 'build'} disabled={!selectedProject || selectedProject.status === 'draft_local_only'} onClick={() => runProjectAction('build')}>Build</Button>
                <Button type="primary" danger icon={<RocketOutlined />} loading={projectBusy === 'flash'} disabled={!selectedProject || selectedProject.status === 'draft_local_only'} onClick={() => runProjectAction('flash')}>Flash Production</Button>
                <Button disabled={!selectedProject} onClick={() => selectedProject && onSaveProject(selectedProject)}>Save</Button>
                <Button disabled={!selectedProject} onClick={() => selectedProject && onDuplicateProject(selectedProject)}>Duplicate</Button>
                <Button danger disabled={!selectedProject} onClick={() => selectedProject && onDeleteProject(selectedProject)}>Delete</Button>
              </Space>
              <Collapse
                ghost
                size="small"
                items={[{
                  key: 'deploy-notes',
                  label: 'Deploy notes',
                  children: (
                    <Space direction="vertical" className="fullWidth">
                      <Text type="secondary">Flash releases the workbench serial session first. After production firmware is flashed, restart the lab firmware to return to interactive commands.</Text>
                      <JsonBlock value={selectedProject?.production} />
                    </Space>
                  )
                }]}
              />
            </Space>
          </Card>
        </Space>
        <Space direction="vertical" size="small" className="fullWidth">
          <Card size="small" title="Project Detail">
            <Tabs
              size="small"
              items={[
                {
                  key: 'blocks',
                  label: 'Blocks',
                  children: (
                    <Table
                      size="small"
                      pagination={false}
                      dataSource={projectBlockRows(selectedProject, blocks)}
                      columns={[
                        { title: 'Kind', dataIndex: 'kind', width: 86, render: (kind: string) => <Tag>{kind}</Tag> },
                        { title: 'Block', dataIndex: 'name' },
                        { title: 'Origin', dataIndex: 'origin', width: 82, render: (origin: string) => <Tag>{origin}</Tag> },
                        {
                          title: 'Status',
                          dataIndex: 'status',
                          width: 118,
                          render: (blockStatus: string) => <Tag color={blockStatus.includes('validated') || blockStatus.includes('working') ? 'green' : 'blue'}>{blockStatus}</Tag>
                        }
                      ]}
                    />
                  )
                },
                {
                  key: 'validation',
                  label: 'Validation',
                  children: validation ? (
                    <Space direction="vertical" className="fullWidth">
                      <Tag color={validation.ok ? 'green' : 'red'}>{validation.ok ? 'valid' : 'blocked'}</Tag>
                      {validation.errors.map((error) => <Alert key={error} type="error" showIcon message={error} />)}
                      {validation.warnings.map((warning) => <Alert key={warning} type="warning" showIcon message={warning} />)}
                      <JsonBlock value={{ source_gpios: validation.source_gpios, destination_gpios: validation.destination_gpios }} />
                    </Space>
                  ) : <Empty image={Empty.PRESENTED_IMAGE_SIMPLE} description="Validate a project" />
                },
                {
                  key: 'result',
                  label: 'Result',
                  children: projectResult ? <JsonBlock value={projectResult} /> : <Empty image={Empty.PRESENTED_IMAGE_SIMPLE} description="No build or flash result yet" />
                }
              ]}
            />
          </Card>
        </Space>
      </div>
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

function Esp32p4Dashboard({ activeBlocks }: { activeBlocks: Set<string> }) {
  return (
    <div className="mcuDashboard">
      {esp32p4Regions.map((region) => (
        <div className="mcuRegion" key={region.title}>
          <div className="mcuRegionTitle">{region.title}</div>
          <div className="mcuBlockGrid">
            {region.blocks.map((block) => {
              const active = activeBlocks.has(block);
              return <Tag className={`mcuBlock ${active ? 'mcuBlockActive' : ''}`} color={active ? 'green' : 'default'} key={block}>{block}</Tag>;
            })}
          </div>
        </div>
      ))}
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

  return (
    <Space direction="vertical" size="small" className="pageStack">
      <MetricStrip items={[
        { label: 'Device', value: status?.device_connected ? <Tag color="green">online</Tag> : <Tag color="orange">offline</Tag> },
        { label: 'Project', value: selectedProject?.name || 'none' },
        { label: 'Signal Profile', value: profile?.profile_id || 'unknown' },
        { label: 'I/O Profile', value: destinationProfile?.profile_id || 'unknown' }
      ]} />
      <Card size="small" title="ESP32-P4 Block Dashboard">
        <Esp32p4Dashboard activeBlocks={activeBlocks} />
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
  const [graphTool, setGraphTool] = useState('select');
  const [selectedGraphNode, setSelectedGraphNode] = useState<Node | null>(null);
  const [graphDirty, setGraphDirty] = useState(false);

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
      return graphNodes.map((node, index) => {
        const position = node.position && typeof node.position === 'object' ? node.position as { x?: unknown; y?: unknown } : {};
        const label = String(node.label || node.type || node.id || `node_${index}`);
        const type = String(node.type || 'block');
        return {
          id: String(node.id || `node_${index}`),
          position: {
            x: typeof position.x === 'number' ? position.x : index * 300,
            y: typeof position.y === 'number' ? position.y : 180
          },
          className: `flowNodeType-${type.replace(/[^A-Za-z0-9_-]+/g, '-')}`,
          data: {
            raw: node,
            type,
            label: (
              <div className="flowNode">
                <Tag color={graphNodeColor(type)}>{type}</Tag>
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
  const isRtosTaskNode = selectedRawNode?.type === 'lab_function_block'
    && (selectedNodeParams.api_group === 'freertos' || String(selectedRawNode.label || '').toLowerCase().includes('task'));
  const selectedNodeId = selectedRawNode ? String(selectedRawNode.id || selectedGraphNode?.id || '') : '';
  const selectedNodeType = selectedRawNode ? String(selectedRawNode.type || 'block') : '';
  const selectedNodeLabel = selectedRawNode ? String(selectedRawNode.label || selectedNodeId || 'Block') : '';
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
  const rtosTaskOverlay = {
    enabled: typeof selectedNodeOverlay.enabled === 'boolean' ? selectedNodeOverlay.enabled : true,
    task_name: String(selectedNodeOverlay.task_name || 'app_main'),
    priority: typeof selectedNodeOverlay.priority === 'number' ? selectedNodeOverlay.priority : 5,
    stack_size_bytes: typeof selectedNodeOverlay.stack_size_bytes === 'number' ? selectedNodeOverlay.stack_size_bytes : 4096,
    core_affinity: String(selectedNodeOverlay.core_affinity || 'any'),
    notes: String(selectedNodeOverlay.notes || ''),
    source_write_policy: String(selectedNodeOverlay.source_write_policy || 'overlay_only')
  };

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

  return (
    <div className={`graphWorkspace ${inspectorOpen ? 'inspectorOpen' : ''}`}>
      <div className="graphTopBar">
        <Space>
          <Text strong>Project Flowgraph</Text>
          <Tag color={project?.status.includes('validated') ? 'green' : 'blue'}>{project?.id || 'no-project'}</Tag>
          <Tag>{status?.device_connected ? 'device online' : 'offline'}</Tag>
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
      <div className="graphSurface">
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
                        <Text strong>{selectedNodeLabel}</Text>
                      </Space>
                      <Text type="secondary">{selectedNodeId}</Text>
                      <Space wrap>
                        <Tag color={isRtosTaskNode ? 'green' : 'default'}>{isRtosTaskNode ? 'typed editor' : 'read-only inspector'}</Tag>
                        {selectedNodeOverlay.source_write_policy ? <Tag color="blue">{String(selectedNodeOverlay.source_write_policy)}</Tag> : null}
                        {graphDirty ? <Tag color="orange">unsaved overlay/layout</Tag> : null}
                      </Space>
                    </Space>
                  </Card>
                  {isRtosTaskNode ? (
                    <Card size="small" title="RTOS Task" className="inspectorEditorCard">
                      <Space direction="vertical" size="middle" className="fullWidth">
                        <Space className="fullWidth" align="center">
                          <Text className="editorLabel">Enabled</Text>
                          <Switch
                            checked={rtosTaskOverlay.enabled}
                            onChange={(enabled) => updateSelectedNodeOverlay({ enabled })}
                          />
                          <Tag color="blue">draft</Tag>
                        </Space>
                        <div>
                          <Text className="editorLabel">Task name</Text>
                          <Input
                            value={rtosTaskOverlay.task_name}
                            onChange={(event) => updateSelectedNodeOverlay({ task_name: event.target.value })}
                          />
                        </div>
                        <div>
                          <Space className="editorHeaderRow">
                            <Text className="editorLabel">Priority</Text>
                            <InputNumber
                              min={1}
                              max={24}
                              value={rtosTaskOverlay.priority}
                              onChange={(value) => updateSelectedNodeOverlay({ priority: Number(value || 1) })}
                            />
                          </Space>
                          <Slider
                            min={1}
                            max={24}
                            value={rtosTaskOverlay.priority}
                            onChange={(value) => updateSelectedNodeOverlay({ priority: Number(value) })}
                          />
                        </div>
                        <div>
                          <Space className="editorHeaderRow">
                            <Text className="editorLabel">Stack size</Text>
                            <InputNumber
                              min={2048}
                              max={32768}
                              step={1024}
                              value={rtosTaskOverlay.stack_size_bytes}
                              addonAfter="bytes"
                              onChange={(value) => updateSelectedNodeOverlay({ stack_size_bytes: Number(value || 2048) })}
                            />
                          </Space>
                          <Slider
                            min={2048}
                            max={32768}
                            step={1024}
                            marks={{ 2048: '2K', 8192: '8K', 16384: '16K', 32768: '32K' }}
                            value={rtosTaskOverlay.stack_size_bytes}
                            onChange={(value) => updateSelectedNodeOverlay({ stack_size_bytes: Number(value) })}
                          />
                        </div>
                        <div>
                          <Text className="editorLabel">Core affinity</Text>
                          <Select
                            className="fullWidth"
                            value={rtosTaskOverlay.core_affinity}
                            options={[
                              { value: 'any', label: 'Any core' },
                              { value: 'core0', label: 'Core 0' },
                              { value: 'core1', label: 'Core 1' }
                            ]}
                            onChange={(core_affinity) => updateSelectedNodeOverlay({ core_affinity })}
                          />
                        </div>
                        <div>
                          <Text className="editorLabel">Notes</Text>
                          <Input.TextArea
                            rows={3}
                            value={rtosTaskOverlay.notes}
                            onChange={(event) => updateSelectedNodeOverlay({ notes: event.target.value })}
                            placeholder="Why this task exists, timing assumptions, and future generation notes."
                          />
                        </div>
                      </Space>
                    </Card>
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
  onProjectsChanged,
  onOpenProject
}: {
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
        onOpenProject(result.project.id, 'graph');
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

  return (
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
  const [selected, setSelected] = useState('dashboard');
  const [status, setStatus] = useState<WorkbenchStatus | null>(null);
  const [profile, setProfile] = useState<TargetProfile | null>(null);
  const [destinationProfile, setDestinationProfile] = useState<DestinationProfile | null>(null);
  const [blocks, setBlocks] = useState<LabBlock[]>([]);
  const [projects, setProjects] = useState<LabProject[]>([]);
  const [selectedProjectId, setSelectedProjectId] = useState('');
  const [artifacts, setArtifacts] = useState<ArtifactItem[]>([]);
  const [pins, setPins] = useState<PinRow[]>([]);
  const [logs, setLogs] = useState<string[]>([]);

  const log = (message: string) => setLogs((current) => [`${new Date().toISOString()} ${message}`, ...current].slice(0, 200));

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

  const page = selected === 'dashboard' ? <DashboardPage profile={profile} status={status} blocks={blocks} destinationProfile={destinationProfile} selectedProject={selectedProject} /> :
    selected === 'project' ? <ProjectPage profile={profile} status={status} blocks={blocks} projects={projects} selectedProject={selectedProject} selectedProjectId={selectedProjectId} onSelectProject={setSelectedProjectId} onCreateProject={createLocalProject} onSaveProject={saveProject} onDuplicateProject={duplicateProject} onDeleteProject={deleteProject} /> :
    selected === 'graph' ? <GraphPage status={status} blocks={blocks} selectedProject={selectedProject} destinationProfile={destinationProfile} onProjectDraftChange={updateProjectDraft} onSaveProject={saveProject} /> :
    selected === 'sdk' ? <SdkResearchPage onProjectsChanged={replaceProjects} onOpenProject={(projectId, pageName = 'project') => { setSelectedProjectId(projectId); setSelected(pageName); }} /> :
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
    </Layout>
  );
}
