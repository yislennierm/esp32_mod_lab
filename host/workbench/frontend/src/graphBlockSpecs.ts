export type BlockFieldType = 'text' | 'textarea' | 'number' | 'switch' | 'select';

export type BlockFieldSpec = {
  key: string;
  label: string;
  type: BlockFieldType;
  description?: string;
  defaultValue?: string | number | boolean;
  min?: number;
  max?: number;
  step?: number;
  unit?: string;
  options?: Array<{ value: string; label: string }>;
};

export type TelemetryChannelSpec = {
  key: string;
  label: string;
  description: string;
};

export type GraphBlockSpec = {
  key: string;
  title: string;
  category: string;
  summary: string;
  capabilities: string[];
  parameterFields: BlockFieldSpec[];
  telemetryChannels: TelemetryChannelSpec[];
};

export type GraphLibraryEntry = {
  id: string;
  family: 'sdf' | 'sdk' | 'resource' | 'external' | 'intent';
  lane: 'intent' | 'io' | 'sdk' | 'resource' | 'observe';
  nodeTemplate: {
    type: string;
    label: string;
    params?: Record<string, unknown>;
    ports?: Record<string, string[]>;
  };
  spec: GraphBlockSpec;
};

type GraphNodeRecord = Record<string, unknown>;

const genericTelemetryFields: BlockFieldSpec[] = [
  {
    key: 'sample_window_ms',
    label: 'Window',
    type: 'number',
    min: 50,
    max: 5000,
    step: 50,
    unit: 'ms',
    defaultValue: 250
  },
  {
    key: 'emit_mode',
    label: 'Emit mode',
    type: 'select',
    defaultValue: 'delta',
    options: [
      { value: 'delta', label: 'Delta' },
      { value: 'snapshot', label: 'Snapshot' },
      { value: 'threshold', label: 'Threshold' }
    ]
  }
];

function resourceSpec(
  key: string,
  title: string,
  summary: string,
  capabilities: string[],
  parameterFields: BlockFieldSpec[],
  telemetryChannels: TelemetryChannelSpec[]
): GraphBlockSpec {
  return {
    key,
    title,
    category: 'ESP32-P4 Resource',
    summary,
    capabilities,
    parameterFields,
    telemetryChannels
  };
}

const systemIntentSpec: GraphBlockSpec = {
  key: 'system_intent',
  title: 'System Intent',
  category: 'Project Intent',
  summary: 'Declares what the graph is trying to achieve before runtime and peripheral details are chosen.',
  capabilities: [
    'Bind an imported SDK example or lab objective to the graph',
    'Declare output goals, latency targets, and acceptance notes',
    'Carry generation guidance without modifying source code'
  ],
  parameterFields: [
    { key: 'objective', label: 'Objective', type: 'text', defaultValue: 'deliver pixels to destination' },
    { key: 'success_metric', label: 'Success metric', type: 'text', defaultValue: 'stable output' },
    { key: 'latency_budget_ms', label: 'Latency budget', type: 'number', min: 0, max: 1000, step: 1, unit: 'ms', defaultValue: 16 },
    { key: 'notes', label: 'Notes', type: 'textarea', defaultValue: '' }
  ],
  telemetryChannels: [
    { key: 'phase_transitions', label: 'Phase transitions', description: 'Track lifecycle transitions such as configured, started, degraded, and done.' },
    { key: 'goal_progress', label: 'Goal progress', description: 'Expose progress against the declared success metric.' }
  ]
};

const externalDeviceSpecs: Record<string, GraphBlockSpec> = {
  default: {
    key: 'external_device',
    title: 'External Device',
    category: 'External Device',
    summary: 'Represents a board, panel, sensor, console, or cable-side endpoint outside the ESP32-P4.',
    capabilities: [
      'Define external interface expectations',
      'Capture electrical or protocol assumptions',
      'Annotate bring-up and safety constraints'
    ],
    parameterFields: [
      { key: 'interface_kind', label: 'Interface', type: 'select', defaultValue: 'parallel', options: [
        { value: 'parallel', label: 'Parallel bus' },
        { value: 'serial', label: 'Serial bus' },
        { value: 'display', label: 'Display endpoint' },
        { value: 'sensor', label: 'Sensor/control' }
      ] },
      { key: 'voltage_domain', label: 'Voltage domain', type: 'text', defaultValue: '3v3/unknown' },
      { key: 'timing_role', label: 'Timing role', type: 'text', defaultValue: 'source or sink' },
      { key: 'notes', label: 'Notes', type: 'textarea', defaultValue: '' }
    ],
    telemetryChannels: [
      { key: 'presence', label: 'Presence', description: 'Indicate whether the external device appears connected or responsive.' },
      { key: 'signal_health', label: 'Signal health', description: 'Expose sync presence, basic electrical confidence, or control-line state.' }
    ]
  },
  lcd_panel: {
    key: 'external_lcd_panel',
    title: 'External LCD Panel',
    category: 'External Display',
    summary: 'A destination display panel driven by SPI, RGB LCD, MIPI DSI, or other display paths.',
    capabilities: [
      'Accept rendered pixel streams',
      'Expose panel orientation and timing assumptions',
      'Model reset/backlight/control dependencies'
    ],
    parameterFields: [
      { key: 'native_width', label: 'Width', type: 'number', min: 1, max: 4096, step: 1, defaultValue: 320 },
      { key: 'native_height', label: 'Height', type: 'number', min: 1, max: 4096, step: 1, defaultValue: 240 },
      { key: 'pixel_format', label: 'Pixel format', type: 'select', defaultValue: 'rgb565', options: [
        { value: 'rgb565', label: 'RGB565' },
        { value: 'rgb666', label: 'RGB666' },
        { value: 'rgb888', label: 'RGB888' }
      ] },
      { key: 'orientation', label: 'Orientation', type: 'select', defaultValue: 'landscape', options: [
        { value: 'landscape', label: 'Landscape' },
        { value: 'portrait', label: 'Portrait' }
      ] }
    ],
    telemetryChannels: [
      { key: 'present_fps', label: 'Present FPS', description: 'Track panel update cadence.' },
      { key: 'tearing_or_drops', label: 'Drops/tearing', description: 'Report dropped presents, underflow, or visual sync issues.' },
      { key: 'power_state', label: 'Power state', description: 'Observe reset, sleep, or backlight state.' }
    ]
  },
  camera_sensor: {
    key: 'external_camera_sensor',
    title: 'External Camera/Sensor',
    category: 'External Source',
    summary: 'An upstream source device that provides sampled pixels or control-plane data into the ESP32-P4.',
    capabilities: [
      'Drive external pixel or control signals',
      'Define expected sync polarity, cadence, or bus width',
      'Model sideband configuration paths such as I2C'
    ],
    parameterFields: [
      { key: 'bus_width', label: 'Bus width', type: 'number', min: 1, max: 24, step: 1, defaultValue: 8 },
      { key: 'sync_scheme', label: 'Sync scheme', type: 'select', defaultValue: 'vsync_hsync', options: [
        { value: 'vsync_hsync', label: 'VSYNC/HSYNC' },
        { value: 'de_only', label: 'DE only' },
        { value: 'custom', label: 'Custom' }
      ] },
      { key: 'control_bus', label: 'Control bus', type: 'select', defaultValue: 'i2c', options: [
        { value: 'i2c', label: 'I2C' },
        { value: 'spi', label: 'SPI' },
        { value: 'none', label: 'None' }
      ] },
      { key: 'notes', label: 'Notes', type: 'textarea', defaultValue: '' }
    ],
    telemetryChannels: [
      { key: 'frame_sync', label: 'Frame sync', description: 'Observe whether source frame sync is present and stable.' },
      { key: 'line_sync', label: 'Line sync', description: 'Observe line cadence or burst cadence.' },
      { key: 'control_ack', label: 'Control ACK', description: 'Watch basic responsiveness of the sideband control path.' }
    ]
  }
};

