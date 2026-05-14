export type WorkbenchStatus = {
  ok: boolean;
  device_connected?: boolean;
  device_error?: string;
  serial_port?: string;
  serial_owner?: string;
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

export type LabBlock = {
  id: string;
  name: string;
  kind: 'source' | 'processing' | 'destination' | 'transport';
  status: string;
  profile?: string;
  firmware_module?: string;
  evidence?: string[];
};

export type ProjectBuildProfile = {
  id?: string;
  name?: string;
  role?: string;
  description?: string;
  build_script?: string | null;
  flash_script?: string | null;
  default_env?: Record<string, string>;
  known_good_command?: string | null;
};

export type LabProject = {
  id: string;
  name: string;
  status: string;
  description?: string;
  source?: { block: string; profile?: string };
  processing?: Array<{ block: string; profile?: string }>;
  destination?: { block: string; profile?: string };
  mcu_blocks?: string[];
  production?: {
    build_script?: string | null;
    flash_script?: string | null;
    default_env?: Record<string, string>;
    known_good_command?: string | null;
  };
  build_profiles?: Record<string, ProjectBuildProfile>;
  graph?: {
    nodes?: Array<Record<string, unknown>>;
    edges?: Array<Record<string, unknown>>;
  };
  sdk_example?: Record<string, unknown>;
};

export type ProjectValidation = {
  ok: boolean;
  project_id: string;
  errors: string[];
  warnings: string[];
  source_gpios: Array<{ signal: string; gpio: number }>;
  destination_gpios: Array<{ signal: string; gpio: number }>;
};

export type ProjectActionResult = {
  ok: boolean;
  project_id: string;
  action: string;
  build_profile?: string;
  command?: string[];
  returncode?: number;
  stdout?: string;
  stderr?: string;
  error?: string;
};

export type SerialOwnershipResult = {
  ok: boolean;
  port?: string;
  state?: string;
  serial_owner?: string;
  error?: string;
  status?: WorkbenchStatus;
};

export type FlashManifestImage = {
  address: number;
  relative_path: string;
  size: number;
  url: string;
};

export type FlashManifest = {
  ok: boolean;
  project_id: string;
  project_name?: string;
  build_profile: string;
  chip: string;
  generated_at: string;
  build_dir: string;
  before: string;
  after: string;
  flash_mode: string;
  flash_freq: string;
  flash_size: string;
  reset_strategy: string;
  images: FlashManifestImage[];
};

export type ProjectMutationResult = {
  ok: boolean;
  project?: LabProject;
  projects: LabProject[];
  id?: string;
  path?: string;
  error?: string;
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

export type SdkInventorySummary = {
  ok: boolean;
  schema: string;
  generated_at: string;
  source: {
    idf_path?: string;
    target?: string;
    version?: Record<string, string | null>;
  };
  summary: {
    component_count?: number;
    example_count?: number;
    examples_by_category?: Record<string, number>;
    examples_by_relevance?: Record<string, number>;
    example_api_group_hits?: Record<string, number>;
    example_mcu_block_hits?: Record<string, number>;
    top_import_candidates?: Array<Record<string, unknown>>;
  };
  classification?: Record<string, unknown>;
};

export type SdkExample = {
  id: string;
  name: string;
  path: string;
  category_path: string;
  relevance: string;
  categories: string[];
  api_groups: string[];
  components: string[];
  mcu_blocks: string[];
  source_file_count: number;
  sdkconfig_defaults: string[];
  import_status: string;
};

export type EspressifRepo = {
  name: string;
  full_name: string;
  html_url: string;
  description: string;
  language: string;
  topics: string[];
  stars: number;
  forks: number;
  archived: boolean;
  categories: string[];
  relevance: string;
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

function queryString(params: Record<string, string | number | undefined>) {
  const query = new URLSearchParams();
  for (const [key, value] of Object.entries(params)) {
    if (value !== undefined && value !== '') {
      query.set(key, String(value));
    }
  }
  const encoded = query.toString();
  return encoded ? `?${encoded}` : '';
}

export const api = {
  status: () => getJson<WorkbenchStatus>('/api/status'),
  blocks: () => getJson<{ ok: boolean; blocks: LabBlock[] }>('/api/blocks'),
  projects: () => getJson<{ ok: boolean; projects: LabProject[] }>('/api/projects'),
  createProject: (payload: LabProject) => postJson<ProjectMutationResult>('/api/projects/create', payload),
  saveProject: (payload: LabProject) => postJson<ProjectMutationResult>('/api/projects/save', payload),
  duplicateProject: (sourceId: string, id: string, name: string) => postJson<ProjectMutationResult>('/api/projects/duplicate', { source_id: sourceId, id, name }),
  deleteProject: (id: string, confirm?: string) => postJson<ProjectMutationResult>('/api/projects/delete', { id, confirm }),
  importIdfExample: (id: string) => postJson<ProjectMutationResult>('/api/projects/import-idf-example', { id }),
  validateProject: (projectId: string) => getJson<ProjectValidation>(`/api/projects/validate?id=${encodeURIComponent(projectId)}`),
  buildProject: (projectId: string, profile = 'production') => postJson<ProjectActionResult>('/api/projects/build', { id: projectId, profile }),
  flashProject: (projectId: string, profile = 'production') => postJson<ProjectActionResult>('/api/projects/flash', { id: projectId, profile }),
  flashManifest: (projectId: string, profile = 'production') => getJson<FlashManifest>(`/api/projects/flash-manifest?id=${encodeURIComponent(projectId)}&profile=${encodeURIComponent(profile)}`),
  releaseSerial: () => getJson<SerialOwnershipResult>('/api/serial/release'),
  reconnectSerial: () => getJson<SerialOwnershipResult>('/api/serial/reconnect'),
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
  sdkIdf: () => getJson<SdkInventorySummary>('/api/sdk/idf'),
  sdkExamples: (params: Record<string, string | number | undefined> = {}) => getJson<{ ok: boolean; count: number; examples: SdkExample[] }>(`/api/sdk/examples${queryString(params)}`),
  sdkExample: (id: string) => getJson<{ ok: boolean; example: SdkExample & Record<string, unknown> }>(`/api/sdk/examples?id=${encodeURIComponent(id)}`),
  espressifRepos: (params: Record<string, string | number | undefined> = {}) => getJson<{ ok: boolean; generated_at: string; summary: Record<string, unknown>; count: number; repositories: EspressifRepo[] }>(`/api/research/espressif/repos${queryString(params)}`),
  destinationSpiStatus: () => getJson<Record<string, unknown>>('/api/destination/spi/status'),
  destinationSpiInit: () => getJson<Record<string, unknown>>('/api/destination/spi/init'),
  destinationSpiSafeOff: () => getJson<Record<string, unknown>>('/api/destination/spi/safe-off'),
  destinationSpiTestPattern: (pattern = 'orientation') => getJson<Record<string, unknown>>(`/api/destination/spi/test-pattern?pattern=${encodeURIComponent(pattern)}`),
  destinationSpiTestPattern565: () => getJson<Record<string, unknown>>('/api/destination/spi/test-pattern565'),
  destinationSpiShowSourceFrame: () => getJson<Record<string, unknown>>('/api/destination/spi/show-gbc-frame?timeout_ms=300'),
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
