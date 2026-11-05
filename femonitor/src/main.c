/**
 * main.c
 * FEMONITOR Main Application
 */

#include <getopt.h>
#include <ncurses.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "../include/common/config.h"
#include "../include/common/global_state.h"
#include "../include/common/ui_common.h"
#include "../include/core/device_manager.h"
#include "../include/core/femon_generic.h"
#include "../include/femonitor.h"
#include "../include/modes/znssd_mode.h"
#include "../include/views/debug_view.h"
#include "../include/views/view_manager.h"

// Global variables for signal handling
static int g_running = 1;
static WINDOW *g_win = NULL;
device_manager_t *g_device_manager = NULL;

/**
 * Parse command line arguments
 *
 * @param argc Argument count
 * @param argv Argument array
 * @param options Options structure to store results
 */
void parse_args(int argc, char *argv[], femonitor_options_t *options) {
  int opt;
  static struct option long_options[] = {
      {"device", required_argument, 0, 'd'},
      {"device2", required_argument, 0, '2'},
      {"mode", required_argument, 0, 'm'},
      {"interval", required_argument, 0, 'i'},
      {"view", required_argument, 0, 'v'},
      {"no-cursor", no_argument, 0, 'c'},
      {"debug", no_argument, 0, 'D'},
      {"help", no_argument, 0, 'h'},
      {"start-log", no_argument, 0, 'L'},
      {"log-mode", required_argument, 0, 'M'},
      {"log-dir", required_argument, 0, 'O'},
      {"snapshot", no_argument, 0, 'S'},
      {"reset", no_argument, 0, 'R'},
      {"list", no_argument, 0, 'l'},
      {"generic", no_argument, 0, 'G'},
      {"trace", required_argument, 0, 'T'},
      {"hist", no_argument, 0, 'H'},
      {0, 0, 0, 0}};

  // Set default values
  options->device_count = 2;
  options->current_device = 0;
  options->mode = MODE_MULTI;
  options->interval = DEFAULT_INTERVAL;
  options->starting_view = 0;
  options->hide_cursor = 1;
  options->debug_mode = 0;
  options->auto_start_log = 0;  // Default: don't auto-start logging
  options->log_mode = 0;  // Default: aggregate mode (0=aggregate, 1=individual)
  strcpy(options->log_directory, "logs");  // Default to logs subdirectory
  options->snapshot_mode = 0;  // Default: interactive TUI mode
  options->reset_stats = 0;    // Default: don't reset stats
  options->list_mode = 0;      // Default: no schema listing
  options->generic_mode = 0;   // Default: no generic dump
  options->trace_ctl = -1;     // Default: don't touch tracepoints
  options->hist_mode = 0;      // Default: no histogram rendering

  // Set default device
  strncpy(options->devices[0], DEFAULT_CNS_DEVICE, MAX_DEVICE_PATH - 1);
  strncpy(options->devices[1], DEFAULT_ZNS_DEVICE, MAX_DEVICE_PATH - 1);
  options->devices[0][MAX_DEVICE_PATH - 1] = '\0';

  // Parse arguments
  int explicit_second = 0;
  while ((opt = getopt_long(argc, argv, "d:2:m:i:v:cDhLM:O:SRlGT:H",
                            long_options, NULL)) != -1) {
    switch (opt) {
      case 'd':  // Device path (alone: one device; add a second with -2)
        strncpy(options->devices[0], optarg, MAX_DEVICE_PATH - 1);
        options->devices[0][MAX_DEVICE_PATH - 1] = '\0';
        if (!explicit_second) options->device_count = 1;
        break;

      case '2':  // Second device for multi-mode
        explicit_second = 1;
        options->device_count = 2;
        strncpy(options->devices[1], optarg, MAX_DEVICE_PATH - 1);
        options->devices[1][MAX_DEVICE_PATH - 1] = '\0';
        break;

      case 'm':  // Operating mode
        if (strcmp(optarg, "cns") == 0) {
          options->mode = MODE_BBSSD;
          options->device_count = 1;
        } else if (strcmp(optarg, "zns") == 0) {
          options->mode = MODE_ZNSSD;
          options->device_count = 1;
        } else if (strcmp(optarg, "multi") == 0) {
          options->mode = MODE_MULTI;
          if (options->device_count < 2) {
            options->device_count = 2;
            strncpy(options->devices[1], "/dev/nvme1n1", MAX_DEVICE_PATH - 1);
            options->devices[1][MAX_DEVICE_PATH - 1] = '\0';
          }
        } else {
          fprintf(stderr, "Unknown mode: %s. Using default.\n", optarg);
        }
        break;

      case 'i':  // Update interval
        options->interval = atof(optarg);
        if (options->interval <= 0.0) {
          options->interval = DEFAULT_INTERVAL;
        }
        break;

      case 'v':  // View index
        options->starting_view = atoi(optarg);
        break;

      case 'c':  // Cursor visibility
        options->hide_cursor = 1;
        break;

      case 'D':  // Debug mode
        options->debug_mode = 1;
        break;

      case 'L':  // Start logging immediately
        options->auto_start_log = 1;
        options->starting_view = VIEW_LOG;  // Set view to logging view
        break;

      case 'M':  // Log mode
        if (strcmp(optarg, "individual") == 0) {
          options->log_mode = 1;  // Individual mode
        } else if (strcmp(optarg, "aggregate") == 0) {
          options->log_mode = 0;  // Aggregate mode
        } else {
          fprintf(stderr, "Unknown log mode: %s. Using default (aggregate).\n",
                  optarg);
        }
        break;
      case 'O':  // Log directory
        strncpy(options->log_directory, optarg, MAX_DEVICE_PATH - 1);
        options->log_directory[MAX_DEVICE_PATH - 1] = '\0';
        break;
      case 'S':  // Snapshot mode
        options->snapshot_mode = 1;
        break;
      case 'R':  // Reset statistics
        options->reset_stats = 1;
        break;
      case 'l':  // List registry schema
        options->list_mode = 1;
        break;
      case 'G':  // Generic registry dump
        options->generic_mode = 1;
        break;
      case 'T':  // Tracepoint control
        if (strcmp(optarg, "on") == 0) {
          options->trace_ctl = 1;
        } else if (strcmp(optarg, "off") == 0) {
          options->trace_ctl = 0;
        } else {
          fprintf(stderr, "Invalid --trace value: %s (use on|off)\n", optarg);
          exit(EXIT_FAILURE);
        }
        break;
      case 'H':  // Histogram rendering
        options->hist_mode = 1;
        break;
      case 'h':  // Help
      default:
        printf("FEMONITOR - Advanced SSD Monitoring Tool\n\n");
        printf("Usage: %s [options]\n\n", argv[0]);
        printf("Options:\n");
        printf(
            "  -m, --mode=MODE      Operating mode: multi, cns, zns "
            "(default: multi)\n");
        printf("  -d, --device=DEV     Device to monitor (default: %s)\n",
               DEFAULT_CNS_DEVICE);
        printf("  -2, --device2=DEV    Second device for multi-device mode (default: %s)\n",
               DEFAULT_ZNS_DEVICE);
        printf(
            "  -i, --interval=SEC   Update interval in seconds (default: "
            "%.1f)\n",
            DEFAULT_INTERVAL);
        printf("  -v, --view=NUM       Starting view number (default: 0)\n");
        printf("  -c, --no-cursor      Hide cursor (default: on)\n");
        printf("  -D, --debug          Enable debug mode\n");
        printf(
            "  -L, --start-log      Auto-start logging (switches to log "
            "view)\n");
        printf(
            "  -M, --log-mode=MODE  Log mode: aggregate, individual (default: "
            "aggregate)\n");
        printf(
            "  -O, --log-dir=DIR    Directory to store log files (default: "
            "logs)\n");
        printf(
            "  -S, --snapshot       Snapshot mode: single capture, CSV to "
            "stdout, exit\n");
        printf(
            "  -R, --reset          Reset monitoring statistics before "
            "starting\n");
        printf(
            "  -l, --list           List the device's metric schema (0xD1) "
            "and exit\n");
        printf(
            "  -G, --generic        Dump all registry metrics as CSV (0xD2) "
            "and exit\n");
        printf(
            "  -T, --trace=on|off   Enable/disable tracepoint histograms "
            "(0xD3) and exit\n");
        printf(
            "  -H, --hist           Render histogram metrics as ASCII bars "
            "and exit\n");
        printf("  -h, --help           Show this help message\n");
        exit(opt == 'h' ? EXIT_SUCCESS : EXIT_FAILURE);
    }
  }
}

