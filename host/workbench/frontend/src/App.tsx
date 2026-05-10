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
  Segmented,
  Slider,
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

type VisualMode = 'clean' | 'grid' | 'mask' | 'gbc';

type VisualOptions = {
  mode: VisualMode;
  scale: number;
  grid: number;
  tint: number;
  contrast: number;
  persistence: number;
  lens: boolean;
  lensOpacity: number;
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
  const bytesPerPixel = dataMode === 'RGB666' ? 3 : 2;

  const pixelScale = options.scale <= 4 ? 3 : 6;
  const renderWidth = visibleWidth * pixelScale;
  const renderHeight = visibleHeight * pixelScale;
  const previous = options.persistence > 0 && canvas.width === renderWidth && canvas.height === renderHeight
    ? ctx.getImageData(0, 0, renderWidth, renderHeight)
    : null;

  if (canvas.width !== renderWidth || canvas.height !== renderHeight) {
    canvas.width = renderWidth;
    canvas.height = renderHeight;
  }

  const image = ctx.createImageData(renderWidth, renderHeight);
  const requestedGridAlpha = options.mode === 'grid' || options.mode === 'mask' || options.mode === 'gbc'
    ? options.grid / 100
    : 0;
  const gridAlpha = options.lens
    ? Math.min(requestedGridAlpha, 0.06)
    : requestedGridAlpha;
  const persistence = options.persistence / 100;

  for (let y = 0; y < visibleHeight; y += 1) {
    for (let x = 0; x < visibleWidth; x += 1) {
      const src = (y * streamWidth + x) * bytesPerPixel;
      const rgb = dataMode === 'RGB565'
        ? pixelRgb565(raw, src)
        : dataMode === 'RGB664'
          ? pixelRgb664(raw, src)
          : pixelRgb666(raw, src);
      const [r, g, b] = applyVisualColor(rgb[0], rgb[1], rgb[2], options);

      for (let py = 0; py < pixelScale; py += 1) {
        for (let px = 0; px < pixelScale; px += 1) {
          const dx = x * pixelScale + px;
          const dy = y * pixelScale + py;
          const dst = (dy * renderWidth + dx) * 4;
          let sr = r;
          let sg = g;
          let sb = b;

          if (options.mode === 'mask' || options.mode === 'gbc') {
            const sub = px % 3;
            const boost = options.mode === 'gbc' ? 1.08 : 1.12;
            const dim = options.mode === 'gbc' ? 0.78 : 0.72;
            sr *= sub === 0 ? boost : dim;
            sg *= sub === 1 ? boost : dim;
            sb *= sub === 2 ? boost : dim;
          }

          if (gridAlpha > 0 && (px === 0 || py === 0)) {
            sr *= 1 - gridAlpha;
            sg *= 1 - gridAlpha;
            sb *= 1 - gridAlpha;
          }

          if (options.mode === 'gbc') {
            const edgeX = Math.min(dx, renderWidth - 1 - dx) / renderWidth;
            const edgeY = Math.min(dy, renderHeight - 1 - dy) / renderHeight;
            const vignette = 0.88 + Math.min(edgeX, edgeY) * 1.6;
            sr *= Math.min(1, vignette);
            sg *= Math.min(1, vignette);
            sb *= Math.min(1, vignette);
          }

          if (previous && persistence > 0) {
            sr = sr * (1 - persistence) + previous.data[dst] * persistence;
            sg = sg * (1 - persistence) + previous.data[dst + 1] * persistence;
            sb = sb * (1 - persistence) + previous.data[dst + 2] * persistence;
          }

          image.data[dst] = clampByte(sr);
          image.data[dst + 1] = clampByte(sg);
          image.data[dst + 2] = clampByte(sb);
          image.data[dst + 3] = 255;
        }
      }
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
  const [visualOptions, setVisualOptions] = useState<VisualOptions>({
    mode: 'clean',
    scale: 6,
    grid: 18,
    tint: 26,
    contrast: 92,
    persistence: 0,
    lens: false,
    lensOpacity: 88
  });

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
        drawFrame(canvasRef.current, frame.bytes, dataMode, visualOptions);
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
  }, [status?.running, visualOptions]);

  return (
    <div className="liveGrid">
      <Card className="liveCard" title="Live Monitor" extra={<Badge status={statusColor(status)} text={status?.source_state || 'unknown'} />}>
        <div className="nativeLiveSurface">
          <div className={`nativeLiveFrame ${visualOptions.lens ? 'withLens' : 'withoutLens'}`}>
            <canvas ref={canvasRef} className="nativeLiveCanvas" width={640} height={576} />
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
      <Space direction="vertical" size="middle" className="fullWidth">
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
        <Card title="Visuals">
          <Space direction="vertical" className="fullWidth">
            <Segmented
              block
              value={visualOptions.mode}
              options={[
                { label: 'Clean', value: 'clean' },
                { label: 'Grid', value: 'grid' },
                { label: 'LCD Mask', value: 'mask' },
                { label: 'GBC LCD', value: 'gbc' }
              ]}
              onChange={(mode) => setVisualOptions((current) => ({ ...current, mode: mode as VisualMode }))}
            />
            <div>
              <Text type="secondary">LCD render scale</Text>
              <Slider
                min={3}
                max={6}
                step={3}
                marks={{ 3: '3x', 6: '6x' }}
                value={visualOptions.scale}
                onChange={(scaleValue) => setVisualOptions((current) => ({ ...current, scale: scaleValue }))}
              />
            </div>
            <div>
              <Text type="secondary">Grid strength</Text>
              <Slider min={0} max={45} value={visualOptions.grid} onChange={(grid) => setVisualOptions((current) => ({ ...current, grid }))} />
            </div>
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
            <Segmented
              block
              value={visualOptions.lens ? 'on' : 'off'}
              options={[
                { label: 'No Lens', value: 'off' },
                { label: 'GBC Lens', value: 'on' }
              ]}
              onChange={(lens) => setVisualOptions((current) => ({ ...current, lens: lens === 'on' }))}
            />
            <div>
              <Text type="secondary">Lens opacity</Text>
              <Slider min={20} max={100} value={visualOptions.lensOpacity} onChange={(lensOpacity) => setVisualOptions((current) => ({ ...current, lensOpacity }))} />
            </div>
          </Space>
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