const sdfBlockSpecs: Record<string, GraphBlockSpec> = {
  timer_tick: {
    key: 'sdf.timer_tick',
    title: 'Timer Tick Source',
    category: 'SDF Source',
    summary: 'Emits periodic tick messages or trigger events into the graph.',
    capabilities: [
      'Generate periodic events',
      'Drive message or control cadence',
      'Prototype scheduling without hardware ingress'
    ],
    parameterFields: [
      { key: 'period_ms', label: 'Period', type: 'number', min: 1, max: 60000, step: 1, unit: 'ms', defaultValue: 500 },
      { key: 'burst_count', label: 'Burst', type: 'number', min: 1, max: 1024, step: 1, defaultValue: 1 }
    ],
    telemetryChannels: [
      { key: 'tick_rate', label: 'Tick rate', description: 'Observe actual emitted tick cadence.' },
      { key: 'jitter', label: 'Jitter', description: 'Track deviation from the configured schedule.' }
    ]
  },
  constant_pattern_source: {
    key: 'sdf.constant_pattern_source',
    title: 'Constant Pattern Source',
    category: 'SDF Source',
    summary: 'Produces a fixed pattern, level, or frame for lab-safe prototyping.',
    capabilities: [
      'Generate deterministic patterns',
      'Drive test vectors into logic or destinations',
      'Stand in for missing hardware sources'
    ],
    parameterFields: [
      { key: 'pattern_kind', label: 'Pattern', type: 'select', defaultValue: 'square', options: [
        { value: 'square', label: 'Square' },
        { value: 'saw', label: 'Saw' },
        { value: 'constant_high', label: 'Constant high' },
        { value: 'constant_low', label: 'Constant low' }
      ] },
      { key: 'value', label: 'Value', type: 'number', min: 0, max: 65535, step: 1, defaultValue: 1 }
    ],
    telemetryChannels: [
      { key: 'emit_count', label: 'Emit count', description: 'Count emitted values or frames.' },
      { key: 'pattern_state', label: 'Pattern state', description: 'Observe the current output state.' }
    ]
  },
  toggle_logic: {
    key: 'sdf.toggle_logic',
    title: 'Toggle Logic',
    category: 'SDF Transform',
    summary: 'Toggles output state each time a trigger arrives.',
    capabilities: [
      'Convert tick or trigger messages into alternating state',
      'Model stateful logic without firmware code',
      'Bridge control messages to output levels'
    ],
    parameterFields: [
      { key: 'initial_level', label: 'Initial level', type: 'select', defaultValue: 'low', options: [
        { value: 'low', label: 'Low' },
        { value: 'high', label: 'High' }
      ] },
      { key: 'toggle_on', label: 'Toggle on', type: 'select', defaultValue: 'every_event', options: [
        { value: 'every_event', label: 'Every event' },
        { value: 'rising_only', label: 'Rising only' }
      ] }
    ],
    telemetryChannels: [
      { key: 'state', label: 'State', description: 'Observe current toggled output level.' },
      { key: 'transition_count', label: 'Transitions', description: 'Count output transitions.' }
    ]
  },
  gpio_led_destination: {
    key: 'sdf.gpio_led_destination',
    title: 'GPIO LED Destination',
    category: 'SDF Destination',
    summary: 'Consumes logic level input and drives an LED output pin.',
    capabilities: [
      'Bind graph output level to GPIO',
      'Define active polarity',
      'Prototype destination actuation in a safe simple form'
    ],
    parameterFields: [
      { key: 'gpio', label: 'GPIO', type: 'number', min: 0, max: 54, step: 1, defaultValue: 2 },
      { key: 'active_level', label: 'Active level', type: 'select', defaultValue: '1', options: [
        { value: '1', label: 'High' },
        { value: '0', label: 'Low' }
      ] }
    ],
    telemetryChannels: [
      { key: 'logic_level', label: 'Logic level', description: 'Observe the commanded LED output level.' },
      { key: 'write_count', label: 'Write count', description: 'Count output updates.' }
    ]
  },
  gbc_lcd_source: {
    key: 'sdf.gbc_lcd_source',
    title: 'GBC LCD Source',
    category: 'SDF Source',
    summary: 'Represents the Game Boy Color LCD ingress path and its timing assumptions.',
    capabilities: [
      'Define GBC source geometry and sync assumptions',
      'Bind source profile and visible region behavior',
      'Drive capture-oriented downstream resources'
    ],
    parameterFields: [
      { key: 'visible_width', label: 'Visible width', type: 'number', min: 1, max: 512, step: 1, defaultValue: 160 },
      { key: 'visible_height', label: 'Visible height', type: 'number', min: 1, max: 512, step: 1, defaultValue: 144 },
      { key: 'pixel_format', label: 'Pixel format', type: 'select', defaultValue: 'rgb565', options: [
        { value: 'rgb565', label: 'RGB565' },
        { value: 'rgb666', label: 'RGB666' }
      ] }
    ],
    telemetryChannels: [
      { key: 'frame_sync', label: 'Frame sync', description: 'Observe frame-sync presence and cadence.' },
      { key: 'line_activity', label: 'Line activity', description: 'Observe line or burst cadence.' },
      { key: 'capture_rate', label: 'Capture rate', description: 'Track captured frame cadence.' }
    ]
  },
  spi_lcd_destination: {
    key: 'sdf.spi_lcd_destination',
    title: 'SPI LCD Destination',
    category: 'SDF Destination',
    summary: 'Represents an SPI-driven LCD endpoint in the graph.',
    capabilities: [
      'Bind destination controller and timing assumptions',
      'Consume frame or line updates',
      'Model display-present behavior in the graph'
    ],
    parameterFields: [
      { key: 'width', label: 'Width', type: 'number', min: 1, max: 4096, step: 1, defaultValue: 320 },
      { key: 'height', label: 'Height', type: 'number', min: 1, max: 4096, step: 1, defaultValue: 240 },
      { key: 'pclk_hz_initial', label: 'SPI clock', type: 'number', min: 100000, max: 80000000, step: 100000, unit: 'Hz', defaultValue: 20000000 }
    ],
    telemetryChannels: [
      { key: 'present_fps', label: 'Present FPS', description: 'Track destination update cadence.' },
      { key: 'present_drop', label: 'Present drops', description: 'Count skipped or failed presents.' },
      { key: 'panel_state', label: 'Panel state', description: 'Observe init/reset/ready state.' }
    ]
  }
};