/**
 * Signal handler for clean termination
 *
 * @param sig Signal number
 */
void signal_handler(int sig) {
  g_running = 0;

  // Clean up ncurses
  if (g_win) {
    endwin();
  }

  // Clean up device manager
  if (g_device_manager) {
    device_manager_cleanup(g_device_manager);
  }

  exit(sig);
}

/**
 * Signal handler for logging control
 * Handles SIGUSR1 to stop logging and SIGUSR2 to start logging
 *
 * @param sig Signal number
 */
void log_signal_handler(int sig) {
  if (sig == SIGUSR1) {
    stop_logging_if_active();
  } else if (sig == SIGUSR2) {
    start_logging_if_inactive();
  }
}

/**
 * Output ZNSSD data in CSV format for snapshot mode
 *
 * @param device Device context
 */
/* CNS snapshot: write pointer, GC, DSM and line summary + per-line VPC/IPC/pos CSV */
void snapshot_output_bbssd(device_context_t *device) {
  bbssd_data_t *data = (bbssd_data_t *)device->device_data;
  if (!data || !data->lm_info) {
    fprintf(stderr, "Error: No BBSSD data available\n");
    return;
  }
  struct timespec snap_ts;
  clock_gettime(CLOCK_REALTIME, &snap_ts);
  printf("# BBSSD Snapshot: %s\n", device->device_path);
  printf("# timestamp_ns=%lld.%09ld\n", (long long)snap_ts.tv_sec, snap_ts.tv_nsec);
  printf("# wp_line=%d, wp_ch=%d, wp_lun=%d, wp_pg=%d, wp_blk=%d, wp_pl=%d\n",
         data->wp_info.curline_id, data->wp_info.ch, data->wp_info.lun,
         data->wp_info.pg, data->wp_info.blk, data->wp_info.pl);
  printf("# gc_count=%lu, gc_pgs=%lu\n", data->gc_count, data->gc_pgs);
  printf("# tt_lines=%d, free=%d, victim=%d, full=%d\n", data->lm_info->tt_lines,
         data->lm_info->free_line_cnt, data->lm_info->victim_line_cnt,
         data->lm_info->full_line_cnt);
  printf("\n# Line Data\n");
  printf("line_id,vpc,ipc,victim_pos\n");
  for (int i = 0; i < data->lm_info->tt_lines; i++) {
    printf("%d,%d,%d,%lu\n", data->lm_info->lines[i].id, data->lm_info->lines[i].vpc,
           data->lm_info->lines[i].ipc, data->lm_info->lines[i].pos);
  }
}

