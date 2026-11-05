/**
 * data_types.h
 * FEMONITOR Data Type Definitions
 */

#ifndef DATA_TYPES_H
#define DATA_TYPES_H

#include <stdint.h>

#include "../common/config.h"

// Include history types first to avoid circular dependencies
#include "../common/history.h"

// Operating mode definitions
typedef enum {
  MODE_MULTI = 0,  // Multi-device mode (BBSSD + ZNSSD)
  MODE_BBSSD = 1,  // BBSSD standalone mode
  MODE_ZNSSD = 2   // ZNSSD standalone mode
} femonitor_mode_t;

// Device type definitions
typedef uint64_t device_type_t;

enum {
  DEVICE_BBSSD = 0,
  DEVICE_ZNSSD = 1,
  DEVICE_UNKNOWN = -1,
};

// Unified device information structure — wire format from the shared
// protocol header (femon_protocol.h via config.h)
typedef femon_device_info_t nvme_device_info_t;

// Command line options
typedef struct {
  char devices[MAX_DEVICES][MAX_DEVICE_PATH];  // Device path array
  int device_count;                            // Number of configured devices
  int current_device;   // Currently selected device index
  femonitor_mode_t mode;  // Operating mode
  float interval;       // Update interval
  int starting_view;    // Initial view to display
  int hide_cursor;      // Flag to hide cursor (1) or show it (0)
  int debug_mode;       // Flag to enable debug mode
  int auto_start_log;   // Flag to start logging automatically
  int log_mode;         // Logging mode (0: individual, 1: aggregate)
  char log_directory[MAX_DEVICE_PATH];  // Log directory path
  int snapshot_mode;    // Flag for snapshot mode (single capture, CSV output)
  int reset_stats;      // Flag to reset statistics before starting
  int list_mode;        // Flag: print registry schema (0xD1) and exit
  int generic_mode;     // Flag: dump all registry metrics (0xD2) and exit
  int trace_ctl;        // Tracepoint control: -1 none, 0 off, 1 on (0xD3)
  int hist_mode;        // Flag: render histogram metrics and exit
} femonitor_options_t;

// Base data structure for global view state
typedef struct {
  // View and display settings
  int update_count;  // Number of updates performed
  int auto_scale;    // Flag for auto-scaling graphs
  int show_grid;     // Flag for showing grid on graphs
  int sort_mode;     // Current sort mode
  int filter_mode;   // Current filter mode

  // Optional mode-specific state data can be added here
} view_state_t;

// Forward declarations for device context and manager
struct device_context_s;
typedef struct device_context_s device_context_t;

struct device_manager_s;
typedef struct device_manager_s device_manager_t;

// Include mode-specific data structures
#include "../modes/bbssd_mode.h"
#include "../modes/multi_mode.h"
#include "../modes/znssd_mode.h"

#endif /* DATA_TYPES_H */