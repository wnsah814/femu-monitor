/**
 * femon_generic.h
 * Schema-driven generic metric client (0xD1 SCHEMA / 0xD2 FETCH).
 *
 * This client learns the metric
 * set from the device at runtime: metrics registered on the device side
 * appear here with no tool change.
 */
#ifndef FEMON_GENERIC_H
#define FEMON_GENERIC_H

/* Print the device's metric schema as a table. Returns 0 on success. */
#include <stdint.h>
#include "femon_protocol.h"

/* 0xD1/0xD2 snapshot: the schema (cached per path) and all metric values. The view
 * models of the TUI, logging and snapshot modes (bbssd_data_t / znssd_data_t) take values from it by name. */
typedef struct {
  femon_schema_hdr_t hdr;
  femon_schema_rec_t *recs;   /* hdr.count records (owned by the cache; do not free) */
  uint64_t **values;          /* u64 array per id (freed by femon_snapshot_free) */
} femon_snapshot_t;

int femon_snapshot_fetch(const char *device_path, femon_snapshot_t *snap);
const uint64_t *femon_snapshot_get(const femon_snapshot_t *snap, const char *name,
                                   uint32_t *nvals);
void femon_snapshot_free(femon_snapshot_t *snap);

int femon_generic_list(const char *device_path);

/* Fetch all metrics and print "name,value" CSV lines (arrays flattened
 * as name[i] / name[i][j][k]). Returns 0 on success. */
int femon_generic_dump(const char *device_path);

/* Enable/disable tracepoint histograms on the device (0xD3, all metrics). */
int femon_generic_trace(const char *device_path, int enable);

/* Fetch and render all histogram-type metrics as ASCII bar charts. */
int femon_generic_hist(const char *device_path);

#endif /* FEMON_GENERIC_H */