const apiGroupSpecs: Record<string, GraphBlockSpec> = {
  freertos: {
    key: 'lab_function_block.freertos',
    title: 'FreeRTOS Runtime Block',
    category: 'SDK Runtime',
    summary: 'Represents the task or scheduling layer that owns application control flow.',
    capabilities: [
      'Create and schedule tasks',
      'Define stack, priority, and core affinity',
      'Coordinate producer/consumer sequencing and synchronization'
    ],
    parameterFields: [
      { key: 'enabled', label: 'Enabled', type: 'switch', defaultValue: true },
      { key: 'task_name', label: 'Task name', type: 'text', defaultValue: 'app_main' },
      { key: 'priority', label: 'Priority', type: 'number', min: 1, max: 24, step: 1, defaultValue: 5 },
      { key: 'stack_size_bytes', label: 'Stack size', type: 'number', min: 2048, max: 32768, step: 1024, unit: 'bytes', defaultValue: 4096 },
      { key: 'core_affinity', label: 'Core affinity', type: 'select', defaultValue: 'any', options: [
        { value: 'any', label: 'Any core' },
        { value: 'core0', label: 'Core 0' },
        { value: 'core1', label: 'Core 1' }
      ] },
      { key: 'notes', label: 'Notes', type: 'textarea', defaultValue: '' }
    ],
    telemetryChannels: [
      { key: 'task_state', label: 'Task state', description: 'Observe created/running/blocked/suspended state.' },
      { key: 'runtime_cpu_pct', label: 'Runtime share', description: 'Track CPU share or execution time for the task.' },
      { key: 'stack_high_watermark', label: 'Stack watermark', description: 'Monitor remaining stack margin.' },
      { key: 'wake_rate', label: 'Wake rate', description: 'Count wakeups or scheduling frequency.' }
    ]
  },
  gpio: {
    key: 'lab_function_block.gpio',
    title: 'GPIO Control Block',
    category: 'SDK Peripheral',
    summary: 'Configures and drives or samples GPIO lines for control, reset, sideband state, or basic bit signals.',
    capabilities: [
      'Drive output levels',
      'Read digital states',
      'Set pull, direction, and edge behavior'
    ],
    parameterFields: [
      { key: 'direction', label: 'Direction', type: 'select', defaultValue: 'output', options: [
        { value: 'input', label: 'Input' },
        { value: 'output', label: 'Output' },
        { value: 'bidirectional', label: 'Bidirectional' }
      ] },
      { key: 'default_level', label: 'Default level', type: 'select', defaultValue: 'low', options: [
        { value: 'low', label: 'Low' },
        { value: 'high', label: 'High' },
        { value: 'floating', label: 'Floating' }
      ] },
      { key: 'pull_mode', label: 'Pull', type: 'select', defaultValue: 'disabled', options: [
        { value: 'disabled', label: 'Disabled' },
        { value: 'up', label: 'Pull-up' },
        { value: 'down', label: 'Pull-down' }
      ] }
    ],
    telemetryChannels: [
      { key: 'logic_level', label: 'Logic level', description: 'Monitor current high/low state.' },
      { key: 'edge_count', label: 'Edge count', description: 'Count transitions on the line.' },
      { key: 'ownership', label: 'Ownership', description: 'Expose which block currently owns the line.' }
    ]
  },
  i2c: {
    key: 'lab_function_block.i2c',
    title: 'I2C Control Bus',
    category: 'SDK Peripheral',
    summary: 'Owns configuration and low-speed control traffic to external devices such as panels or sensors.',
    capabilities: [
      'Transmit and receive addressed control transactions',
      'Configure bus timing and acknowledge policy',
      'Sequence device bring-up and register writes'
    ],
    parameterFields: [
      { key: 'bus_speed_hz', label: 'Bus speed', type: 'number', min: 10000, max: 1000000, step: 10000, unit: 'Hz', defaultValue: 400000 },
      { key: 'address_width', label: 'Address width', type: 'select', defaultValue: '7bit', options: [
        { value: '7bit', label: '7-bit' },
        { value: '10bit', label: '10-bit' }
      ] },
      { key: 'timeout_ms', label: 'Timeout', type: 'number', min: 1, max: 1000, step: 1, unit: 'ms', defaultValue: 25 }
    ],
    telemetryChannels: [
      { key: 'transaction_rate', label: 'Transaction rate', description: 'Observe control transaction throughput.' },
      { key: 'nack_count', label: 'NACK count', description: 'Count missing acknowledgements.' },
      { key: 'bus_busy', label: 'Bus busy', description: 'Watch bus busy or stuck-low conditions.' }
    ]
  },
  i2s: {
    key: 'lab_function_block.i2s',
    title: 'I2S Stream Block',
    category: 'SDK Peripheral',
    summary: 'Represents synchronous serial sample transport for audio-style or framed data streams.',
    capabilities: [
      'Move sample streams over bit clock and word select',
      'Configure sample format and rate',
      'Drive or receive DMA-backed sample buffers'
    ],
    parameterFields: [
      { key: 'direction', label: 'Direction', type: 'select', defaultValue: 'rx', options: [
        { value: 'rx', label: 'RX' },
        { value: 'tx', label: 'TX' }
      ] },
      { key: 'sample_rate_hz', label: 'Sample rate', type: 'number', min: 8000, max: 192000, step: 1000, unit: 'Hz', defaultValue: 48000 },
      { key: 'bits_per_sample', label: 'Bits/sample', type: 'select', defaultValue: '16', options: [
        { value: '16', label: '16' },
        { value: '24', label: '24' },
        { value: '32', label: '32' }
      ] },
      { key: 'channels', label: 'Channels', type: 'select', defaultValue: 'stereo', options: [
        { value: 'mono', label: 'Mono' },
        { value: 'stereo', label: 'Stereo' }
      ] }
    ],
    telemetryChannels: [
      { key: 'sample_rate_actual', label: 'Actual rate', description: 'Track effective stream rate.' },
      { key: 'dma_fill', label: 'DMA fill', description: 'Watch queue or DMA buffer occupancy.' },
      { key: 'underrun_overrun', label: 'Under/overrun', description: 'Count stream starvation or overflow events.' }
    ]
  },
  spi_master: {
    key: 'lab_function_block.spi_master',
    title: 'SPI Master Bus',
    category: 'SDK Peripheral',
    summary: 'Owns host-driven SPI transactions to displays, devices, or transport endpoints.',
    capabilities: [
      'Queue SPI transactions',
      'Configure host timing and mode',
      'Move command/data streams through DMA-backed transfers'
    ],
    parameterFields: [
      { key: 'clock_hz', label: 'Clock', type: 'number', min: 100000, max: 80000000, step: 100000, unit: 'Hz', defaultValue: 20000000 },
      { key: 'mode', label: 'SPI mode', type: 'select', defaultValue: '0', options: [
        { value: '0', label: 'Mode 0' },
        { value: '1', label: 'Mode 1' },
        { value: '2', label: 'Mode 2' },
        { value: '3', label: 'Mode 3' }
      ] },
      { key: 'queue_depth', label: 'Queue depth', type: 'number', min: 1, max: 32, step: 1, defaultValue: 4 }
    ],
    telemetryChannels: [
      { key: 'transfer_rate', label: 'Transfer rate', description: 'Observe effective byte or pixel throughput.' },
      { key: 'queue_fill', label: 'Queue fill', description: 'Monitor queued transaction depth.' },
      { key: 'transfer_errors', label: 'Transfer errors', description: 'Count failed or timed-out transfers.' }
    ]
  },
  esp_lcd: {
    key: 'lab_function_block.esp_lcd',
    title: 'LCD Panel Driver',
    category: 'SDK Display',
    summary: 'Represents panel driver logic layered over SPI, RGB LCD, or DSI endpoints.',
    capabilities: [
      'Initialize panel command sequences',
      'Manage orientation, color mode, and flush boundaries',
      'Present pixel buffers to a display endpoint'
    ],
    parameterFields: [
      { key: 'pixel_format', label: 'Pixel format', type: 'select', defaultValue: 'rgb565', options: [
        { value: 'rgb565', label: 'RGB565' },
        { value: 'rgb666', label: 'RGB666' },
        { value: 'rgb888', label: 'RGB888' }
      ] },
      { key: 'swap_xy', label: 'Swap XY', type: 'switch', defaultValue: false },
      { key: 'mirror_x', label: 'Mirror X', type: 'switch', defaultValue: false },
      { key: 'mirror_y', label: 'Mirror Y', type: 'switch', defaultValue: false }
    ],
    telemetryChannels: [
      { key: 'present_fps', label: 'Present FPS', description: 'Track display present cadence.' },
      { key: 'flush_latency', label: 'Flush latency', description: 'Measure panel flush time.' },
      { key: 'drop_count', label: 'Drop count', description: 'Count skipped or failed presents.' }
    ]
  },
  camera: {
    key: 'lab_function_block.camera',
    title: 'Camera Source Block',
    category: 'SDK Capture',
    summary: 'Owns upstream capture configuration and transfer initiation for DVP or related source ingress.',
    capabilities: [
      'Configure camera/bus sampling',
      'Receive source frames or lines',
      'Coordinate sync and format expectations with downstream blocks'
    ],
    parameterFields: [
      { key: 'frame_width', label: 'Frame width', type: 'number', min: 1, max: 4096, step: 1, defaultValue: 160 },
      { key: 'frame_height', label: 'Frame height', type: 'number', min: 1, max: 4096, step: 1, defaultValue: 144 },
      { key: 'pixel_format', label: 'Pixel format', type: 'select', defaultValue: 'rgb565', options: [
        { value: 'rgb565', label: 'RGB565' },
        { value: 'rgb666', label: 'RGB666' },
        { value: 'raw8', label: 'RAW8' }
      ] },
      { key: 'sync_mode', label: 'Sync mode', type: 'select', defaultValue: 'external', options: [
        { value: 'external', label: 'External sync' },
        { value: 'de', label: 'DE gated' },
        { value: 'line_marker', label: 'Line marker' }
      ] }
    ],
    telemetryChannels: [
      { key: 'capture_fps', label: 'Capture FPS', description: 'Track source ingress cadence.' },
      { key: 'sync_loss', label: 'Sync loss', description: 'Count missing frame or line sync conditions.' },
      { key: 'drop_count', label: 'Drop count', description: 'Count capture drops or incomplete frames.' }
    ]
  },
  isp: {
    key: 'lab_function_block.isp',
    title: 'Image Signal Processor',
    category: 'SDK Image',
    summary: 'Transforms or reformats captured image data before it moves to memory or display.',
    capabilities: [
      'Perform pixel pipeline transforms',
      'Bridge camera-formatted data toward downstream consumers',
      'Own image-format assumptions inside the graph'
    ],
    parameterFields: [
      { key: 'input_format', label: 'Input', type: 'select', defaultValue: 'raw8', options: [
        { value: 'raw8', label: 'RAW8' },
        { value: 'rgb565', label: 'RGB565' },
        { value: 'rgb666', label: 'RGB666' }
      ] },
      { key: 'output_format', label: 'Output', type: 'select', defaultValue: 'rgb565', options: [
        { value: 'rgb565', label: 'RGB565' },
        { value: 'rgb666', label: 'RGB666' },
        { value: 'yuv422', label: 'YUV422' }
      ] },
      { key: 'crop_enable', label: 'Crop', type: 'switch', defaultValue: false }
    ],
    telemetryChannels: [
      { key: 'process_latency', label: 'Process latency', description: 'Measure ISP stage time.' },
      { key: 'output_rate', label: 'Output rate', description: 'Track output frame cadence.' },
      { key: 'processing_faults', label: 'Faults', description: 'Count format or processing errors.' }
    ]
  },
  ledc: {
    key: 'lab_function_block.ledc',
    title: 'LEDC/PWM Control',
    category: 'SDK Peripheral',
    summary: 'Controls timing-stable PWM outputs such as backlights, clocks, or drive enables.',
    capabilities: [
      'Generate PWM waveforms',
      'Control duty and frequency',
      'Drive backlight or auxiliary timing signals'
    ],
    parameterFields: [
      { key: 'frequency_hz', label: 'Frequency', type: 'number', min: 1, max: 1000000, step: 100, unit: 'Hz', defaultValue: 1000 },
      { key: 'duty_pct', label: 'Duty', type: 'number', min: 0, max: 100, step: 1, unit: '%', defaultValue: 50 },
      { key: 'channel_mode', label: 'Mode', type: 'select', defaultValue: 'high_speed', options: [
        { value: 'high_speed', label: 'High speed' },
        { value: 'low_speed', label: 'Low speed' }
      ] }
    ],
    telemetryChannels: [
      { key: 'duty_actual', label: 'Duty actual', description: 'Observe actual configured duty.' },
      { key: 'frequency_actual', label: 'Frequency actual', description: 'Observe actual output frequency.' },
      { key: 'enabled_state', label: 'Enabled state', description: 'Show whether the PWM output is enabled.' }
    ]
  },
  jpeg: {
    key: 'lab_function_block.jpeg',
    title: 'JPEG/Image Block',
    category: 'SDK Image',
    summary: 'Represents image decode or encode stages that require memory and transfer coordination.',
    capabilities: [
      'Decode or encode image payloads',
      'Bridge compressed assets to displayable buffers',
      'Allocate working memory and staging buffers'
    ],
    parameterFields: [
      { key: 'operation', label: 'Operation', type: 'select', defaultValue: 'decode', options: [
        { value: 'decode', label: 'Decode' },
        { value: 'encode', label: 'Encode' }
      ] },
      { key: 'quality', label: 'Quality', type: 'number', min: 1, max: 100, step: 1, defaultValue: 75 },
      { key: 'use_psram', label: 'Use PSRAM', type: 'switch', defaultValue: true }
    ],
    telemetryChannels: [
      { key: 'throughput', label: 'Throughput', description: 'Track payload decode or encode rate.' },
      { key: 'buffer_usage', label: 'Buffer usage', description: 'Monitor staging buffer usage.' },
      { key: 'error_count', label: 'Error count', description: 'Count decode or encode faults.' }
    ]
  },
  psram: {
    key: 'lab_function_block.psram',
    title: 'PSRAM Buffering Block',
    category: 'SDK Memory',
    summary: 'Represents higher-capacity memory use for frame buffers, staging, or caches.',
    capabilities: [
      'Allocate large external buffers',
      'Trade latency for capacity',
      'Provide frame storage outside internal DMA-capable memory'
    ],
    parameterFields: [
      { key: 'buffer_budget_kb', label: 'Buffer budget', type: 'number', min: 4, max: 16384, step: 4, unit: 'KB', defaultValue: 512 },
      { key: 'cache_policy', label: 'Policy', type: 'select', defaultValue: 'mixed', options: [
        { value: 'mixed', label: 'Mixed' },
        { value: 'mostly_read', label: 'Mostly read' },
        { value: 'mostly_write', label: 'Mostly write' }
      ] }
    ],
    telemetryChannels: [
      { key: 'allocation_bytes', label: 'Allocated bytes', description: 'Track how much PSRAM is reserved.' },
      { key: 'high_watermark', label: 'High watermark', description: 'Track peak memory use.' },
      { key: 'contention_events', label: 'Contention', description: 'Count observable contention or fallback events.' }
    ]
  },
  default: {
    key: 'lab_function_block.default',
    title: 'SDK Function Block',
    category: 'SDK Runtime',
    summary: 'A function block inferred from SDK example metadata.',
    capabilities: [
      'Represent a reusable software stage in the graph',
      'Expose editable overlay parameters',
      'Declare telemetry selection without changing imported source'
    ],
    parameterFields: [
      { key: 'instance_name', label: 'Instance name', type: 'text', defaultValue: 'block' },
      { key: 'enabled', label: 'Enabled', type: 'switch', defaultValue: true },
      { key: 'notes', label: 'Notes', type: 'textarea', defaultValue: '' }
    ],
    telemetryChannels: [
      { key: 'state', label: 'State', description: 'Observe coarse block state.' },
      { key: 'activity_rate', label: 'Activity rate', description: 'Track how often the block runs or emits work.' }
    ]
  }
};

