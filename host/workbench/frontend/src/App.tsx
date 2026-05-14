import {
  Alert,
  Badge,
  Button,
  Card,
  Descriptions,
  Empty,
  Form,
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
  PlayCircleOutlined,
  PlusOutlined,
  ProfileOutlined,
  ReloadOutlined,
  ThunderboltOutlined
} from '@ant-design/icons';
import { Application, Filter, GlProgram, Sprite, Texture } from 'pixi.js';
import { useEffect, useMemo, useRef, useState } from 'react';
import { api, ArtifactItem, DestinationProfile, PinRow, TargetProfile, WorkbenchStatus } from './api';

const { Header, Sider, Content } = Layout;
const { Text, Title } = Typography;

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
      label: owner ? `${option.label} - source ${owner}` : option.label
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
        <Card title="SPI LCD Destination">
          <Space direction="vertical" size="middle" className="fullWidth">
            <Alert
              type="warning"
              showIcon
              message="Destination outputs are disabled by default"
              description="This UI is a lab planning surface. Firmware commands for SPI LCD output are not active yet, and no ESP32-P4 GPIO should drive the panel until the destination module is explicitly implemented and initialized."
            />
            <Steps
              size="small"
              current={0}
              items={[
                { title: 'Identify', description: 'Controller, resolution, logic voltage' },
                { title: 'Map Pins', description: 'CS, RESET, D/C, SDI, SCK' },
                { title: 'Pattern', description: 'Standalone test output' },
                { title: 'Frame', description: 'Show last RGB565 source frame' },
                { title: 'Mirror', description: 'Optional lab mirror mode' }
              ]}
            />
            <Descriptions bordered size="small" column={2}>
              <Descriptions.Item label="Profile">{destinationProfile?.profile_id || 'not loaded'}</Descriptions.Item>
              <Descriptions.Item label="Status">{destinationProfile?.status || '?'}</Descriptions.Item>
              <Descriptions.Item label="Interface">{destination.interface || '?'}</Descriptions.Item>
              <Descriptions.Item label="Driver">{destination.driver_family || '?'}</Descriptions.Item>
              <Descriptions.Item label="Boot policy" span={2}>{destination.boot_policy || '?'}</Descriptions.Item>
            </Descriptions>
          </Space>
        </Card>

        <Card
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
                  if (sourceOwner) return <Tooltip title={`Owned by source ${sourceOwner}`}><Tag color="red">source</Tag></Tooltip>;
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

        <Card title="Open Unknowns">
          <Space wrap>
            {unknowns.length > 0 ? unknowns.map((unknown) => <Tag key={unknown}>{unknown}</Tag>) : <Empty image={Empty.PRESENTED_IMAGE_SIMPLE} description="No unknowns listed" />}
          </Space>
        </Card>
      </Space>

      <Space direction="vertical" size="middle" className="fullWidth">
        <Card title="Panel Parameters">
          <Form layout="vertical">
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
            <Button block type="primary" loading={destinationBusy === 'save-profile'} onClick={savePinDraft}>Save Destination Profile</Button>
          </Form>
        </Card>

        <Card title="Lab Actions">
          <Space direction="vertical" className="fullWidth">
            <Button block loading={destinationBusy === 'spi-status'} onClick={() => runDestinationSpiAction('spi-status', api.destinationSpiStatus)}>DEST_SPI_LCD_STATUS</Button>
            <Button block type="primary" loading={destinationBusy === 'spi-init'} onClick={() => runDestinationSpiAction('spi-init', api.destinationSpiInit)}>DEST_SPI_LCD_INIT</Button>
            <Button block loading={destinationBusy === 'spi-pattern-orientation'} onClick={() => runDestinationSpiAction('spi-pattern-orientation', () => api.destinationSpiTestPattern('orientation'))}>PATTERN orientation</Button>
            <Button block loading={destinationBusy === 'spi-pattern-bars'} onClick={() => runDestinationSpiAction('spi-pattern-bars', () => api.destinationSpiTestPattern('color_bars'))}>PATTERN color_bars</Button>
            <Button block loading={destinationBusy === 'spi-pattern565'} onClick={() => runDestinationSpiAction('spi-pattern565', api.destinationSpiTestPattern565)}>PATTERN RGB565 bars</Button>
            <Button block loading={destinationBusy === 'spi-burst'} onClick={() => runDestinationSpiAction('spi-burst', () => api.destinationSpiSignalBurst(5000))}>SIGNAL_BURST 5s</Button>
            <Button block loading={destinationBusy === 'spi-clear'} onClick={() => runDestinationSpiAction('spi-clear', () => api.destinationSpiClear('0000'))}>CLEAR black</Button>
            <Button block loading={destinationBusy === 'spi-show-gbc'} onClick={() => runDestinationSpiAction('spi-show-gbc', api.destinationSpiShowGbcFrame)}>SHOW_GBC_FRAME once</Button>
            <Button block danger loading={destinationBusy === 'spi-safe-off'} onClick={() => runDestinationSpiAction('spi-safe-off', api.destinationSpiSafeOff)}>DEST_SPI_LCD_SAFE_OFF</Button>
            <Text type="secondary">SPI actions are command-gated. Nothing runs at boot.</Text>
          </Space>
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
          drawMessage(pixiRendererRef.current.sourceCanvas, message.includes('waiting') ? 'waiting for source' : 'no current live frame');
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
                { label: 'Source', value: 'clean' },
                { label: 'GBC Glass', value: 'gbc' }
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
              <Text type="secondary">LCD RGB shader</Text>
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
  const [destinationProfile, setDestinationProfile] = useState<DestinationProfile | null>(null);
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
    selected === 'destination' ? <DestinationPage destinationProfile={destinationProfile} sourcePins={pins} /> :
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
