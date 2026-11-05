/**
 * view_manager.c
 * View Manager Implementation
 */

#include "../../include/views/view_manager.h"

#include <ncurses.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "../../include/common/ui_common.h"
#include "../../include/views/dashboard_view.h"
#include "../../include/views/view_cns.h"
#include "../../include/views/view_zns.h"
#include "../../include/views/debug_view.h"
#include "../../include/views/log_view.h"

// Array of all available views
static view_interface_t *available_views[VIEW_COUNT + 1];

// Current active view index
static int active_view_index = 0;

/**
 * Initialize the view manager
 *
 * @param starting_view Index of the starting view
 * @return 0 on success, non-zero on failure
 */
int view_manager_init(int starting_view) {
  int i;

  // Initialize available views array
  for (i = 0; i < VIEW_COUNT + 1; i++) {
    available_views[i] = NULL;
  }

  // Register default views (will be filtered by mode later)
  available_views[VIEW_DASHBOARD] = &dashboard_view;
  available_views[VIEW_CNS] = &view_cns;
  available_views[VIEW_ZNS] = &view_zns;
  // available_views[VIEW_LINE] = &line_view;
  available_views[VIEW_DEBUG] = &debug_view;
  available_views[VIEW_LOG] = &log_view;  // Register log view

  // Initialize all views
  for (i = 0; i < VIEW_COUNT; i++) {
    if (available_views[i] && available_views[i]->init) {
      if (available_views[i]->init() != 0) {
        return -1;
      }
    }
  }

  // Validate starting view
  if (starting_view < 0 || starting_view >= VIEW_COUNT ||
      !available_views[starting_view]) {
    starting_view = 0;
  }

  // Set initial view
  active_view_index = starting_view;

  // Note: We don't activate the view here, it will be activated in main.c with
  // device_manager

  return 0;
}

/**
 * Register views based on mode
 *
 * @param mode Operating mode
 */
void view_manager_register_views(femonitor_mode_t mode) {
  // Selective view registration based on mode
  switch (mode) {
    case MODE_BBSSD:
      available_views[VIEW_LINE] = NULL;
      break;
    case MODE_ZNSSD:
      // ZNS-only mode: disable CNS view and line view
      available_views[VIEW_CNS] = NULL;
      available_views[VIEW_LINE] = NULL;
      break;
    case MODE_MULTI:
      available_views[VIEW_DASHBOARD] = NULL;
      available_views[VIEW_LINE] = NULL;
      break;

    default:
      // Default to all views
      break;
  }

  // Ensure active view is a valid one
  if (!available_views[active_view_index]) {
    // Find first available view
    for (int i = 0; i < VIEW_COUNT; i++) {
      if (available_views[i]) {
        active_view_index = i;
        break;
      }
    }
  }
}

/**
 * Switch to a different view
 *
 * @param view_index Index of the view to switch to
 * @param manager Device manager to pass to the activate function
 * @return 0 on success, non-zero on failure
 */
int view_manager_switch(int view_index, device_manager_t *manager) {
  // Validate view index
  if (view_index < 0 || view_index >= VIEW_COUNT ||
      !available_views[view_index]) {
    return -1;
  }

  // Deactivate current view
  if (available_views[active_view_index] &&
      available_views[active_view_index]->deactivate) {
    if (available_views[active_view_index]->deactivate() != 0) {
      return -1;
    }
  }

  // Set new active view
  active_view_index = view_index;

  // Activate new view
  if (available_views[active_view_index] &&
      available_views[active_view_index]->activate) {
    if (available_views[active_view_index]->activate(manager) != 0) {
      return -1;
    }
  }

  return 0;
}

/**
 * Get the number of available views
 *
 * @return Number of views
 */
int view_manager_count(void) {
  int count = 0;

  for (int i = 0; i < VIEW_COUNT; i++) {
    if (available_views[i]) {
      count++;
    }
  }

  return count;
}

/**
 * Get view by index
 *
 * @param index View index
 * @return Pointer to the view or NULL if not found
 */
const view_interface_t *view_manager_get(int index) {
  if (index < 0 || index >= VIEW_COUNT) {
    return NULL;
  }

  return available_views[index];
}

/**
 * Get current active view
 *
 * @return Pointer to the active view
 */
const view_interface_t *view_manager_get_active(void) {
  return available_views[active_view_index];
}

/**
 * Get current active view index
 *
 * @return Index of the active view
 */
int view_manager_get_active_index(void) { return active_view_index; }

/**
 * Handle input and route to the appropriate view
 *
 * @param ch Input character
 * @param manager Device manager
 * @return 1 if handled, 0 if not
 */
