import {
  Alert,
  Badge,
  Button,
  Card,
  Descriptions,
  Empty,
  Layout,
  List,
  Menu,
  Space,
  Statistic,
  Table,
  Tag,
  Typography
} from 'antd';
import {
  ApartmentOutlined,
  ApiOutlined,
  BugOutlined,
  DatabaseOutlined,
  DesktopOutlined,
  ExperimentOutlined,
  FileSearchOutlined,
  PlayCircleOutlined,
  ProfileOutlined
} from '@ant-design/icons';
import { useEffect, useMemo, useRef, useState } from 'react';
import { api, ArtifactItem, PinRow, TargetProfile, WorkbenchStatus } from './api';

const { Header, Sider, Content } = Layout;
const { Text, Title } = Typography;

const navItems = [
  { key: 'project', icon: <ApartmentOutlined />, label: 'Project' },
  { key: 'source', icon: <ExperimentOutlined />, label: 'Source' },
  { key: 'processing', icon: <ApiOutlined />, label: 'Processing' },
  { key: 'destination', icon: <DesktopOutlined />, label: 'Destination' },
  { key: 'live', icon: <PlayCircleOutlined />, label: 'Live' },
  { key: 'artifacts', icon: <DatabaseOutlined />, label: 'Artifacts' },
  { key: 'profile', icon: <ProfileOutlined />, label: 'Profile' },
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

function HeaderStatus({ profile, status }: { profile: TargetProfile | null; status: WorkbenchStatus | null }) {
  return (
    <Space size="middle" wrap>
      <Text strong>ESP32-P4 Signal Lab</Text>
      <Tag color="blue">{profile?.profile_id || 'no-profile'}</Tag>
      <Badge status={statusColor(status)} text={status?.source_state || 'unknown'} />
      <Tag>{profile?.current_capture_profile?.data_mode || 'mode ?'}</Tag>
      <Text type="secondary">FPS {status?.server_capture_fps ?? '?'}</Text>
      <Text type="secondary">Frame age {status?.server_frame_age_ms ?? '?'} ms</Text>
    </Space>
  );
}

function ProjectPage({ profile, status }: { profile: TargetProfile | null; status: WorkbenchStatus | null }) {
  return (
    <Space direction="vertical" size="middle" className="pageStack">
      <Alert
        type="info"
        showIcon
        message="Pipeline"
        description="Source bus -> ESP32-P4 capture/processing -> destination. The browser and AI workbench control and observe the ESP32-P4 through profile-aware APIs."
      />
      <div className="statsGrid">
        <Card><Statistic title="Source" value={profile?.display_name || 'unknown'} /></Card>
        <Card><Statistic title="Processing" value={profile?.current_capture_profile?.capture_peripheral || 'source capture'} /></Card>
        <Card><Statistic title="Destination" value="browser/artifacts" /></Card>
        <Card><Statistic title="Frames" value={status?.server_frame_count ?? 0} /></Card>
      </div>
      <Card title="Instrument">
        <Descriptions bordered size="small" column={2}>
          <Descriptions.Item label="Running">{String(status?.running ?? false)}</Descriptions.Item>
          <Descriptions.Item label="Source">{status?.source_state || '?'}</Descriptions.Item>
          <Descriptions.Item label="Last capture">{status?.server_last_capture_ms ?? '?'} ms</Descriptions.Item>
          <Descriptions.Item label="Errors">{status?.consecutive_errors ?? 0}</Descriptions.Item>
          <Descriptions.Item label="Last error" span={2}>{status?.error || ''}</Descriptions.Item>
        </Descriptions>
      </Card>
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
    <Space direction="vertical" size="middle" className="pageStack">
      <Alert
        type="warning"
        showIcon
        message="Safety"
        description={(profile?.safety?.known_concerns || []).join(' ')}
      />
      <Card title="Dangerous / Do Not Connect">
        <Space wrap>
          {(profile?.safety?.dangerous_rails || []).map((rail) => <Tag color="red" key={rail}>{rail}</Tag>)}
        </Space>
      </Card>
      <Card title="Connected Profile GPIOs">
        <Table
          size="small"
          pagination={false}
          dataSource={pins.map((pin) => ({ ...pin, key: pin.gpio }))}
          columns={[
            { title: 'Signal', dataIndex: 'signal' },
            { title: 'GPIO', dataIndex: 'gpio' },
            { title: 'Bus Pin', dataIndex: 'bus_pin' },
            { title: 'Role', dataIndex: 'role' }
          ]}
        />
      </Card>
      <Card title="Timing / Control Signals">
        <Table
          size="small"
          pagination={false}
          dataSource={timingRows}
          columns={[
            { title: 'Signal', dataIndex: 'signal' },
            { title: 'GPIO', dataIndex: 'gpio' },
            { title: 'Bus Pin', dataIndex: 'bus' },
            { title: 'Candidate Roles', dataIndex: 'roles' }
          ]}
        />
      </Card>
    </Space>
  );
}

function ProcessingPage({ profile }: { profile: TargetProfile | null }) {
  return (
    <Space direction="vertical" size="middle" className="pageStack">
      <Alert type="info" showIcon message="Processing is not a product chain yet" description="Current active block is source capture. Future blocks include retiming, line buffering, frame buffering, color conversion, scaling, overlays, and USB streaming." />
      <Card title="Active Capture Block">
        <Descriptions bordered size="small" column={1}>
          <Descriptions.Item label="Block">source_capture</Descriptions.Item>
          <Descriptions.Item label="Backend">{profile?.current_capture_profile?.capture_peripheral || 'LCD_CAM'}</Descriptions.Item>
          <Descriptions.Item label="Mode">{profile?.current_capture_profile?.data_mode || '?'}</Descriptions.Item>
          <Descriptions.Item label="Transport"><JsonBlock value={profile?.current_capture_profile?.transport} /></Descriptions.Item>
        </Descriptions>
      </Card>
    </Space>
  );
}

function DestinationPage() {
  return (
    <Card>
      <Empty
        image={Empty.PRESENTED_IMAGE_SIMPLE}
        description="No destination profile configured yet"
      >
        <Text type="secondary">Next: choose a panel/protocol, document electrical requirements, and add test-pattern output before source-to-panel bridging.</Text>
      </Empty>
    </Card>
  );
}

type LiveFrameMeta = {
  dataMode: string;
  bytes: number;
  frame: number | string;
  captureMs: number | string;
  receivedAt: string;
};

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

function drawFrame(canvas: HTMLCanvasElement, raw: Uint8Array, dataMode: string) {
  const ctx = canvas.getContext('2d');
  if (!ctx) return;
  if (!['RGB565', 'RGB664', 'RGB666'].includes(dataMode)) {
    drawMessage(canvas, `unsupported frame mode ${dataMode || 'unknown'}`);
    return;
  }
  const visibleWidth = 160;
  const visibleHeight = 144;
  const streamWidth = 161;
  const bytesPerPixel = dataMode === 'RGB666' ? 3 : 2;
  const image = ctx.createImageData(visibleWidth, visibleHeight);
  for (let y = 0; y < visibleHeight; y += 1) {
    for (let x = 0; x < visibleWidth; x += 1) {
      const src = (y * streamWidth + x) * bytesPerPixel;
      const [r, g, b] = dataMode === 'RGB565'
        ? pixelRgb565(raw, src)
        : dataMode === 'RGB664'
          ? pixelRgb664(raw, src)
          : pixelRgb666(raw, src);
      const dst = (y * visibleWidth + x) * 4;
      image.data[dst] = r;
      image.data[dst + 1] = g;
      image.data[dst + 2] = b;
      image.data[dst + 3] = 255;
    }
  }
  ctx.putImageData(image, 0, 0);
}

function LivePage({ status, onStart, onStop, onRecover, onSafeIdle }: {
  status: WorkbenchStatus | null;
  onStart: () => void;
  onStop: () => void;
  onRecover: () => void;
  onSafeIdle: () => void;
}) {
  const canvasRef = useRef<HTMLCanvasElement | null>(null);
  const [frameMeta, setFrameMeta] = useState<LiveFrameMeta | null>(null);
  const [frameError, setFrameError] = useState('');

  useEffect(() => {
    if (!canvasRef.current) return;
    if (!status?.running) {
      drawMessage(canvasRef.current, 'live capture stopped');
      return;
    }
    let cancelled = false;
    let busy = false;
    const fetchFrame = async () => {
      if (busy || cancelled) return;
      busy = true;
      try {
        const frame = await api.frame();
        if (cancelled || !canvasRef.current) return;
        const dataMode = String(frame.metadata.pixel_format || frame.metadata.data_mode || 'RGB565');
        drawFrame(canvasRef.current, frame.bytes, dataMode);
        setFrameMeta({
          dataMode,
          bytes: frame.bytes.length,
          frame: String(frame.metadata.server_frame_count ?? '?'),
          captureMs: String(frame.metadata.server_last_capture_ms ?? '?'),
          receivedAt: new Date().toLocaleTimeString()
        });
        setFrameError('');
      } catch (error) {
        if (!cancelled && canvasRef.current) {
          const message = (error as Error).message.trim();
          drawMessage(canvasRef.current, message.includes('waiting') ? 'waiting for source' : 'no current live frame');
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
  }, [status?.running]);

  return (
    <div className="liveGrid">
      <Card className="liveCard" title="Live Monitor" extra={<Badge status={statusColor(status)} text={status?.source_state || 'unknown'} />}>
        <div className="nativeLiveSurface">
          <canvas ref={canvasRef} className="nativeLiveCanvas" width={160} height={144} />
        </div>
      </Card>
      <Card title="Controls">
        <Space direction="vertical" className="fullWidth">
          <Space wrap>
            <Button type="primary" onClick={onStart}>Start</Button>
            <Button danger onClick={onStop}>Stop</Button>
            <Button onClick={onRecover}>Recover</Button>
            <Button onClick={onSafeIdle}>Safe Idle</Button>
          </Space>
          <Descriptions size="small" bordered column={1}>
            <Descriptions.Item label="Running">{String(status?.running ?? false)}</Descriptions.Item>
            <Descriptions.Item label="FPS">{status?.server_capture_fps ?? '?'}</Descriptions.Item>
            <Descriptions.Item label="Frame age">{status?.server_frame_age_ms ?? '?'} ms</Descriptions.Item>
            <Descriptions.Item label="Mode">{frameMeta?.dataMode || '?'}</Descriptions.Item>
            <Descriptions.Item label="Bytes">{frameMeta?.bytes ?? '?'}</Descriptions.Item>
            <Descriptions.Item label="Frame">{frameMeta?.frame ?? '?'}</Descriptions.Item>
            <Descriptions.Item label="Received">{frameMeta?.receivedAt || '?'}</Descriptions.Item>
            <Descriptions.Item label="Error">{status?.error || ''}</Descriptions.Item>
          </Descriptions>
          {frameError ? <Alert type="warning" showIcon message="Live frame issue" description={frameError} /> : null}
        </Space>
      </Card>
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
      <Card title="Current Capture Profile">
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
  const [selected, setSelected] = useState('project');
  const [status, setStatus] = useState<WorkbenchStatus | null>(null);
  const [profile, setProfile] = useState<TargetProfile | null>(null);
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

  const page = selected === 'project' ? <ProjectPage profile={profile} status={status} /> :
    selected === 'source' ? <SourcePage profile={profile} pins={pins} /> :
    selected === 'processing' ? <ProcessingPage profile={profile} /> :
    selected === 'destination' ? <DestinationPage /> :
    selected === 'live' ? <LivePage status={status} onStart={actions.start} onStop={actions.stop} onRecover={actions.recover} onSafeIdle={actions.safeIdle} /> :
    selected === 'artifacts' ? <ArtifactsPage items={artifacts} /> :
    selected === 'profile' ? <ProfilePage profile={profile} /> :
    <LogsPage logs={logs} />;

  return (
    <Layout className="appShell">
      <Header className="appHeader">
        <HeaderStatus profile={profile} status={status} />
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
