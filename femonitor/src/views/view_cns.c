/**
 * view_cns.c
 * Multi-device View Implementation
 */

#include "../../include/views/view_interface.h"
#include "../../include/views/view_cns.h"

#include <ncurses.h>
#include <stdlib.h>
#include <string.h>

#include "../../include/common/ui_common.h"
#include "../../include/core/device_manager.h"
#include "../../include/modes/bbssd_mode.h"
#include "../../include/modes/multi_mode.h"
#include "../../include/modes/znssd_mode.h"

// Forward declarations of private functions
static int view_cns_init(void);
static int view_cns_render(WINDOW *win, device_manager_t *manager, int width,
                             int height);
static int view_cns_handle_input(int ch, device_manager_t *manager);
static void view_cns_cleanup(void);
static int render_all_lines_table(WINDOW *win, device_manager_t *manager, int width, int height);

// Local variables
static int view_cns_initialized = 0;
static int show_all_lines = 0;  // Toggle for showing all lines table
static int scroll_offset = 0;   // Scroll offset for all lines view

// View interface implementation
view_interface_t view_cns = {
    .name = "CNS_VIEW",
    .description = "Conventional SSD",
    .key = "1",
    .init = view_cns_init,
    .activate = view_interface_default_activate,
    .deactivate = view_interface_default_deactivate,
    .render = view_cns_render,
    .handle_input = view_cns_handle_input,
    .cleanup = view_cns_cleanup};

/**
 * Initialize cns view
 *
 * @return 0 on success, non-zero on failure
 */
static int view_cns_init(void) {
  if (view_cns_initialized) {
    return 0;
  }

  view_cns_initialized = 1;
  return 0;
}

/**
 * Render cns view
 *
 * @param win Window to render to
 * @param manager Device manager
 * @param width Window width
 * @param height Window height
 * @return 0 on success, non-zero on failure
 */