const esp32ResourceSpecs: Record<string, GraphBlockSpec> = {
  'free rtos': resourceSpec('resource.freertos', 'FreeRTOS', 'Scheduler/runtime resource used by software blocks and tasks.', ['Task scheduling', 'Timing ownership', 'Inter-task coordination'], [
    { key: 'reserve_core_affinity', label: 'Preferred core', type: 'select', defaultValue: 'any', options: [{ value: 'any', label: 'Any' }, { value: 'core0', label: 'Core 0' }, { value: 'core1', label: 'Core 1' }] },
    { key: 'watchdog_budget_ms', label: 'Budget', type: 'number', min: 1, max: 1000, step: 1, unit: 'ms', defaultValue: 50 },
    ...genericTelemetryFields
  ], [
    { key: 'task_count', label: 'Task count', description: 'Observe active task count.' },
    { key: 'runtime_split', label: 'Runtime split', description: 'Track runtime per task or core.' },
    { key: 'watchdog_events', label: 'Watchdog', description: 'Count task stalls or watchdog hits.' }
  ]),
  'gdma': resourceSpec('resource.gdma', 'GDMA', 'General DMA transport for frame and buffer movement.', ['Descriptor-driven transport', 'Peripheral-to-memory transfer', 'Queue ownership'], [
    { key: 'channel_direction', label: 'Direction', type: 'select', defaultValue: 'rx', options: [{ value: 'rx', label: 'RX' }, { value: 'tx', label: 'TX' }, { value: 'bidirectional', label: 'Both' }] },
    { key: 'descriptor_budget', label: 'Descriptors', type: 'number', min: 1, max: 64, step: 1, defaultValue: 8 },
    ...genericTelemetryFields
  ], [
    { key: 'descriptor_fill', label: 'Descriptor fill', description: 'Observe descriptor queue occupancy.' },
    { key: 'throughput', label: 'Throughput', description: 'Track transfer bandwidth.' },
    { key: 'overrun_or_stall', label: 'Stall/overrun', description: 'Count stall, timeout, or overrun events.' }
  ]),
  'gpio matrix / io mux': resourceSpec('resource.gpio_matrix', 'GPIO Matrix / IO MUX', 'Pin routing fabric binding internal signals to physical GPIOs.', ['Route peripheral signals', 'Control mux ownership', 'Enforce pin claims'], [
    { key: 'ownership_policy', label: 'Ownership', type: 'select', defaultValue: 'strict', options: [{ value: 'strict', label: 'Strict' }, { value: 'shared_lab', label: 'Shared lab' }] },
    { key: 'default_safe_state', label: 'Safe state', type: 'select', defaultValue: 'input_float', options: [{ value: 'input_float', label: 'Input float' }, { value: 'input_pullup', label: 'Input pull-up' }, { value: 'disabled', label: 'Disabled' }] },
    ...genericTelemetryFields
  ], [
    { key: 'pin_claims', label: 'Pin claims', description: 'Track active pin ownership.' },
    { key: 'conflict_count', label: 'Conflict count', description: 'Count pin claim conflicts.' },
    { key: 'line_state', label: 'Line state', description: 'Observe selected routed lines.' }
  ]),
  'internal dma ram': resourceSpec('resource.internal_dma_ram', 'Internal DMA RAM', 'Low-latency DMA-capable memory used for deterministic transfer buffers.', ['DMA-safe slots', 'Low-latency staging', 'Hot-path isolation'], [
    { key: 'slot_count', label: 'Slot count', type: 'number', min: 1, max: 32, step: 1, defaultValue: 4 },
    { key: 'slot_size_bytes', label: 'Slot size', type: 'number', min: 256, max: 1048576, step: 256, unit: 'bytes', defaultValue: 65536 },
    ...genericTelemetryFields
  ], [
    { key: 'slot_fill', label: 'Slot fill', description: 'Observe frame-slot occupancy.' },
    { key: 'allocation_usage', label: 'Usage', description: 'Track reserved internal DMA memory.' },
    { key: 'allocation_failures', label: 'Alloc failures', description: 'Count failed DMA-safe allocations.' }
  ]),
  'psram': resourceSpec('resource.psram', 'PSRAM', 'External RAM for high-capacity storage and staging.', ['Large frame buffers', 'Capacity over latency', 'Background buffer storage'], [
    { key: 'budget_kb', label: 'Budget', type: 'number', min: 4, max: 16384, step: 4, unit: 'KB', defaultValue: 1024 },
    { key: 'traffic_class', label: 'Traffic', type: 'select', defaultValue: 'bulk', options: [{ value: 'bulk', label: 'Bulk' }, { value: 'mixed', label: 'Mixed' }, { value: 'read_heavy', label: 'Read heavy' }] },
    ...genericTelemetryFields
  ], [
    { key: 'allocated_bytes', label: 'Allocated', description: 'Observe reserved external RAM.' },
    { key: 'high_watermark', label: 'High watermark', description: 'Track peak external RAM use.' },
    { key: 'contention', label: 'Contention', description: 'Count contention-sensitive events.' }
  ]),
  'spi': resourceSpec('resource.spi', 'SPI', 'Serial master/slave transport used for display and peripheral traffic.', ['Clocked serial transfers', 'DMA-backed bursts', 'Command/data framing'], [
    { key: 'clock_hz', label: 'Clock', type: 'number', min: 100000, max: 80000000, step: 100000, unit: 'Hz', defaultValue: 20000000 },
    { key: 'mode', label: 'Mode', type: 'select', defaultValue: '0', options: [{ value: '0', label: 'Mode 0' }, { value: '1', label: 'Mode 1' }, { value: '2', label: 'Mode 2' }, { value: '3', label: 'Mode 3' }] },
    ...genericTelemetryFields
  ], [
    { key: 'transfer_rate', label: 'Transfer rate', description: 'Observe active SPI throughput.' },
    { key: 'queue_depth', label: 'Queue depth', description: 'Watch queued transfer depth.' },
    { key: 'transfer_faults', label: 'Faults', description: 'Count failed transfers or bus faults.' }
  ]),
  'lcd_cam': resourceSpec('resource.lcd_cam', 'LCD_CAM', 'Parallel ingress/egress peripheral for display buses and camera-style capture.', ['Parallel capture or drive', 'Clocked pixel transport', 'DMA integration'], [
    { key: 'mode', label: 'Mode', type: 'select', defaultValue: 'capture', options: [{ value: 'capture', label: 'Capture' }, { value: 'drive', label: 'Drive' }] },
    { key: 'bus_width', label: 'Bus width', type: 'number', min: 1, max: 24, step: 1, defaultValue: 8 },
    { key: 'frame_timeout_ms', label: 'Timeout', type: 'number', min: 1, max: 1000, step: 1, unit: 'ms', defaultValue: 300 },
    ...genericTelemetryFields
  ], [
    { key: 'capture_fps', label: 'Capture FPS', description: 'Observe effective capture or present cadence.' },
    { key: 'sync_loss', label: 'Sync loss', description: 'Count missing or unstable sync events.' },
    { key: 'short_frame', label: 'Short frame', description: 'Count incomplete frame transfers.' }
  ]),
  'isp': resourceSpec('resource.isp', 'ISP', 'Image signal processing hardware stage.', ['Image pipeline transforms', 'Format conversion', 'Post-capture processing'], [
    { key: 'pipeline_mode', label: 'Pipeline mode', type: 'select', defaultValue: 'pass_through', options: [{ value: 'pass_through', label: 'Pass-through' }, { value: 'convert', label: 'Convert' }, { value: 'process', label: 'Process' }] },
    ...genericTelemetryFields
  ], [
    { key: 'latency', label: 'Latency', description: 'Observe ISP stage time.' },
    { key: 'output_rate', label: 'Output rate', description: 'Track output cadence.' },
    { key: 'fault_count', label: 'Fault count', description: 'Count pipeline faults.' }
  ]),
  'i2c': resourceSpec('resource.i2c', 'I2C', 'Low-speed control fabric for external configuration buses.', ['Addressed control transfers', 'Clocked control bus', 'Device register access'], [
    { key: 'bus_speed_hz', label: 'Bus speed', type: 'number', min: 10000, max: 1000000, step: 10000, unit: 'Hz', defaultValue: 400000 },
    ...genericTelemetryFields
  ], [
    { key: 'ack_rate', label: 'ACK rate', description: 'Observe bus acknowledgement success.' },
    { key: 'transaction_rate', label: 'Transaction rate', description: 'Track control transaction cadence.' },
    { key: 'stuck_bus', label: 'Stuck bus', description: 'Count stuck bus or recovery events.' }
  ]),
  'i2s': resourceSpec('resource.i2s', 'I2S', 'Synchronous serial sample transport hardware.', ['Clocked sample transport', 'DMA buffering', 'RX/TX framed streams'], [
    { key: 'direction', label: 'Direction', type: 'select', defaultValue: 'rx', options: [{ value: 'rx', label: 'RX' }, { value: 'tx', label: 'TX' }] },
    { key: 'sample_rate_hz', label: 'Sample rate', type: 'number', min: 8000, max: 192000, step: 1000, unit: 'Hz', defaultValue: 48000 },
    ...genericTelemetryFields
  ], [
    { key: 'sample_rate_actual', label: 'Actual rate', description: 'Track effective sample cadence.' },
    { key: 'dma_fill', label: 'DMA fill', description: 'Observe stream buffer fill.' },
    { key: 'underrun_overrun', label: 'Under/overrun', description: 'Count sample starvation or overflow.' }
  ]),
  'uart': resourceSpec('resource.uart', 'UART', 'Asynchronous serial transport hardware.', ['Byte stream TX/RX', 'Configurable baud and framing', 'Interrupt or DMA-backed serial transport'], [
    { key: 'baud_rate', label: 'Baud', type: 'number', min: 1200, max: 5000000, step: 1200, unit: 'baud', defaultValue: 115200 },
    { key: 'framing', label: 'Framing', type: 'select', defaultValue: '8n1', options: [{ value: '8n1', label: '8N1' }, { value: '8e1', label: '8E1' }, { value: '8o1', label: '8O1' }] },
    ...genericTelemetryFields
  ], [
    { key: 'rx_rate', label: 'RX rate', description: 'Observe receive throughput.' },
    { key: 'tx_rate', label: 'TX rate', description: 'Observe transmit throughput.' },
    { key: 'framing_errors', label: 'Framing errors', description: 'Count serial framing errors.' }
  ]),
  'usb serial/jtag': resourceSpec('resource.usb_serial_jtag', 'USB Serial/JTAG', 'Native USB control and debug transport.', ['Interactive command channel', 'Console transport', 'Flashing or debug handoff'], [
    { key: 'ownership_mode', label: 'Ownership', type: 'select', defaultValue: 'backend', options: [{ value: 'backend', label: 'Backend' }, { value: 'browser_flash', label: 'Browser flash' }] },
    ...genericTelemetryFields
  ], [
    { key: 'owner', label: 'Owner', description: 'Observe current transport owner.' },
    { key: 'disconnects', label: 'Disconnects', description: 'Count disconnect/reconnect events.' },
    { key: 'command_rate', label: 'Command rate', description: 'Track interactive control traffic.' }
  ]),
  'jpeg': resourceSpec('resource.jpeg', 'JPEG', 'Dedicated hardware support for JPEG stages.', ['Image encode/decode assist', 'Memory-backed pipeline stage', 'Block-level image operations'], [
    { key: 'operation', label: 'Operation', type: 'select', defaultValue: 'decode', options: [{ value: 'decode', label: 'Decode' }, { value: 'encode', label: 'Encode' }] },
    ...genericTelemetryFields
  ], [
    { key: 'throughput', label: 'Throughput', description: 'Track JPEG stage throughput.' },
    { key: 'buffer_usage', label: 'Buffer usage', description: 'Observe working-buffer occupancy.' },
    { key: 'error_count', label: 'Error count', description: 'Count JPEG stage faults.' }
  ]),
  'ppa': resourceSpec('resource.ppa', 'PPA', 'Pixel processing accelerator for transform and scale operations.', ['Scale/transform offload', 'Pixel processing acceleration', 'Display-path assist'], [
    { key: 'transform_mode', label: 'Transform', type: 'select', defaultValue: 'scale', options: [{ value: 'scale', label: 'Scale' }, { value: 'blend', label: 'Blend' }, { value: 'copy', label: 'Copy' }] },
    ...genericTelemetryFields
  ], [
    { key: 'stage_latency', label: 'Stage latency', description: 'Observe PPA processing latency.' },
    { key: 'work_rate', label: 'Work rate', description: 'Track processed frames or blocks.' },
    { key: 'fallback_count', label: 'Fallback count', description: 'Count software fallbacks or failures.' }
  ]),
  'rgb lcd': resourceSpec('resource.rgb_lcd', 'RGB LCD', 'Dedicated RGB LCD output path.', ['Parallel RGB display drive', 'Frame timing ownership', 'Display DMA support'], [
    { key: 'pixel_clock_hz', label: 'Pixel clock', type: 'number', min: 1000000, max: 80000000, step: 1000000, unit: 'Hz', defaultValue: 9000000 },
    ...genericTelemetryFields
  ], [
    { key: 'present_fps', label: 'Present FPS', description: 'Track panel output cadence.' },
    { key: 'underflow', label: 'Underflow', description: 'Count frame underflow events.' },
    { key: 'timing_faults', label: 'Timing faults', description: 'Count RGB timing issues.' }
  ]),
  'mipi dsi': resourceSpec('resource.mipi_dsi', 'MIPI DSI', 'High-speed display serial interface.', ['Serialized display transport', 'Panel command/data path', 'Display timing ownership'], [
    { key: 'lane_count', label: 'Lanes', type: 'number', min: 1, max: 4, step: 1, defaultValue: 2 },
    ...genericTelemetryFields
  ], [
    { key: 'link_state', label: 'Link state', description: 'Observe DSI link readiness.' },
    { key: 'throughput', label: 'Throughput', description: 'Track lane throughput.' },
    { key: 'ecc_crc_errors', label: 'ECC/CRC', description: 'Count transport integrity faults.' }
  ]),
  'mipi csi': resourceSpec('resource.mipi_csi', 'MIPI CSI', 'High-speed camera serial interface.', ['Serialized sensor ingress', 'Lane-based capture transport', 'Camera link management'], [
    { key: 'lane_count', label: 'Lanes', type: 'number', min: 1, max: 4, step: 1, defaultValue: 2 },
    ...genericTelemetryFields
  ], [
    { key: 'link_state', label: 'Link state', description: 'Observe CSI link readiness.' },
    { key: 'frame_rate', label: 'Frame rate', description: 'Track ingress frame rate.' },
    { key: 'crc_errors', label: 'CRC errors', description: 'Count transport integrity faults.' }
  ])
};

