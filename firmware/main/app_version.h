#pragma once

#define GBC_P4_PROBE_NAME "gbc-p4-probe"
#ifdef GBC_P4_SAFE_RECOVERY
#define GBC_P4_PROBE_VERSION "0.1.0-safe-recovery"
#define GBC_P4_PROBE_PHASE "safe-recovery-electrical-isolation"
#else
#define GBC_P4_PROBE_VERSION "0.1.0-phase1"
#define GBC_P4_PROBE_PHASE "phase1-electrical-safety"
#endif