int view_manager_handle_input(int ch, device_manager_t *manager) {
  int i;

  // Check if input is for view switching
  for (i = 0; i < VIEW_COUNT; i++) {
    if (available_views[i] && available_views[i]->key &&
        ch == available_views[i]->key[0]) {
      view_manager_switch(i, manager);
      return 1;
    }
  }

  // Otherwise, let the current view handle it
  if (available_views[active_view_index] &&
      available_views[active_view_index]->handle_input) {
    return available_views[active_view_index]->handle_input(ch, manager);
  }

  return 0;
}

/**
 * Render the current view
 *
 * @param win Window to render to
 * @param manager Device manager
 * @param width Window width
 * @param height Window height
 * @return 0 on success, non-zero on failure
 */
int view_manager_render(WINDOW *win, device_manager_t *manager, int width,
                        int height) {
  // Call the active view's render function
  if (available_views[active_view_index] &&
      available_views[active_view_index]->render) {
    return available_views[active_view_index]->render(win, manager, width,
                                                      height);
  }

  return 0;
}

/**
 * Draw the common header with view selection menu
 *
 * @param win Window to draw on
 * @param manager Device manager
 * @param width Window width
 */
void view_manager_draw_header(WINDOW *win, device_manager_t *manager,
                              int width) {
  int i;
  time_t current_time;
  struct tm *time_info;
  char time_str[64];
  char header[256];
  char menu[512] = "";
  char *menu_ptr = menu;
  int menu_space = sizeof(menu);
  int chars;

  // Get currently selected device
  device_context_t *device = &manager->devices[manager->current_device];

  // Create header text based on mode
  const char *mode_str;
  switch (manager->mode) {
    case MODE_BBSSD:
      mode_str = "BBSSD";
      break;
    case MODE_ZNSSD:
      mode_str = "ZNSSD";
      break;
    case MODE_MULTI:
      mode_str = "BBSSD+ZNSSD";
      break;
    default:
      mode_str = "Unknown";
  }

  snprintf(header, sizeof(header), " FEMONITOR: %s Monitor - Device: %s ",
           mode_str, device->device_path);

  // Get current time
  time(&current_time);
  time_info = localtime(&current_time);
  strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", time_info);

  // Draw header
  wattron(win, COLOR_PAIR(COLOR_PAIR_HEADER));
  mvwhline(win, 0, 0, ' ', width);
  mvwprintw(win, 0, (width - strlen(header)) / 2, "%s", header);
  wattroff(win, COLOR_PAIR(COLOR_PAIR_HEADER));

  // Build complete menu string first
  chars = snprintf(menu_ptr, menu_space, " [q] Quit | [r] Refresh | Views: ");
  menu_ptr += chars;
  menu_space -= chars;

  for (i = 0; i < VIEW_COUNT; i++) {
    if (available_views[i] && menu_space > 0) {
      chars = snprintf(menu_ptr, menu_space, "[%s] %s ",
                       available_views[i]->key, available_views[i]->name);
      menu_ptr += chars;
      menu_space -= chars;
    }
  }

  // Draw the complete menu line
  wattron(win, COLOR_PAIR(COLOR_PAIR_NORMAL));
  mvwprintw(win, 1, 0, "%-*.*s", width, width, menu);
  wattroff(win, COLOR_PAIR(COLOR_PAIR_NORMAL));

  // Now highlight the active view name
  int x_pos = strlen(" [q] Quit | [r] Refresh | Views: ");
  for (i = 0; i < VIEW_COUNT; i++) {
    if (available_views[i]) {
      int key_len = strlen(available_views[i]->key);
      int name_len = strlen(available_views[i]->name);
      
      if (i == active_view_index) {
        // Highlight the active view name
        wattron(win, A_BOLD | COLOR_PAIR(COLOR_PAIR_NORMAL));
        mvwprintw(win, 1, x_pos + 1 + key_len + 2, "%s", available_views[i]->name);
        wattroff(win, A_BOLD | COLOR_PAIR(COLOR_PAIR_NORMAL));
        break;
      }
      
      // Move position: "[k] name "
      x_pos += 1 + key_len + 2 + name_len + 1;
    }
  }

  // Draw timestamp at the end
  mvwprintw(win, 1, width - strlen(time_str) - 1, "%s", time_str);
}

/**
 * Cleanup and free resources used by the view manager
 */
void view_manager_cleanup(void) {
  int i;

  // Clean up all views
  for (i = 0; i < VIEW_COUNT; i++) {
    if (available_views[i] && available_views[i]->cleanup) {
      available_views[i]->cleanup();
    }
  }
}