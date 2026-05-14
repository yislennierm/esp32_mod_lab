#pragma once

#define GBC_P4_PROBE_NAME "gbc-p4-probe"
#ifdef GBC_P4_SAFE_RECOVERY
#define GBC_P4_PROBE_VERSION "0.1.0-safe-recovery"
#define GBC_P4_PROBE_PHASE "safe-recovery-electrical-isolation"
#elif defined(GBC_P4_LAB)
#define GBC_P4_PROBE_VERSION "0.1.0-lab"
#define GBC_P4_PROBE_PHASE "lab-investigation-and-probing"
#elif defined(GBC_P4_TELEMETRY)
#define GBC_P4_PROBE_VERSION "0.1.0-telemetry"
#define GBC_P4_PROBE_PHASE "telemetry-observe-and-monitor"
#elif defined(GBC_P4_PRODUCTION_MIRROR)
#define GBC_P4_PROBE_VERSION "0.1.0-production-mirror"
#define GBC_P4_PROBE_PHASE "early-production-gbc-source-to-spi-lcd"
#else
#define GBC_P4_PROBE_VERSION "0.1.0-phase1"
#define GBC_P4_PROBE_PHASE "phase1-electrical-safety"
#endif
