/**
 * view_zns.c
 * ZNS (Zoned Namespace) SSD View Implementation
 */

#include "../../include/views/view_interface.h"
#include "../../include/views/view_zns.h"

#include <ncurses.h>
#include <stdlib.h>
#include <string.h>

#include "../../include/common/ui_common.h"
#include "../../include/core/device_manager.h"
#include "../../include/modes/znssd_mode.h"
#include "../../include/modes/multi_mode.h"

// Forward declarations of private functions
static int view_zns_init(void);
static int view_zns_render(WINDOW *win, device_manager_t *manager, int width,
                             int height);
static int view_zns_handle_input(int ch, device_manager_t *manager);
static void view_zns_cleanup(void);
static int render_all_zones_table(WINDOW *win, device_manager_t *manager, int width, int height);

// Local variables
static int view_zns_initialized = 0;
static int show_all_zones = 0;  // Toggle for showing all zones table
static int zone_scroll_offset = 0;   // Scroll offset for all zones view

// Zone state definitions for display
static const char* zone_state_names[] = {
    "Empty", "Imp Open", "Exp Open", "Closed", "Read Only", "Full", "Offline", "Reserved"
};

// Zone type names for future use
// static const char* zone_type_names[] = {
//     "Reserved", "Conventional", "Sequential"
// };

// View interface implementation
view_interface_t view_zns = {
    .name = "ZNS_VIEW",
    .description = "ZNS SSD",
    .key = "2",
    .init = view_zns_init,
    .activate = view_interface_default_activate,
    .deactivate = view_interface_default_deactivate,
    .render = view_zns_render,
    .handle_input = view_zns_handle_input,
    .cleanup = view_zns_cleanup};

/**
 * Initialize ZNS view
 *
 * @return 0 on success, non-zero on failure
 */
static int view_zns_init(void) {
  if (view_zns_initialized) {
    return 0;
  }

  view_zns_initialized = 1;
  return 0;
}

/**
 * Get zone state color based on state
 */
static int get_zone_state_color(uint8_t zs) {
  switch (zs) {
    case 1: // Empty
      return COLOR_PAIR_GREEN;
    case 2: // Implicitly Opened
    case 3: // Explicitly Opened
      return COLOR_PAIR_YELLOW;
    case 4: // Closed
      return COLOR_PAIR_BLUE;
    case 5: // Full
      return COLOR_PAIR_RED;
    case 6: // Read Only
    case 7: // Offline
      return COLOR_PAIR_MAGENTA;
    default:
      return COLOR_PAIR_NORMAL;
  }
}

/**
 * Calculate zone utilization percentage
 */
static int get_zone_utilization(znssd_zone_info_t *zone) {
  if (zone->d.zcap == 0) return 0;

  uint64_t used = zone->d.wp - zone->d.zslba;
  return (int)((used * 100) / zone->d.zcap);
}

/**
 * Render ZNS view
 *
 * @param win Window to render to
 * @param manager Device manager
 * @param width Window width
 * @param height Window height
 * @return 0 on success, non-zero on failure
 */
