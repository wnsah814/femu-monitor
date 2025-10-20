/**
 * femonitor.h
 * FEMONITOR - FEMU Device Monitoring Tool Main Header
 */

#ifndef FEMONITOR_H
#define FEMONITOR_H

#include "common/ui_common.h"
#include "core/data_types.h"
#include "core/device_manager.h"
#include "core/nvme_cmd.h"
#include "views/view_manager.h"

// Version information
#define FEMONITOR_VERSION "1.0.0"
#define FEMONITOR_NAME "FEMONITOR"
#define FEMONITOR_DESCRIPTION "FEMU Device Monitoring Tool"

// Function prototypes
int femonitor_init(int argc, char *argv[]);
void femonitor_cleanup(void);

#endif /* FEMONITOR_H */