const genericResourceSpec: GraphBlockSpec = resourceSpec(
  'resource.generic',
  'ESP32-P4 Resource',
  'A claimed or candidate hardware resource inside the ESP32-P4.',
  ['Declare ownership', 'Expose configuration overlays', 'Select monitoring channels'],
  [
    { key: 'claim_mode', label: 'Claim mode', type: 'select', defaultValue: 'exclusive', options: [
      { value: 'exclusive', label: 'Exclusive' },
      { value: 'shared_lab', label: 'Shared lab' },
      { value: 'observe_only', label: 'Observe only' }
    ] },
    ...genericTelemetryFields
  ],
  [
    { key: 'state', label: 'State', description: 'Observe coarse block state.' },
    { key: 'activity', label: 'Activity', description: 'Track whether the resource is active or idle.' }
  ]
);

function normalizeName(value: string) {
  return value.toLowerCase().replace(/[_-]+/g, ' ').replace(/\s+/g, ' ').trim();
}

export function resolveGraphBlockSpec(node: GraphNodeRecord | null): GraphBlockSpec | null {
  if (!node) return null;
  const type = String(node.type || 'block');
  const params = node.params && typeof node.params === 'object' ? node.params as GraphNodeRecord : {};
  const label = String(node.label || '');
  const normalizedLabel = normalizeName(label);

  if (type === 'system_intent') return systemIntentSpec;
  if (sdfBlockSpecs[type]) return sdfBlockSpecs[type];
  if (type === 'external_device') {
    if (normalizedLabel.includes('lcd') || normalizedLabel.includes('panel')) return externalDeviceSpecs.lcd_panel;
    if (normalizedLabel.includes('camera') || normalizedLabel.includes('sensor')) return externalDeviceSpecs.camera_sensor;
    return externalDeviceSpecs.default;
  }
  if (type === 'lab_function_block') {
    const apiGroup = normalizeName(String(params.api_group || 'default'));
    return apiGroupSpecs[apiGroup] || apiGroupSpecs.default;
  }
  if (type === 'esp32p4_resource') {
    return esp32ResourceSpecs[normalizedLabel] || genericResourceSpec;
  }
  if (type.includes('source')) return externalDeviceSpecs.camera_sensor;
  if (type.includes('destination')) return externalDeviceSpecs.lcd_panel;
  return apiGroupSpecs.default;
}

