/**
 * config.h
 * Common Configuration and Constants for FEMONITOR
 */

#ifndef CONFIG_H
#define CONFIG_H

// Configuration values
#define DEFAULT_CNS_DEVICE "/dev/nvme0n1"
#define DEFAULT_ZNS_DEVICE "/dev/nvme1n1"
#define BUFFER_SIZE 8192
#define DEFAULT_INTERVAL 1.0
#define HISTORY_SIZE 60                 // Increased history for better trending
#define MAX_DEVICES 4                   // Maximum number of supported devices
#define MAX_DEVICE_PATH 256             // Maximum device path length

// NVMe Admin command opcodes
#define NVME_ADMIN_CMD_IDENTIFY 0x06    // Identify command

// Vendor opcodes + wire-format structs come from the shared protocol header
// (single source of truth, also compiled into final/femu)
#include "femon_protocol.h"


// Timeout values
#define NVME_CMD_TIMEOUT 5000  // Timeout in milliseconds

#endif /* CONFIG_H */