void snapshot_output_znssd(device_context_t *device) {
  znssd_data_t *data = (znssd_data_t *)device->device_data;

  if (!data) {
    fprintf(stderr, "Error: No ZNSSD data available\n");
    return;
  }

  // Print header (with nanosecond capture timestamp for trace correlation)
  struct timespec snap_ts;
  clock_gettime(CLOCK_REALTIME, &snap_ts);
  printf("# ZNSSD Snapshot: %s\n", device->device_path);
  printf("# timestamp_ns=%lld.%09ld\n", (long long)snap_ts.tv_sec,
         snap_ts.tv_nsec);
  printf("# num_zones=%lu, num_channels=%lu, num_planes=%lu, zr_count=%lu\n",
         data->num_zones, data->num_channels, data->num_planes,
         data->zr_count);
  printf("# wc_hit=%lu, wc_miss=%lu, wc_evict=%lu, wc_flush=%lu\n",
         data->wc_hit, data->wc_miss, data->wc_evict, data->wc_flush);

  // Zone data header and rows
  printf("\n# Zone Data\n");
  printf("zone_id,zone_type,zone_state,zone_cap,zslba,write_ptr,cnt_reset\n");
  for (uint64_t i = 0; i < data->num_zones; i++) {
    printf("%lu,%u,%u,%lu,%lu,%lu,%lu\n", i, data->znssd_data[i].d.zt,
           data->znssd_data[i].d.zs, data->znssd_data[i].d.zcap,
           data->znssd_data[i].d.zslba, data->znssd_data[i].w_ptr,
           data->znssd_data[i].cnt_reset);
  }

  // Channel statistics (if available)
  if (data->channel_stats && data->num_channels > 0) {
    printf("\n# Channel Statistics\n");
    printf("channel_id,total_xfer_count,conflict_count,total_wait_time_ns\n");
    for (uint64_t i = 0; i < data->num_channels; i++) {
      printf("%lu,%lu,%lu,%lu\n", i, data->channel_stats[i].total_xfer_count,
             data->channel_stats[i].conflict_count,
             data->channel_stats[i].total_wait_time_ns);
    }
  }

  // Plane statistics (if available)
  if (data->plane_stats && data->num_planes > 0) {
    printf("\n# Plane Statistics\n");
    printf("plane_id,total_io_count,conflict_count,total_wait_time_ns\n");
    for (uint64_t i = 0; i < data->num_planes; i++) {
      printf("%lu,%lu,%lu,%lu\n", i, data->plane_stats[i].total_io_count,
             data->plane_stats[i].conflict_count,
             data->plane_stats[i].total_wait_time_ns);
    }
  }
}