static int view_cns_render(WINDOW *win, device_manager_t *manager, int width,
                             int height) {
  // Check if we should show all lines table
  if (show_all_lines) {
    return render_all_lines_table(win, manager, width, height);
  }

  int row = 3;
  int col_left = 2;
  int col_right = width / 2 + 2;
  int half_width = width / 2 - 4;

  // Check if this is the correct mode
  if (manager->mode != MODE_MULTI) {
    mvwprintw(win, row, col_left,
              "CNS view is only available in BBSSD+ZNSSD mode");
    return 0;
  }

  // Get current device context
  device_context_t *device = &manager->devices[manager->current_device];

  // Check if current device is BBSSD type
  if (device->device_type != DEVICE_BBSSD) {
    mvwprintw(win, row, col_left,
              "CNS view requires BBSSD device (current device is not BBSSD)");
    return 0;
  }

  bbssd_data_t *bbssd_data = (bbssd_data_t *)device->device_data;
  if (!bbssd_data || !bbssd_data->lm_info) {
    mvwprintw(win, row, col_left, "No BBSSD data available");
    return 0;
  }

  // Title
  wattron(win, A_BOLD | COLOR_PAIR(COLOR_PAIR_CYAN));
  mvwprintw(win, row++, col_left, "Conventional SSD (CNS) Monitoring");
  wattroff(win, A_BOLD | COLOR_PAIR(COLOR_PAIR_CYAN));
  row++;

  // ========== LEFT COLUMN: Write Pointer & GC Info ==========

  // Write Pointer Information Box
  draw_box_with_title(win, row, col_left, 11, half_width, "Write Pointer Info",
                      COLOR_PAIR_CYAN);
  int box_row = row + 1;
  mvwprintw(win, box_row++, col_left + 2, "Current Line ID : %d",
            bbssd_data->wp_info.curline_id);
  mvwprintw(win, box_row++, col_left + 2, "Channel         : %d",
            bbssd_data->wp_info.ch);
  mvwprintw(win, box_row++, col_left + 2, "LUN             : %d",
            bbssd_data->wp_info.lun);
  mvwprintw(win, box_row++, col_left + 2, "Block           : %d",
            bbssd_data->wp_info.blk);
  mvwprintw(win, box_row++, col_left + 2, "Page            : %d",
            bbssd_data->wp_info.pg);
  mvwprintw(win, box_row++, col_left + 2, "Plane           : %d",
            bbssd_data->wp_info.pl);
  // Physical Address (combined representation)
  wattron(win, A_BOLD | COLOR_PAIR(COLOR_PAIR_YELLOW));
  mvwprintw(win, box_row++, col_left + 2, "Phys Addr       : Blk%d/Pg%d/Sec[ ]/Pl%d/LUN%d/Ch%d",
            bbssd_data->wp_info.blk, bbssd_data->wp_info.pg,
            bbssd_data->wp_info.pl, bbssd_data->wp_info.lun,
            bbssd_data->wp_info.ch
            );
  wattroff(win, A_BOLD | COLOR_PAIR(COLOR_PAIR_YELLOW));

  // Write Progress Indicator (assuming 256 pages per block)
  int pages_per_block = 256;
  int write_progress = (bbssd_data->wp_info.pg * 100) / pages_per_block;
  mvwprintw(win, box_row, col_left + 2, "Block Progress  : ");
  draw_progress_bar(win, box_row++, col_left + 20, 15, write_progress,
                    COLOR_PAIR_CYAN);

  row += 12;

  // Garbage Collection Statistics Box
  draw_box_with_title(win, row, col_left, 7, half_width, "Garbage Collection",
                      COLOR_PAIR_CYAN);
  box_row = row + 1;

  char gc_count_str[32], gc_pgs_str[32];
  format_number(bbssd_data->gc_count, gc_count_str, sizeof(gc_count_str));
  format_number(bbssd_data->gc_pgs, gc_pgs_str, sizeof(gc_pgs_str));

  mvwprintw(win, box_row++, col_left + 2, "GC Count        : %s", gc_count_str);
  mvwprintw(win, box_row++, col_left + 2, "Pages Moved     : %s", gc_pgs_str);

  // Calculate average pages per GC and add visual indicator
  if (bbssd_data->gc_count > 0) {
    double avg_pgs = (double)bbssd_data->gc_pgs / bbssd_data->gc_count;
    mvwprintw(win, box_row++, col_left + 2, "Avg Pages/GC    : %.1f", avg_pgs);

    // GC Activity level indicator
    int gc_activity_level = (avg_pgs > 200) ? 3 : (avg_pgs > 100) ? 2 : 1;
    mvwprintw(win, box_row, col_left + 2, "GC Activity     : ");
    const char* activity_text[] = {"Low", "Medium", "High"};
    int activity_color[] = {COLOR_PAIR_GREEN, COLOR_PAIR_YELLOW, COLOR_PAIR_RED};

    wattron(win, COLOR_PAIR(activity_color[gc_activity_level - 1]));
    mvwprintw(win, box_row++, col_left + 18, "%s", activity_text[gc_activity_level - 1]);
    wattroff(win, COLOR_PAIR(activity_color[gc_activity_level - 1]));
  } else {
    mvwprintw(win, box_row++, col_left + 2, "Avg Pages/GC    : N/A");
    mvwprintw(win, box_row++, col_left + 2, "GC Activity     : None");
  }

  row += 8;

  // ========== RIGHT COLUMN: Line Management ==========

  // Line Management Overview Box
  int right_row = 5;
  draw_box_with_title(win, right_row, col_right, 8, half_width,
                      "Line Management", COLOR_PAIR_CYAN);
  box_row = right_row + 1;

  int total_lines = bbssd_data->lm_info->tt_lines;
  int free_lines = bbssd_data->lm_info->free_line_cnt;
  int victim_lines = bbssd_data->lm_info->victim_line_cnt;
  int full_lines = bbssd_data->lm_info->full_line_cnt;

  mvwprintw(win, box_row++, col_right + 2, "Total Lines     : %d", total_lines);
  mvwprintw(win, box_row++, col_right + 2, "Free Lines      : %d", free_lines);
  mvwprintw(win, box_row++, col_right + 2, "Victim Lines    : %d", victim_lines);
  mvwprintw(win, box_row++, col_right + 2, "Full Lines      : %d", full_lines);

  // Calculate utilization percentage
  int used_lines = total_lines - free_lines;
  int utilization = (total_lines > 0) ? (used_lines * 100 / total_lines) : 0;
  mvwprintw(win, box_row++, col_right + 2, "Utilization     : %d%%", utilization);

  right_row += 9;

  // Line Distribution Bars
  draw_box_with_title(win, right_row, col_right, 9, half_width,
                      "Line Distribution", COLOR_PAIR_CYAN);
  box_row = right_row + 1;

  int bar_width = half_width - 20;

  // Free lines bar (Green - good)
  mvwprintw(win, box_row, col_right + 2, "Free  :");
  draw_bar(win, box_row++, col_right + 10, free_lines, total_lines,
           bar_width, COLOR_PAIR_GREEN);

  // Victim lines bar (Yellow - warning)
  mvwprintw(win, box_row, col_right + 2, "Victim:");
  draw_bar(win, box_row++, col_right + 10, victim_lines, total_lines,
           bar_width, COLOR_PAIR_YELLOW);

  // Full lines bar (Red - critical)
  mvwprintw(win, box_row, col_right + 2, "Full  :");
  draw_bar(win, box_row++, col_right + 10, full_lines, total_lines,
           bar_width, COLOR_PAIR_RED);

  right_row += 10;

  // ========== BOTTOM SECTION: Line Details & Health ==========

  // Line Details Table (showing top 5 victim lines by IPC)
  draw_box_with_title(win, right_row, col_right, 8, half_width,
                      "Top Victim Lines (by IPC)", COLOR_PAIR_CYAN);
  box_row = right_row + 1;

  // Sort and display top victim lines
  if (total_lines > 0 && bbssd_data->lm_info->lines) {
    mvwprintw(win, box_row++, col_right + 2, "Line  IPC  VPC  Efficiency");
    draw_separator(win, box_row++, col_right + 2, half_width - 4);

    // Display up to 5 lines with highest IPC
    int lines_shown = 0;
    for (int i = 0; i < total_lines && lines_shown < 5; i++) {
      bbssd_line_info_t *line = &bbssd_data->lm_info->lines[i];
      if (line->ipc > 0) {  // Only show lines with invalid pages
        int efficiency = (line->vpc + line->ipc > 0) ?
                        (line->vpc * 100 / (line->vpc + line->ipc)) : 0;

        // Color code by efficiency
        if (efficiency < 50) {
          wattron(win, COLOR_PAIR(COLOR_PAIR_RED));
        } else if (efficiency < 80) {
          wattron(win, COLOR_PAIR(COLOR_PAIR_YELLOW));
        } else {
          wattron(win, COLOR_PAIR(COLOR_PAIR_GREEN));
        }

        mvwprintw(win, box_row++, col_right + 2, "%4d %4d %4d      %2d%%",
                  line->id, line->ipc, line->vpc, efficiency);

        wattroff(win, COLOR_PAIR(COLOR_PAIR_RED) | COLOR_PAIR(COLOR_PAIR_YELLOW) | COLOR_PAIR(COLOR_PAIR_GREEN));
        lines_shown++;
      }
    }

    if (lines_shown == 0) {
      mvwprintw(win, box_row, col_right + 2, "No victim lines");
    }
  }

  right_row += 9;

  // Status indicators showing line health
  int status_row = right_row;

  // Free line status (inverted: lower is worse)
  int free_pct = (total_lines > 0) ? (free_lines * 100 / total_lines) : 0;
  draw_status_indicator(win, status_row, col_right + 2, free_pct,
                        20, 10, 1, "Free");

  // Victim line status
  int victim_pct = (total_lines > 0) ? (victim_lines * 100 / total_lines) : 0;
  draw_status_indicator(win, status_row, col_right + 15, victim_pct,
                        30, 50, 0, "Victim");

  status_row++;

  // ========== FULL BOTTOM SECTION: GC Efficiency & Activity ==========

  // GC Efficiency Box
  draw_box_with_title(win, row, col_left, 6, half_width, "GC Efficiency",
                      COLOR_PAIR_CYAN);
  box_row = row + 1;

  // Calculate GC efficiency metrics
  if (bbssd_data->gc_count > 0) {
    double avg_pages_per_gc = (double)bbssd_data->gc_pgs / bbssd_data->gc_count;
    double gc_efficiency = (avg_pages_per_gc > 0) ? (avg_pages_per_gc / 256.0) * 100 : 0;  // Assuming 256 pages per line

    mvwprintw(win, box_row++, col_left + 2, "Total GC Ops    : %s", gc_count_str);
    mvwprintw(win, box_row++, col_left + 2, "Pages Moved     : %s", gc_pgs_str);
    mvwprintw(win, box_row++, col_left + 2, "Avg Pages/GC    : %.1f", avg_pages_per_gc);

    // GC efficiency progress bar
    mvwprintw(win, box_row, col_left + 2, "GC Efficiency   : ");
    draw_progress_bar(win, box_row++, col_left + 20, 15, (int)gc_efficiency,
                      gc_efficiency > 70 ? COLOR_PAIR_GREEN :
                      (gc_efficiency > 40 ? COLOR_PAIR_YELLOW : COLOR_PAIR_RED));
  } else {
    mvwprintw(win, box_row++, col_left + 2, "No GC activity recorded");
  }

  // Bottom help text
  int bottom_row = height - 2;
  wattron(win, A_DIM);
  mvwprintw(win, bottom_row, col_left,
            "Press 'a' for all lines");
  wattroff(win, A_DIM);

  return 0;
}

