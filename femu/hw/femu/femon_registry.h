/*
 * femon_registry.h — registration-based monitoring metric registry
 *
 * A backend (FTL) registers metrics by name at init; the host tool then
 * reads their schema and values, self-described, through 0xD1/0xD2
 * (femon_protocol.h). Exposing a new metric takes one registration line and
 * no change to the protocol or the host tool.
 *
 * Threading: registration happens only in realize (single thread). Queries
 * read the counters asynchronously from the admin path; for statistics a
 * torn read is limited to one u64, so no lock is taken.
 */
#ifndef FEMON_REGISTRY_H
#define FEMON_REGISTRY_H

#include <stdint.h>
#include <stddef.h>
#include "femon_protocol.h"

#define FEMON_REG_MAX_METRICS 64

/* array or derived metrics: write exactly nvals u64 values to out */
typedef void (*femon_fill_fn)(void *opaque, uint64_t *out);

/* ---- static tracepoints: in-place log2 histograms ----
 * Lock-free, assuming a single writer (the one thread that runs the path).
 * When disabled the cost is one inlined, predictable branch (the "disabled
 * probe effect ~0" principle of DTrace); recording goes to an out-of-line
 * slow path so more tracepoints do not pollute the hot-path i-cache.
 * bucket through max are laid out contiguously and go on the wire as is
 * (the FEMON_HIST_NVALS layout in femon_protocol.h). */
typedef struct femon_hist {
    uint64_t enabled;                        /* switched at run time by 0xD3 */
    uint64_t bucket[FEMON_HIST_BUCKETS];     /* log2 µs */
    uint64_t n;                              /* exact statistics below, in ns */
    uint64_t sum;
    uint64_t min;
    uint64_t max;
} femon_hist_t;

void femon_hist_record_slow(femon_hist_t *h, uint64_t ns);

static inline void femon_hist_record(femon_hist_t *h, uint64_t ns)
{
    if (__builtin_expect(!h->enabled, 1)) {
        return;
    }
    femon_hist_record_slow(h, ns);
}

typedef struct femon_metric {
    char     name[FEMON_NAME_LEN];
    uint8_t  type;                  /* FEMON_MT_* */
    uint8_t  ndims;
    uint32_t dims[FEMON_MAX_DIMS];
    uint32_t nvals;                 /* product of dims (scalar = 1) */
    uint64_t *ptr;                  /* direct view of a contiguous u64 array (or fill) */
    femon_fill_fn fill;
    void     *opaque;
    femon_hist_t *hist;             /* FEMON_MT_HIST: the histogram 0xD3 controls */
} femon_metric_t;

typedef struct femon_registry {
    uint32_t version;               /* bumped on every registration: schema generation */
    uint32_t count;
    femon_metric_t m[FEMON_REG_MAX_METRICS];
    /* backend hook for 0xD4 RESET: zeroes scattered counters (e.g. the sources of fill arrays) */
    void (*reset_hook)(void *opaque);
    void *reset_opaque;
} femon_registry_t;

femon_registry_t *femon_registry_new(void);

/* scalar u64 counter: exposed by pointer */
int femon_register_u64(femon_registry_t *reg, const char *name, uint64_t *ptr);

/* array metric: the fill callback writes nvals (= product of dims) values at query time */
int femon_register_arr(femon_registry_t *reg, const char *name, uint8_t type,
                       uint8_t ndims, const uint32_t *dims,
                       femon_fill_fn fill, void *opaque);

/* histogram: buckets exposed by pointer, and registered for 0xD3 on/off */
int femon_register_hist(femon_registry_t *reg, const char *name,
                        femon_hist_t *h);

/* 0xD3: turn on/off the histograms selected by sel (FEMON_SEL_ALL or an id) */
void femon_registry_trace_ctl(femon_registry_t *reg, uint32_t sel, int enable);

/* zero the buckets of every registered histogram */
void femon_registry_reset_hists(femon_registry_t *reg);

/* 0xD4 RESET: zero pointer-exposed u64 counters (scalar and array), clear histograms,
 * and call the backend reset_hook. Fill-callback metrics (gauges, state) are untouched. */
void femon_registry_reset(femon_registry_t *reg);
void femon_registry_set_reset_hook(femon_registry_t *reg,
                                   void (*hook)(void *), void *opaque);

/* 0xD1/0xD2 payload encoding (used by nvme-admin.c) */
size_t femon_schema_bytes(const femon_registry_t *reg);
void   femon_schema_encode(const femon_registry_t *reg, uint8_t *buf);
size_t femon_fetch_bytes(const femon_registry_t *reg, uint32_t sel);
void   femon_fetch_encode(const femon_registry_t *reg, uint32_t sel,
                          uint8_t *buf);

#endif /* FEMON_REGISTRY_H */
