/**
 * znssd_mode.h
 * znssd-device Mode (BBSSD+ZNSSD) Definitions and Functions
 */

#ifndef znssd_MODE_H
#define znssd_MODE_H

#include <ncurses.h>

#include "../core/data_types.h"

// Wire-format structs (znssd_zone_descr_info_t, znssd_zone_info_t —
// including the per-zone cnt_reset field, znssd_channel_stat_t,
// znssd_plane_stat_t, znssd_monitor_header_t) are defined in the shared
// protocol header, included via config.h → femon_protocol.h.

// znssd-device monitor data structure
typedef struct {
  uint64_t num_zones;
  uint64_t zr_count;
  uint64_t num_channels;        /* Number of channels (confznsplusplus extended) */
  uint64_t num_planes;          /* Number of planes (confznsplusplus extended) */
  uint64_t wc_hit;              /* write cache stats (v3) */
  uint64_t wc_miss;
  uint64_t wc_evict;
  uint64_t wc_flush;
  znssd_zone_info_t *znssd_data;        /* ZNSSD zone data */
  znssd_channel_stat_t *channel_stats;  /* Channel statistics (confznsplusplus) */
  znssd_plane_stat_t *plane_stats;      /* Plane statistics (confznsplusplus) */
} znssd_data_t;

/**
 * Initialize znssd-device mode
 *
 * @param manager Device manager to initialize
 * @return 0 on success, non-zero on failure
 */
int znssd_mode_init(device_context_t *manager);

/**
 * Clean up znssd-device mode resources
 *
 * @param manager Device manager to clean up
 */
void znssd_mode_cleanup(device_context_t *manager);

/**
 * Get znssd data from device
 *
 * @param device Device context
 * @return 0 on success, non-zero on failure
 */
int znssd_mode_get_data(device_context_t *device);

/**
 * Update ZNSSD history data
 *
 * @param device Device context
 */
void znssd_mode_update_history(device_context_t *device);

/**
 * Render ZNSSD-specific UI elements
 *
 * @param win Window to render to
 * @param device Device context
 * @param row Starting row
 * @param col St arting column
 * @param width Available width
 * @param height Available height
 * @return Next row after rendered elements
 */
int znssd_mode_render(WINDOW *win, device_context_t *device, int row, int col,
                      int width, int height);

#endif /* znssd_MODE_H */