/**
 * Handle input for multi-device view
 *
 * @param ch Input character
 * @param manager Device manager
 * @return 1 if the input was handled, 0 if not
 */
static int view_cns_handle_input(int ch, device_manager_t *manager) {
  // Handle view-specific input
  switch (ch) {
    case 'a':  // Toggle all lines view
    case 'A':
      show_all_lines = !show_all_lines;
      scroll_offset = 0;  // Reset scroll when toggling
      return 1;

    case ' ':  // Toggle grid
      manager->view_state.show_grid = !manager->view_state.show_grid;
      return 1;

    case KEY_UP:
      if (show_all_lines && scroll_offset > 0) {
        scroll_offset--;
      }
      return 1;

    case KEY_DOWN:
      if (show_all_lines) {
        scroll_offset++;
      }
      return 1;

    case KEY_PPAGE:
      if (show_all_lines && scroll_offset > 10) {
        scroll_offset -= 10;
      } else if (show_all_lines) {
        scroll_offset = 0;
      }
      return 1;

    case KEY_NPAGE:
      if (show_all_lines) {
        scroll_offset += 10;
      }
      return 1;
  }

  return 0;  // Input not handled
}

/**
 * Render all lines table view
 *
 * @param win Window to render to
 * @param manager Device manager
 * @param width Window width
 * @param height Window height
 * @return 0 on success, non-zero on failure
 */