static int view_zns_render(WINDOW *win, device_manager_t *manager, int width,
                             int height) {
  // Check if we should show all zones table
  if (show_all_zones) {
    return render_all_zones_table(win, manager, width, height);
  }

  int row = 3;
  int col_left = 2;
  int col_right = width / 2 + 2;
  int half_width = width / 2 - 4;

  // Check if this is the correct mode
  if (manager->mode != MODE_MULTI) {
    mvwprintw(win, row, col_left,
              "ZNS view is only available in BBSSD+ZNSSD mode");
    return 0;
  }

  // Get current device context
  device_context_t *device = &manager->devices[1];

  // Check if current device is ZNSSD type
  if (device->device_type != DEVICE_ZNSSD) {
    mvwprintw(win, row, col_left,
              "ZNS view requires ZNSSD device (current device is not ZNSSD)");
    return 0;
  }

  znssd_data_t *znssd_data = (znssd_data_t *)device->device_data;
  if (!znssd_data || !znssd_data->znssd_data) {
    mvwprintw(win, row, col_left, "No ZNSSD data available");
    return 0;
  }

  // Title
  wattron(win, A_BOLD | COLOR_PAIR(COLOR_PAIR_CYAN));
  mvwprintw(win, row++, col_left, "Zoned Namespace SSD (ZNS) Monitoring");
  wattroff(win, A_BOLD | COLOR_PAIR(COLOR_PAIR_CYAN));
  row++;

  // ========== LEFT COLUMN: Zone Overview ==========

  // Zone Summary Box
  draw_box_with_title(win, row, col_left, 10, half_width, "Zone Summary",
                      COLOR_PAIR_CYAN);
  int box_row = row + 1;

  // Count zones by state
  int state_counts[8] = {0}; // Index by zone state
  int total_zones = 0;
  uint64_t total_capacity = 0;
  uint64_t used_capacity = 0;

  for (uint64_t i = 0; i < znssd_data->num_zones; i++) {
    znssd_zone_info_t *zone = &znssd_data->znssd_data[i];
    if (zone->d.zs < 8) {
      state_counts[zone->d.zs]++;
    }
    total_zones++;
    total_capacity += zone->d.zcap;

    // Calculate used capacity based on write pointer
    if (zone->d.wp > zone->d.zslba) {
      used_capacity += (zone->d.wp - zone->d.zslba);
    }
  }

  char total_zones_str[32], total_cap_str[32], used_cap_str[32];
  format_number(total_zones, total_zones_str, sizeof(total_zones_str));
  format_number(total_capacity * 512, total_cap_str, sizeof(total_cap_str));
  format_number(used_capacity * 512, used_cap_str, sizeof(used_cap_str));

  mvwprintw(win, box_row++, col_left + 2, "Total Zones     : %s", total_zones_str);
  mvwprintw(win, box_row++, col_left + 2, "Total Capacity  : %s", total_cap_str);
  mvwprintw(win, box_row++, col_left + 2, "Used Capacity   : %s", used_cap_str);

  // Overall utilization
  int overall_util = (total_capacity > 0) ? (int)((used_capacity * 100) / total_capacity) : 0;
  mvwprintw(win, box_row, col_left + 2, "Utilization     : ");
  draw_progress_bar(win, box_row++, col_left + 20, 15, overall_util,
                    overall_util > 80 ? COLOR_PAIR_RED :
                    (overall_util > 60 ? COLOR_PAIR_YELLOW : COLOR_PAIR_GREEN));

  // Zone reset count
  char zr_count_str[32];
  format_number(znssd_data->zr_count, zr_count_str, sizeof(zr_count_str));
  mvwprintw(win, box_row++, col_left + 2, "Zone Resets     : %s", zr_count_str);

  row += 11;

  // Zone State Distribution Box
  draw_box_with_title(win, row, col_left, 11, half_width, "Zone States",
                      COLOR_PAIR_CYAN);
  box_row = row + 1;

  int bar_width = half_width - 20;

  for (int i = 1; i < 8; i++) { // Skip reserved state (0)
    if (state_counts[i] > 0) {
      int color = get_zone_state_color(i);
      const char* state_name = (i < 8) ? zone_state_names[i] : "Unknown";

      mvwprintw(win, box_row, col_left + 2, "%-10s:", state_name);
      draw_bar(win, box_row++, col_left + 14, state_counts[i], total_zones,
               bar_width, color);
    }
  }

  row += 12;

  // ========== RIGHT COLUMN: Zone Details ==========

  // Top Active Zones Box
  int right_row = 5;
  draw_box_with_title(win, right_row, col_right, 12, half_width,
                      "Active Zones", COLOR_PAIR_CYAN);
  box_row = right_row + 1;

  mvwprintw(win, box_row++, col_right + 2, "Zone  State    Util  Write Ptr");
  draw_separator(win, box_row++, col_right + 2, half_width - 4);

  // Show first 8 active zones (non-empty, non-full)
  int zones_shown = 0;
  for (uint64_t i = 0; i < znssd_data->num_zones && zones_shown < 8; i++) {
    znssd_zone_info_t *zone = &znssd_data->znssd_data[i];

    // Show zones that are actively being written to
    if (zone->d.zs >= 2 && zone->d.zs <= 4) { // Opened or Closed
      int util = get_zone_utilization(zone);
      int color = get_zone_state_color(zone->d.zs);
      const char* state_name = (zone->d.zs < 8) ? zone_state_names[zone->d.zs] : "Unknown";

      wattron(win, COLOR_PAIR(color));
      mvwprintw(win, box_row++, col_right + 2, "%4llu %-8s %3d%% %8llx",
                (unsigned long long)i, state_name, util,
                (unsigned long long)zone->d.wp);
      wattroff(win, COLOR_PAIR(color));
      zones_shown++;
    }
  }

  if (zones_shown == 0) {
    mvwprintw(win, box_row, col_right + 2, "No active zones");
  }

  right_row += 13;

  // Zone Health Indicators
  draw_box_with_title(win, right_row, col_right, 7, half_width,
                      "Zone Health", COLOR_PAIR_CYAN);
  box_row = right_row + 1;

  // Empty zones percentage (good to have high)
  int empty_pct = (total_zones > 0) ? (state_counts[1] * 100 / total_zones) : 0;
  draw_status_indicator(win, box_row++, col_right + 2, empty_pct,
                        30, 20, 1, "Empty Zones");

  // Full zones percentage (bad to have too high)
  int full_pct = (total_zones > 0) ? (state_counts[5] * 100 / total_zones) : 0;
  draw_status_indicator(win, box_row++, col_right + 2, full_pct,
                        70, 90, 0, "Full Zones");

  // Read-only/Offline zones (critical if any exist)
  int problematic_zones = state_counts[6] + state_counts[7]; // Read-only + Offline
  int problematic_pct = (total_zones > 0) ? (problematic_zones * 100 / total_zones) : 0;
  draw_status_indicator(win, box_row++, col_right + 2, problematic_pct,
                        5, 10, 0, "Problem Zones");

  right_row += 8;

  // ========== BOTTOM SECTION: Zone Activity & Performance ==========

  // Zone Activity Box
  draw_box_with_title(win, right_row, col_right, 6, half_width, "Zone Activity",
                      COLOR_PAIR_CYAN);
  box_row = right_row + 1;

  // Active write zones
  int active_write_zones = state_counts[2] + state_counts[3]; // Imp Open + Exp Open
  mvwprintw(win, box_row++, col_right + 2, "Write Active    : %d", active_write_zones);

  // Zone reset efficiency (lower is better)
  if (total_zones > 0) {
    double reset_ratio = (double)znssd_data->zr_count / total_zones;
    mvwprintw(win, box_row++, col_right + 2, "Reset Ratio     : %.2f", reset_ratio);

    // Zone efficiency indicator
    int efficiency = (reset_ratio < 0.1) ? 90 : (reset_ratio < 0.5) ? 70 : 40;
    mvwprintw(win, box_row, col_right + 2, "Efficiency      : ");
    draw_progress_bar(win, box_row++, col_right + 20, 15, efficiency,
                      efficiency > 80 ? COLOR_PAIR_GREEN :
                      (efficiency > 60 ? COLOR_PAIR_YELLOW : COLOR_PAIR_RED));
  } else {
    mvwprintw(win, box_row++, col_right + 2, "Reset Ratio     : N/A");
    mvwprintw(win, box_row++, col_right + 2, "Efficiency      : N/A");
  }

  // Bottom help text
  int bottom_row = height - 2;
  wattron(win, A_DIM);
  mvwprintw(win, bottom_row, col_left,
            "Press 'a' for all zones");
  wattroff(win, A_DIM);

  return 0;
}