export function graphLibraryEntries(): GraphLibraryEntry[] {
  const entries: GraphLibraryEntry[] = [
    {
      id: 'intent.system',
      family: 'intent',
      lane: 'intent',
      nodeTemplate: {
        type: 'system_intent',
        label: 'Project Intent',
        params: { role: 'graph_intent' }
      },
      spec: systemIntentSpec
    }
  ];

  const sdfTemplates: Array<[string, GraphLibraryEntry['lane'], Record<string, string[]> | undefined, Record<string, unknown> | undefined, string]> = [
    ['constant_pattern_source', 'io', { out: ['pattern'] }, { pattern_kind: 'square', value: 1 }, 'Constant Pattern'],
    ['timer_tick', 'intent', { out: ['tick'] }, { period_ms: 500 }, 'Timer Tick'],
    ['toggle_logic', 'sdk', { in: ['tick'], out: ['level'] }, { initial_level: 'low' }, 'Toggle Logic'],
    ['gpio_led_destination', 'io', { in: ['level'] }, { gpio: 2, active_level: 1 }, 'GPIO LED'],
    ['gbc_lcd_source', 'io', { out: ['frame', 'sync'] }, { visible_width: 160, visible_height: 144, pixel_format: 'rgb565' }, 'GBC LCD Source'],
    ['spi_lcd_destination', 'io', { in: ['frame'] }, { width: 320, height: 240, pclk_hz_initial: 20000000 }, 'SPI LCD Destination']
  ];

  for (const [type, lane, ports, params, label] of sdfTemplates) {
    entries.push({
      id: `sdf.${type}`,
      family: 'sdf',
      lane,
      nodeTemplate: { type, label, ports, params },
      spec: sdfBlockSpecs[type]
    });
  }

  for (const [apiGroup, spec] of Object.entries(apiGroupSpecs)) {
    if (apiGroup === 'default') continue;
    entries.push({
      id: `sdk.${apiGroup}`,
      family: 'sdk',
      lane: 'sdk',
      nodeTemplate: {
        type: 'lab_function_block',
        label: spec.title,
        params: { api_group: apiGroup, components: [] }
      },
      spec
    });
  }

  for (const [resourceName, spec] of Object.entries(esp32ResourceSpecs)) {
    entries.push({
      id: `resource.${resourceName}`,
      family: 'resource',
      lane: 'resource',
      nodeTemplate: {
        type: 'esp32p4_resource',
        label: spec.title,
        params: { resource_claim: 'planned', confidence: 'graph' }
      },
      spec
    });
  }

  entries.push({
    id: 'external.lcd',
    family: 'external',
    lane: 'io',
    nodeTemplate: {
      type: 'external_device',
      label: 'External LCD Panel',
      params: { role: 'external_hardware' }
    },
    spec: externalDeviceSpecs.lcd_panel
  });
  entries.push({
    id: 'external.sensor',
    family: 'external',
    lane: 'io',
    nodeTemplate: {
      type: 'external_device',
      label: 'External Camera/Sensor',
      params: { role: 'external_hardware' }
    },
    spec: externalDeviceSpecs.camera_sensor
  });

  return entries;
}