/**
 * Run snapshot mode - single data capture and output
 *
 * @param options Command line options
 * @return Exit code
 */
int run_snapshot_mode(femonitor_options_t *options) {
  device_manager_t *manager;

  // Initialize device manager
  manager = device_manager_init(options);
  if (!manager) {
    fprintf(stderr, "Error: Failed to initialize device manager\n");
    return EXIT_FAILURE;
  }

  // Reset statistics if requested
  if (options->reset_stats) {
    for (int i = 0; i < manager->device_count; i++) {
      device_context_t *device = &manager->devices[i];
      if (nvme_reset_stats(device->device_path) == 0) {
        fprintf(stderr, "Reset statistics on %s\n", device->device_path);
      }
    }
  }

  // Update device data once
  if (device_manager_update_data(manager) != 0) {
    fprintf(stderr, "Error: Failed to update device data\n");
    device_manager_cleanup(manager);
    return EXIT_FAILURE;
  }

  // Output data based on mode
  for (int i = 0; i < manager->device_count; i++) {
    device_context_t *device = &manager->devices[i];

    if (device->device_type == DEVICE_ZNSSD) {
      snapshot_output_znssd(device);
    } else if (device->device_type == DEVICE_BBSSD) {
      snapshot_output_bbssd(device);
    }
    // BBSSD support can be added here if needed
  }

  // Cleanup
  device_manager_cleanup(manager);
  return EXIT_SUCCESS;
}

/**
 * Main function
 *
 * @param argc Argument count
 * @param argv Argument array
 * @return Exit code
 */