static int render_all_lines_table(WINDOW *win, device_manager_t *manager, int width __attribute__((unused)), int height) {
  int row = 3;
  int col = 2;

  // Check if this is the correct mode
  if (manager->mode != MODE_MULTI) {
    mvwprintw(win, row, col, "CNS view is only available in BBSSD+ZNSSD mode");
    return 0;
  }

  // Get current device context
  device_context_t *device = &manager->devices[manager->current_device];

  // Check if current device is BBSSD type
  if (device->device_type != DEVICE_BBSSD) {
    mvwprintw(win, row, col, "CNS view requires BBSSD device (current device is not BBSSD)");
    return 0;
  }

  bbssd_data_t *bbssd_data = (bbssd_data_t *)device->device_data;
  if (!bbssd_data || !bbssd_data->lm_info) {
    mvwprintw(win, row, col, "No BBSSD data available");
    return 0;
  }

  // Title
  wattron(win, A_BOLD | COLOR_PAIR(COLOR_PAIR_CYAN));
  mvwprintw(win, row++, col, "All Lines Information (Total: %d)", bbssd_data->lm_info->tt_lines);
  wattroff(win, A_BOLD | COLOR_PAIR(COLOR_PAIR_CYAN));
  row++;

  // Help text
  wattron(win, A_DIM);
  mvwprintw(win, row++, col, "Press 'a' to return to summary view | Up/Down: Scroll | PgUp/PgDn: Page");
  wattroff(win, A_DIM);
  row++;

  // Table header
  wattron(win, A_BOLD | COLOR_PAIR(COLOR_PAIR_HEADER));
  mvwprintw(win, row, col, "%-6s %-4s %-4s %-6s %-10s %-8s", "Line", "IPC", "VPC", "Eff%", "State", "Visual");
  wattroff(win, A_BOLD | COLOR_PAIR(COLOR_PAIR_HEADER));
  row++;

  // Separator line
  mvwhline(win, row++, col, ACS_HLINE, 50);

  // Calculate max visible lines
  int max_visible_lines = height - row - 2;
  int total_lines = bbssd_data->lm_info->tt_lines;

  // Bound scroll offset
  if (scroll_offset > total_lines - max_visible_lines) {
    scroll_offset = (total_lines > max_visible_lines) ? total_lines - max_visible_lines : 0;
  }
  if (scroll_offset < 0) {
    scroll_offset = 0;
  }

  // Display lines data
  for (int i = scroll_offset; i < total_lines && i < scroll_offset + max_visible_lines; i++) {
    bbssd_line_info_t *line = &bbssd_data->lm_info->lines[i];

    // Calculate efficiency
    int total_pages = line->ipc + line->vpc;
    int efficiency = -1;  // -1 means N/A (not applicable)
    char efficiency_str[8];

    if (total_pages > 0) {
      efficiency = (line->vpc * 100 / total_pages);
      snprintf(efficiency_str, sizeof(efficiency_str), "%d%%", efficiency);
    } else {
      snprintf(efficiency_str, sizeof(efficiency_str), "N/A");
    }

    // Determine line state
    const char* state = "Unknown";
    int state_color = COLOR_PAIR_NORMAL;

    if (line->ipc == 0 && line->vpc == 0) {
      state = "Free";
      state_color = COLOR_PAIR_GREEN;
    } else if (line->ipc > 0 && line->vpc > 0) {
      state = "Victim";
      state_color = COLOR_PAIR_YELLOW;
    } else if (line->ipc == 0 && line->vpc > 0) {
      state = "Full";
      state_color = COLOR_PAIR_RED;
    }

    // Color based on efficiency (only if efficiency is valid)
    int eff_color = COLOR_PAIR_NORMAL;
    if (efficiency >= 0) {
      if (efficiency < 50) {
        eff_color = COLOR_PAIR_RED;
      } else if (efficiency < 80) {
        eff_color = COLOR_PAIR_YELLOW;
      } else {
        eff_color = COLOR_PAIR_GREEN;
      }
    }

    // Line number
    mvwprintw(win, row, col, "%-6d", line->id);

    // IPC (Invalid Page Count)
    wattron(win, COLOR_PAIR(line->ipc > 0 ? COLOR_PAIR_RED : COLOR_PAIR_NORMAL));
    mvwprintw(win, row, col + 7, "%-4d", line->ipc);
    wattroff(win, COLOR_PAIR(line->ipc > 0 ? COLOR_PAIR_RED : COLOR_PAIR_NORMAL));

    // VPC (Valid Page Count)
    wattron(win, COLOR_PAIR(line->vpc > 0 ? COLOR_PAIR_GREEN : COLOR_PAIR_NORMAL));
    mvwprintw(win, row, col + 12, "%-4d", line->vpc);
    wattroff(win, COLOR_PAIR(line->vpc > 0 ? COLOR_PAIR_GREEN : COLOR_PAIR_NORMAL));

    // Efficiency
    wattron(win, COLOR_PAIR(eff_color));
    mvwprintw(win, row, col + 17, "%-6s", efficiency_str);
    wattroff(win, COLOR_PAIR(eff_color));

    // State
    wattron(win, COLOR_PAIR(state_color));
    mvwprintw(win, row, col + 24, "%-10s", state);
    wattroff(win, COLOR_PAIR(state_color));

    // Visual representation (efficiency bar)
    mvwprintw(win, row, col + 35, "[");
    int bar_length = 8;

    if (efficiency >= 0) {
      // Valid efficiency: show colored bar
      int filled = (efficiency * bar_length) / 100;
      wattron(win, COLOR_PAIR(eff_color));
      for (int j = 0; j < bar_length; j++) {
        if (j < filled) {
          mvwaddch(win, row, col + 36 + j, ACS_BLOCK);
        } else {
          mvwaddch(win, row, col + 36 + j, ' ');
        }
      }
      wattroff(win, COLOR_PAIR(eff_color));
    } else {
      // N/A efficiency (free line): show dimmed dashes
      wattron(win, A_DIM);
      for (int j = 0; j < bar_length; j++) {
        mvwaddch(win, row, col + 36 + j, '-');
      }
      wattroff(win, A_DIM);
    }
    mvwprintw(win, row, col + 44, "]");

    row++;
  }

  // Scroll indicator at bottom
  if (total_lines > max_visible_lines) {
    wattron(win, A_DIM);
    mvwprintw(win, height - 2, col, "Showing lines %d-%d of %d",
              scroll_offset,
              (scroll_offset + max_visible_lines < total_lines) ? scroll_offset + max_visible_lines - 1 : total_lines - 1,
              total_lines);
    wattroff(win, A_DIM);
  }

  return 0;
}

/**
 * Clean up resources used by multi-device view
 */
static void view_cns_cleanup(void) {
  // Clean up any resources
  view_cns_initialized = 0;
  show_all_lines = 0;
  scroll_offset = 0;
}