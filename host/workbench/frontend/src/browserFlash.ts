import type { FlashManifest, SerialOwnershipResult } from './api';

export type BrowserFlashPhase =
  | 'idle'
  | 'preparing'
  | 'awaiting_port'
  | 'connecting'
  | 'downloading'
  | 'writing'
  | 'resetting'
  | 'reconnecting'
  | 'complete'
  | 'error';

export type BrowserFlashSessionResult = {
  chip: string;
  reconnect?: SerialOwnershipResult;
};

type BrowserFlashSessionOptions = {
  manifest: FlashManifest;
  buildProfile: string;
  releaseSerial?: () => Promise<SerialOwnershipResult>;
  reconnectSerial?: () => Promise<SerialOwnershipResult>;
  onLog?: (line: string) => void;
  onProgress?: (value: number) => void;
  onPhase?: (phase: BrowserFlashPhase, detail?: string) => void;
};

type BrowserNavigator = Navigator & {
  serial?: {
    requestPort: (options?: unknown) => Promise<unknown>;
  };
};

export async function runBrowserFlashSession({
  manifest,
  buildProfile,
  releaseSerial,
  reconnectSerial,
  onLog,
  onProgress,
  onPhase
}: BrowserFlashSessionOptions): Promise<BrowserFlashSessionResult> {
  const log = (line: string) => onLog?.(line);
  const setPhase = (phase: BrowserFlashPhase, detail?: string) => onPhase?.(phase, detail);
  const setProgress = (value: number) => onProgress?.(value);
  const serialApi = (navigator as BrowserNavigator).serial;

  if (!serialApi) {
    throw new Error('web_serial_not_supported');
  }

  setPhase('preparing', 'Reserving serial access for browser flashing');
  if (releaseSerial) {
    const ownership = await releaseSerial();
    log(`Serial ownership: ${ownership.serial_owner || ownership.state || 'released'}`);
  }

  setPhase('awaiting_port', `Choose the ${manifest.chip} serial port in the browser prompt`);
  const port = await serialApi.requestPort();

  const esptool = await import('esptool-js');
  const terminal = {
    clean() {
      return;
    },
    writeLine(data: string) {
      if (data.trim()) log(data);
    },
    write(data: string) {
      if (data.trim()) log(data);
    }
  };

  setPhase('connecting', `Connecting to ${manifest.chip}`);
  const transport = new esptool.Transport(port, true);
  const loader = new esptool.ESPLoader({
    transport,
    baudrate: 115200,
    terminal,
    debugLogging: false,
    resetConstructors: {
      usbJTAGSerialReset: (transportInstance: unknown) => new esptool.UsbJtagSerialReset(transportInstance as never)
    }
  });

  try {
    const chip = await loader.main(manifest.before as never);
    log(`Connected to ${chip}`);

    setPhase('downloading', 'Loading build artifacts into the browser');
    const totalBytes = manifest.images.reduce((sum, image) => sum + image.size, 0);
    const imageData = await Promise.all(manifest.images.map(async (image) => {
      const response = await fetch(image.url, { cache: 'no-store' });
      if (!response.ok) {
        throw new Error(`artifact_fetch_failed:${image.relative_path}`);
      }
      return {
        address: image.address,
        size: image.size,
        data: new Uint8Array(await response.arrayBuffer())
      };
    }));

    setPhase('writing', `Flashing ${manifest.images.length} images`);
    let committedBase = 0;
    await loader.writeFlash({
      fileArray: imageData.map((image) => ({ data: image.data, address: image.address })),
      flashMode: manifest.flash_mode as never,
      flashFreq: manifest.flash_freq as never,
      flashSize: manifest.flash_size as never,
      eraseAll: false,
      compress: true,
      reportProgress: (fileIndex: number, written: number, total: number) => {
        const currentSize = imageData[fileIndex]?.size || total || 0;
        const normalizedWritten = total > 0 ? Math.min(currentSize, Math.round((written / total) * currentSize)) : written;
        const overallWritten = Math.min(totalBytes, committedBase + normalizedWritten);
        setProgress(totalBytes > 0 ? Math.round((overallWritten / totalBytes) * 100) : 0);
        if (written >= total) {
          committedBase += currentSize;
        }
      }
    });
    setProgress(100);

    setPhase('resetting', 'Resetting the board into the flashed firmware');
    await loader.after(manifest.after as never);
    log('Flash write completed');
    await transport.disconnect();

    let reconnect: SerialOwnershipResult | undefined;
    if (buildProfile !== 'production' && reconnectSerial) {
      setPhase('reconnecting', 'Reconnecting the backend for live lab access');
      reconnect = await reconnectSerial();
      log(reconnect.ok ? 'Backend serial reconnected' : `Reconnect failed: ${reconnect.error || 'unknown error'}`);
    }

    setPhase('complete', buildProfile === 'production'
      ? 'Production image flashed; device remains in product mode'
      : 'Flash completed and backend access restored');
    return { chip, reconnect };
  } catch (error) {
    setPhase('error', error instanceof Error ? error.message : String(error));
    throw error;
  } finally {
    try {
      await transport.disconnect();
    } catch {
      // Ignore disconnect cleanup errors; flashing outcome is already decided above.
    }
  }
}