int main(int argc, char *argv[]) {
  femonitor_options_t options;
  WINDOW *win;
  struct timespec delay;
  int ch;
  int width, height;

  // Parse command line arguments
  parse_args(argc, argv, &options);

  // Handle registry modes (non-interactive, schema-driven — no fixed format)
  if (options.list_mode || options.generic_mode || options.trace_ctl >= 0 ||
      options.hist_mode) {
    const char *dev = options.devices[0];
    int rc = 0;

    if (options.reset_stats && nvme_reset_stats(dev) == 0) {
      fprintf(stderr, "Reset statistics on %s\n", dev);
    }
    if (options.trace_ctl >= 0) {
      rc |= femon_generic_trace(dev, options.trace_ctl);
    }
    if (options.list_mode) {
      rc |= femon_generic_list(dev);
    }
    if (options.generic_mode) {
      rc |= femon_generic_dump(dev);
    }
    if (options.hist_mode) {
      rc |= femon_generic_hist(dev);
    }
    return rc ? EXIT_FAILURE : EXIT_SUCCESS;
  }

  // Handle snapshot mode (non-interactive)
  if (options.snapshot_mode) {
    return run_snapshot_mode(&options);
  }

  // Setup signal handlers
  signal(SIGINT, signal_handler);
  signal(SIGTERM, signal_handler);
  signal(SIGUSR1, log_signal_handler);  // Handler for stopping logging
  signal(SIGUSR2, log_signal_handler);  // Handler for starting logging

  // Initialize debug view if debug mode is enabled
  if (options.debug_mode) {
    debug_view_init();
  }

  // Initialize device manager
  g_device_manager = device_manager_init(&options);
  if (!g_device_manager) {
    fprintf(stderr, "Failed to initialize device manager\n");
    return EXIT_FAILURE;
  }

  // Reset statistics if requested
  if (options.reset_stats) {
    for (int i = 0; i < g_device_manager->device_count; i++) {
      device_context_t *device = &g_device_manager->devices[i];
      nvme_reset_stats(device->device_path);
    }
  }

  // Initialize ncurses
  win = initscr();
  g_win = win;
  if (!win) {
    fprintf(stderr, "Failed to initialize ncurses\n");
    device_manager_cleanup(g_device_manager);
    return EXIT_FAILURE;
  }

  // Configure ncurses
  cbreak();           // Disable line buffering
  noecho();           // Don't echo input
  keypad(win, TRUE);  // Enable function keys, arrow keys, etc.
  if (options.hide_cursor) {
    curs_set(0);  // Hide cursor
  }
  timeout(10);  // Non-blocking getch with 10ms delay

  // Initialize colors
  init_colors();

  // Initialize view manager with view 0 first
  if (view_manager_init(0) != 0) {
    endwin();
    fprintf(stderr, "Failed to initialize view manager\n");
    device_manager_cleanup(g_device_manager);
    return EXIT_FAILURE;
  }

  // Register views based on mode
  view_manager_register_views(options.mode);

  // Switch to the selected starting view (this will trigger activation with the
  // device manager)
  view_manager_switch(options.starting_view, g_device_manager);

  // Main loop
  while (g_running) {
    // Get current window dimensions
    getmaxyx(win, height, width);

    // Update device data
    if (device_manager_update_data(g_device_manager) == 0) {
      // Clear screen
      clear();

      // Draw header
      view_manager_draw_header(win, g_device_manager, width);

      // Render current view
      view_manager_render(win, g_device_manager, width, height);

      // Refresh the screen
      refresh();
    } else {
      // Error updating device data
      clear();
      mvprintw(
          1, 1,
          "Error updating device data. Press 'q' to quit or 'r' to retry.");
      refresh();
    }

    // Handle input
    ch = getch();
    if (ch == 'q' || ch == 'Q') {
      g_running = 0;
    } else if (ch == 'r' || ch == 'R') {
      // Just continue loop to retry
      continue;
    } else if (ch == KEY_RESIZE) {
      // Handle terminal resize
      clear();
      refresh();
    } else {
      // Let the view manager handle the input
      view_manager_handle_input(ch, g_device_manager);
    }

    // Sleep for the specified interval
    delay.tv_sec = (time_t)options.interval;
    delay.tv_nsec = (long)((options.interval - delay.tv_sec) * 1000000000L);
    nanosleep(&delay, NULL);
  }

  // Cleanup
  view_manager_cleanup();
  endwin();
  device_manager_cleanup(g_device_manager);

  return EXIT_SUCCESS;
}