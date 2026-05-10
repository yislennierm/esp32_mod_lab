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

async function getJson<T>(path: string): Promise<T> {
  const response = await fetch(path, { cache: 'no-store' });
  const data = await response.json();
  if (!response.ok) {
    throw new Error(data?.error || `HTTP ${response.status}`);
  }
  return data as T;
}

export const api = {
  status: () => getJson<WorkbenchStatus>('/api/status'),
  profile: () => getJson<TargetProfile>('/api/profile'),
  artifacts: () => getJson<{ ok: boolean; root: string; items: ArtifactItem[] }>('/api/artifacts/recent'),
  gpios: () => getJson<{ ok: boolean; profile_id: string; gpios: PinRow[] }>('/api/workbench/gpios'),
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