/**
 * Handle input for ZNS view
 *
 * @param ch Input character
 * @param manager Device manager
 * @return 1 if the input was handled, 0 if not
 */
static int view_zns_handle_input(int ch, device_manager_t *manager) {
  // Handle view-specific input
  switch (ch) {
    case 'a':  // Toggle all zones view
    case 'A':
      show_all_zones = !show_all_zones;
      zone_scroll_offset = 0;  // Reset scroll when toggling
      return 1;

    case ' ':  // Toggle grid
      manager->view_state.show_grid = !manager->view_state.show_grid;
      return 1;

    case KEY_UP:
      if (show_all_zones && zone_scroll_offset > 0) {
        zone_scroll_offset--;
      }
      return 1;

    case KEY_DOWN:
      if (show_all_zones) {
        zone_scroll_offset++;
      }
      return 1;

    case KEY_PPAGE:
      if (show_all_zones && zone_scroll_offset > 10) {
        zone_scroll_offset -= 10;
      } else if (show_all_zones) {
        zone_scroll_offset = 0;
      }
      return 1;

    case KEY_NPAGE:
      if (show_all_zones) {
        zone_scroll_offset += 10;
      }
      return 1;
  }

  return 0;  // Input not handled
}

/**
 * Render all zones table view
 *
 * @param win Window to render to
 * @param manager Device manager
 * @param width Window width
 * @param height Window height
 * @return 0 on success, non-zero on failure
 */
