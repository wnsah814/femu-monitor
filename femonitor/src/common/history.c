/**
 * history.c
 * Data History Management Implementation
 */

#include "../../include/common/history.h"

#include <stdio.h>
#include <string.h>

/**
 * Update history data
 *
 * @param history History structure
 * @param value New value
 * @param name History dataset name (optional)
 */
void update_history(History *history, int value, const char *name) {
  // Update max value if needed
  if (value > history->max_value) {
    history->max_value = value;
  }

  // Store name if provided
  if (name && history->name[0] == '\0') {
    strncpy(history->name, name, sizeof(history->name) - 1);
    history->name[sizeof(history->name) - 1] = '\0';
  }

  // Add value to history
  if (history->count < HISTORY_SIZE) {
    history->data[history->count++] = value;
  } else {
    history->data[history->index] = value;
    history->index = (history->index + 1) % HISTORY_SIZE;
  }
}

/**
 * Initialize history set
 *
 * @param histories History set to initialize
 */
void history_set_init(history_set_t *histories) {
  // Clear all history data
  memset(histories, 0, sizeof(history_set_t));

  // Initialize history names
  strncpy(histories->free_line_history.name, "Free Lines",
          sizeof(histories->free_line_history.name) - 1);
  strncpy(histories->victim_line_history.name, "Victim Lines",
          sizeof(histories->victim_line_history.name) - 1);
  strncpy(histories->full_line_history.name, "Full Lines",
          sizeof(histories->full_line_history.name) - 1);
  strncpy(histories->rzone_history.name, "RZone Lines",
          sizeof(histories->rzone_history.name) - 1);
  strncpy(histories->active_rzone_history.name, "Active RZones",
          sizeof(histories->active_rzone_history.name) - 1);
  strncpy(histories->active_szone_history.name, "Active SZones",
          sizeof(histories->active_szone_history.name) - 1);

  // ZNSSD specific histories
  strncpy(histories->zns_slba_history.name, "ZNS SLBA",
          sizeof(histories->zns_slba_history.name) - 1);
  strncpy(histories->zns_capacity_history.name, "ZNS Capacity",
          sizeof(histories->zns_capacity_history.name) - 1);
}

/**
 * Reset history set
 *
 * @param histories History set to reset
 */
void history_set_reset(history_set_t *histories) {
    char names[13][32]; // Increased array size to accommodate new fields

    // Save names for general histories using snprintf for safety
    snprintf(names[0], sizeof(names[0]), "%s", histories->free_line_history.name);
    snprintf(names[1], sizeof(names[1]), "%s", histories->victim_line_history.name);
    snprintf(names[2], sizeof(names[2]), "%s", histories->full_line_history.name);
    snprintf(names[3], sizeof(names[3]), "%s", histories->rzone_history.name);
    snprintf(names[4], sizeof(names[4]), "%s", histories->active_rzone_history.name);
    snprintf(names[5], sizeof(names[5]), "%s", histories->active_szone_history.name);
    snprintf(names[6], sizeof(names[6]), "%s", histories->zns_slba_history.name);
    snprintf(names[7], sizeof(names[7]), "%s", histories->zns_capacity_history.name);

    // Clear all history data
    memset(histories, 0, sizeof(history_set_t));

    // Restore names for general histories using snprintf
    snprintf(histories->free_line_history.name, sizeof(histories->free_line_history.name), "%s", names[0]);
    snprintf(histories->victim_line_history.name, sizeof(histories->victim_line_history.name), "%s", names[1]);
    snprintf(histories->full_line_history.name, sizeof(histories->full_line_history.name), "%s", names[2]);
    snprintf(histories->rzone_history.name, sizeof(histories->rzone_history.name), "%s", names[3]);
    snprintf(histories->active_rzone_history.name, sizeof(histories->active_rzone_history.name), "%s", names[4]);
    snprintf(histories->active_szone_history.name, sizeof(histories->active_szone_history.name), "%s", names[5]);
    snprintf(histories->zns_slba_history.name, sizeof(histories->zns_slba_history.name), "%s", names[6]);
    snprintf(histories->zns_capacity_history.name, sizeof(histories->zns_capacity_history.name), "%s", names[7]);
}