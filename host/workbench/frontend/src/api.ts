export type WorkbenchStatus = {
  ok: boolean;
  running: boolean;
  source_state: string;
  source_wait_ms: number;
  server_frame_count: number;
  server_last_capture_ms: number;
  server_capture_fps: number;
  server_frame_age_ms: number | null;
  consecutive_errors: number;
  error: string;
};

export type TargetProfile = {
  schema_version?: string;
  profile_id?: string;
  display_name?: string;
  status?: string;
  target_type?: string;
  safety?: {
    default_gpio_mode?: string;
    esp32p4_gpio_5v_tolerant?: boolean;
    dangerous_rails?: string[];
    do_not_connect_to_gpio?: string[];
    known_concerns?: string[];
  };
  role_map?: unknown;
  current_capture_profile?: {
    name?: string;
    status?: string;
    capture_peripheral?: string;
    data_mode?: string;
    lcdcam_programmed_size?: Record<string, unknown>;
    decoded_stream_model?: Record<string, unknown>;
    clocking?: Record<string, unknown>;
    transport?: Record<string, unknown>;
  };
  signals?: {
    timing_or_control?: Array<Record<string, unknown>>;
    pixel_bus?: Record<string, unknown>;
    analog_or_power?: Array<Record<string, unknown>>;
  };
};

export type DestinationProfile = TargetProfile & {
  connector?: {
    pin_count?: number;
    pin_numbering_basis?: string;
    pins?: Array<{
      name?: string;
      role?: string;
      esp32p4_gpio?: number | null;
      notes?: string;
    }>;
  };
  destination?: {
    interface?: string;
    driver_family?: string;
    controller_ic?: string | null;
    native_resolution?: { width?: number; height?: number } | null;
    orientation?: {
      swap_xy?: boolean;
      mirror_x?: boolean;
      mirror_y?: boolean;
    };
    color?: {
      input_format?: string;
      color_order?: string;
      invert_color?: boolean;
    };
    spi?: {
      host?: string;
      pclk_hz_initial?: number;
      pclk_hz_sweep?: number[];
      mode?: number;
      cmd_bits?: number;
      param_bits?: number;
      max_transfer_lines_initial?: number;
    };
    lab_commands?: string[];
    boot_policy?: string;
  };
  unknowns?: string[];
};

export type ArtifactItem = {
  name: string;
  path: string;
  modified_utc: string;
  manifest: string;
  files: string[];
  file_count: number;
};

export type PinRow = {
  signal: string;
  role: string;
  bus_pin: number | string | null;
  gpio: number;
};

export type FrameResponse = {
  bytes: Uint8Array;
  metadata: Record<string, unknown>;
};

export type DestinationGpioResponse = {
  ok: boolean;
  command?: string;
  signal?: string;
  gpio?: number;
  level?: number;
  error?: string;
  owner?: string;
  claims?: Array<{ signal: string; gpio: number; level: number }>;
};

async function getJson<T>(path: string): Promise<T> {
  const response = await fetch(path, { cache: 'no-store' });
  const data = await response.json();
  if (!response.ok) {
    throw new Error(data?.error || `HTTP ${response.status}`);
  }
  return data as T;
}

async function postJson<T>(path: string, payload: unknown): Promise<T> {
  const response = await fetch(path, {
    method: 'POST',
    cache: 'no-store',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(payload)
  });
  const data = await response.json();
  if (!response.ok) {
    throw new Error(data?.error || `HTTP ${response.status}`);
  }
  return data as T;
}

export const api = {
  status: () => getJson<WorkbenchStatus>('/api/status'),
  profile: () => getJson<TargetProfile>('/api/profile'),
  destinationProfile: () => getJson<DestinationProfile>('/api/destination-profile'),
  saveDestinationProfile: (payload: unknown) => postJson<{ ok: boolean; profile: DestinationProfile; path: string }>('/api/destination-profile', payload),
  artifacts: () => getJson<{ ok: boolean; root: string; items: ArtifactItem[] }>('/api/artifacts/recent'),
  gpios: () => getJson<{ ok: boolean; profile_id: string; gpios: PinRow[] }>('/api/workbench/gpios'),
  destinationGpioStatus: () => getJson<DestinationGpioResponse>('/api/destination/gpio/status'),
  destinationGpioValidate: (signal: string, gpio: number) => getJson<DestinationGpioResponse>(`/api/destination/gpio/validate?signal=${encodeURIComponent(signal)}&gpio=${gpio}`),
  destinationGpioClaim: (signal: string, gpio: number) => getJson<DestinationGpioResponse>(`/api/destination/gpio/claim?signal=${encodeURIComponent(signal)}&gpio=${gpio}`),
  destinationGpioSet: (signal: string, level: number) => getJson<DestinationGpioResponse>(`/api/destination/gpio/set?signal=${encodeURIComponent(signal)}&level=${level}`),
  destinationGpioPulse: (signal: string, level: number, durationMs: number) => getJson<DestinationGpioResponse>(`/api/destination/gpio/pulse?signal=${encodeURIComponent(signal)}&level=${level}&duration_ms=${durationMs}`),
  destinationGpioRelease: (signal: string) => getJson<DestinationGpioResponse>(`/api/destination/gpio/release?signal=${encodeURIComponent(signal)}`),
  destinationSpiStatus: () => getJson<Record<string, unknown>>('/api/destination/spi/status'),
  destinationSpiInit: () => getJson<Record<string, unknown>>('/api/destination/spi/init'),
  destinationSpiSafeOff: () => getJson<Record<string, unknown>>('/api/destination/spi/safe-off'),
  destinationSpiTestPattern: (pattern = 'orientation') => getJson<Record<string, unknown>>(`/api/destination/spi/test-pattern?pattern=${encodeURIComponent(pattern)}`),
  destinationSpiTestPattern565: () => getJson<Record<string, unknown>>('/api/destination/spi/test-pattern565'),
  destinationSpiShowGbcFrame: () => getJson<Record<string, unknown>>('/api/destination/spi/show-gbc-frame?timeout_ms=300'),
  destinationSpiSignalBurst: (durationMs = 5000) => getJson<Record<string, unknown>>(`/api/destination/spi/signal-burst?duration_ms=${durationMs}`),
  destinationSpiClear: (color = '0000') => getJson<Record<string, unknown>>(`/api/destination/spi/clear?color=${encodeURIComponent(color)}`),
  readGpios: () => getJson<{ ok: boolean; results: Array<Record<string, unknown>> }>('/api/workbench/read-gpios'),
  start: () => getJson<WorkbenchStatus>('/api/start'),
  stop: () => getJson<WorkbenchStatus>('/api/stop'),
  recover: () => getJson<Record<string, unknown>>('/api/recover'),
  safeIdle: () => getJson<Record<string, unknown>>('/api/safe-idle'),
  frame: async (): Promise<FrameResponse> => {
    const response = await fetch('/api/frame.bin', { cache: 'no-store' });
    const metadata = JSON.parse(response.headers.get('X-Capture-Meta') || '{}') as Record<string, unknown>;
    if (!response.ok) {
      const text = await response.text();
      throw new Error(text || String(metadata.error || `HTTP ${response.status}`));
    }
    return {
      bytes: new Uint8Array(await response.arrayBuffer()),
      metadata
    };
  }
};