static int render_all_zones_table(WINDOW *win, device_manager_t *manager, int width __attribute__((unused)), int height) {
  int row = 3;
  int col = 2;

  // Check if this is the correct mode
  if (manager->mode != MODE_MULTI) {
    mvwprintw(win, row, col, "ZNS view is only available in BBSSD+ZNSSD mode");
    return 0;
  }

  // Get current device context
  device_context_t *device = &manager->devices[1];

  // Check if current device is ZNSSD type
  if (device->device_type != DEVICE_ZNSSD) {
    mvwprintw(win, row, col, "ZNS view requires ZNSSD device (current device is not ZNSSD)");
    return 0;
  }

  znssd_data_t *znssd_data = (znssd_data_t *)device->device_data;
  if (!znssd_data || !znssd_data->znssd_data) {
    mvwprintw(win, row, col, "No ZNSSD data available");
    return 0;
  }

  // Title
  wattron(win, A_BOLD | COLOR_PAIR(COLOR_PAIR_CYAN));
  mvwprintw(win, row++, col, "All Zones Information (Total: %llu)",
            (unsigned long long)znssd_data->num_zones);
  wattroff(win, A_BOLD | COLOR_PAIR(COLOR_PAIR_CYAN));
  row++;

  // Help text
  wattron(win, A_DIM);
  mvwprintw(win, row++, col, "Press 'a' to return to summary view | Up/Down: Scroll | PgUp/PgDn: Page");
  wattroff(win, A_DIM);
  row++;

  // Table header
  wattron(win, A_BOLD | COLOR_PAIR(COLOR_PAIR_HEADER));
  mvwprintw(win, row, col, "%-5s %-8s %-4s %-4s %-10s %-12s %-10s %-8s",
            "Zone", "State", "Type", "Util", "Capacity", "Start LBA", "Write Ptr", "Visual");
  wattroff(win, A_BOLD | COLOR_PAIR(COLOR_PAIR_HEADER));
  row++;

  // Separator line
  mvwhline(win, row++, col, ACS_HLINE, 70);

  // Calculate max visible zones
  int max_visible_zones = height - row - 2;
  uint64_t total_zones = znssd_data->num_zones;

  // Bound scroll offset
  if (zone_scroll_offset > (int)(total_zones - (uint64_t)max_visible_zones)) {
    zone_scroll_offset = (total_zones > (uint64_t)max_visible_zones) ? (int)(total_zones - (uint64_t)max_visible_zones) : 0;
  }
  if (zone_scroll_offset < 0) {
    zone_scroll_offset = 0;
  }

  // Display zones data
  for (uint64_t i = (uint64_t)zone_scroll_offset; i < total_zones && i < (uint64_t)(zone_scroll_offset + max_visible_zones); i++) {
    znssd_zone_info_t *zone = &znssd_data->znssd_data[i];

    // Calculate utilization
    int utilization = get_zone_utilization(zone);

    // Get state name and color
    const char* state_name = ((zone->d.zs>>4) < 8) ? zone_state_names[(zone->d.zs>>4)] : "Unknown";
    int state_color = get_zone_state_color(zone->d.zs);

    // Zone type (simplified)
    const char* type_str = (zone->d.zt == 2) ? "SEQ" : (zone->d.zt == 1) ? "CNV" : "RSV";

    // Format capacity and addresses
    char cap_str[16], slba_str[16], wp_str[16];
    format_number(zone->d.zcap, cap_str, sizeof(cap_str));
    snprintf(slba_str, sizeof(slba_str), "%llx", (unsigned long long)zone->d.zslba);
    snprintf(wp_str, sizeof(wp_str), "%llx", (unsigned long long)zone->d.wp);

    // Color based on utilization
    int util_color = COLOR_PAIR_GREEN;
    if (utilization > 80) {
      util_color = COLOR_PAIR_RED;
    } else if (utilization > 60) {
      util_color = COLOR_PAIR_YELLOW;
    }

    // Zone number
    mvwprintw(win, row, col, "%-5llu", (unsigned long long)i);

    // State
    wattron(win, COLOR_PAIR(state_color));
    // mvwprintw(win, row, col + 6, "%-8d", zone->d.zs);
    mvwprintw(win, row, col + 6, "%-8s", state_name);
    wattroff(win, COLOR_PAIR(state_color));

    // Type
    mvwprintw(win, row, col + 15, "%-4s", type_str);

    // Utilization
    wattron(win, COLOR_PAIR(util_color));
    mvwprintw(win, row, col + 20, "%3d%%", utilization);
    wattroff(win, COLOR_PAIR(util_color));

    // Capacity
    mvwprintw(win, row, col + 25, "%-10s", cap_str);

    // Start LBA
    mvwprintw(win, row, col + 36, "%-12s", slba_str);

    // Write Pointer
    mvwprintw(win, row, col + 49, "%-10s", wp_str);

    // Visual representation (utilization bar)
    mvwprintw(win, row, col + 60, "[");
    int bar_length = 8;
    int filled = (utilization * bar_length) / 100;

    wattron(win, COLOR_PAIR(util_color));
    for (int j = 0; j < bar_length; j++) {
      if (j < filled) {
        mvwaddch(win, row, col + 61 + j, ACS_BLOCK);
      } else {
        mvwaddch(win, row, col + 61 + j, ' ');
      }
    }
    wattroff(win, COLOR_PAIR(util_color));
    mvwprintw(win, row, col + 69, "]");

    row++;
  }

  // Scroll indicator at bottom
  if (total_zones > (uint64_t)max_visible_zones) {
    wattron(win, A_DIM);
    mvwprintw(win, height - 2, col, "Showing zones %d-%llu of %llu",
              zone_scroll_offset,
              ((uint64_t)(zone_scroll_offset + max_visible_zones) < total_zones) ?
                (unsigned long long)(zone_scroll_offset + max_visible_zones - 1) : (unsigned long long)(total_zones - 1),
              (unsigned long long)total_zones);
    wattroff(win, A_DIM);
  }

  return 0;
}

/**
 * Clean up resources used by ZNS view
 */
static void view_zns_cleanup(void) {
  // Clean up any resources
  view_zns_initialized = 0;
  show_all_zones = 0;
  zone_scroll_offset = 0;
}