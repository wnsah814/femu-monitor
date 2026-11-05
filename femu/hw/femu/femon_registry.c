/*
 * femon_registry.c — registration-based monitoring metric registry
 */
#include "qemu/osdep.h"
#include <string.h>
#include "femon_registry.h"

femon_registry_t *femon_registry_new(void)
{
    return g_malloc0(sizeof(femon_registry_t));
}

static femon_metric_t *femon_alloc_metric(femon_registry_t *reg,
                                          const char *name)
{
    femon_metric_t *m;

    if (reg->count >= FEMON_REG_MAX_METRICS) {
        return NULL;
    }
    m = &reg->m[reg->count];
    if (strlen(name) >= FEMON_NAME_LEN) {
        return NULL;
    }
    g_strlcpy(m->name, name, sizeof(m->name));
    reg->count++;
    reg->version++;
    return m;
}

int femon_register_u64(femon_registry_t *reg, const char *name, uint64_t *ptr)
{
    femon_metric_t *m = femon_alloc_metric(reg, name);

    if (!m) {
        return -1;
    }
    m->type = FEMON_MT_U64;
    m->ndims = 0;
    m->nvals = 1;
    m->ptr = ptr;
    return 0;
}

int femon_register_arr(femon_registry_t *reg, const char *name, uint8_t type,
                       uint8_t ndims, const uint32_t *dims,
                       femon_fill_fn fill, void *opaque)
{
    femon_metric_t *m;
    uint32_t nvals = 1;
    int i;

    if (ndims == 0 || ndims > FEMON_MAX_DIMS || !fill) {
        return -1;
    }
    m = femon_alloc_metric(reg, name);
    if (!m) {
        return -1;
    }
    m->type = type;
    m->ndims = ndims;
    for (i = 0; i < ndims; i++) {
        m->dims[i] = dims[i];
        nvals *= dims[i];
    }
    m->nvals = nvals;
    m->fill = fill;
    m->opaque = opaque;
    return 0;
}

void femon_hist_record_slow(femon_hist_t *h, uint64_t ns)
{
    uint64_t us = ns / 1000;
    int idx = us ? (63 - __builtin_clzll(us)) : 0;

    if (idx >= FEMON_HIST_BUCKETS) {
        idx = FEMON_HIST_BUCKETS - 1;
    }
    h->bucket[idx]++;
    if (h->n == 0 || ns < h->min) {
        h->min = ns;
    }
    if (ns > h->max) {
        h->max = ns;
    }
    h->n++;
    h->sum += ns;
}

int femon_register_hist(femon_registry_t *reg, const char *name,
                        femon_hist_t *h)
{
    femon_metric_t *m = femon_alloc_metric(reg, name);

    if (!m) {
        return -1;
    }
    m->type = FEMON_MT_HIST;
    m->ndims = 1;
    m->dims[0] = FEMON_HIST_NVALS;
    m->nvals = FEMON_HIST_NVALS;
    m->ptr = h->bucket;   /* bucket..max exposed contiguously (see the header) */
    m->hist = h;
    return 0;
}

void femon_registry_trace_ctl(femon_registry_t *reg, uint32_t sel, int enable)
{
    uint32_t i;

    for (i = 0; i < reg->count; i++) {
        if (reg->m[i].hist && (sel == FEMON_SEL_ALL || sel == i)) {
            reg->m[i].hist->enabled = enable ? 1 : 0;
        }
    }
}

void femon_registry_reset_hists(femon_registry_t *reg)
{
    uint32_t i;

    for (i = 0; i < reg->count; i++) {
        femon_hist_t *h = reg->m[i].hist;

        if (h) {
            /* keep enabled; zero from bucket through the statistics */
            memset(h->bucket, 0, sizeof(*h) - offsetof(femon_hist_t, bucket));
        }
    }
}

size_t femon_schema_bytes(const femon_registry_t *reg)
{
    return sizeof(femon_schema_hdr_t) +
           (size_t)reg->count * sizeof(femon_schema_rec_t);
}

void femon_schema_encode(const femon_registry_t *reg, uint8_t *buf)
{
    femon_schema_hdr_t *hdr = (femon_schema_hdr_t *)buf;
    femon_schema_rec_t *rec = (femon_schema_rec_t *)(buf + sizeof(*hdr));
    uint32_t i, d;

    hdr->version = reg->version;
    hdr->count = reg->count;
    for (i = 0; i < reg->count; i++) {
        const femon_metric_t *m = &reg->m[i];

        memset(&rec[i], 0, sizeof(rec[i]));
        rec[i].id = i;
        rec[i].type = m->type;
        rec[i].ndims = m->ndims;
        for (d = 0; d < m->ndims; d++) {
            rec[i].dims[d] = m->dims[d];
        }
        g_strlcpy(rec[i].name, m->name, sizeof(rec[i].name));
    }
}

size_t femon_fetch_bytes(const femon_registry_t *reg, uint32_t sel)
{
    size_t sz = sizeof(femon_fetch_hdr_t);
    uint32_t i;

    for (i = 0; i < reg->count; i++) {
        if (sel != FEMON_SEL_ALL && sel != i) {
            continue;
        }
        sz += sizeof(femon_fetch_rec_t) +
              (size_t)reg->m[i].nvals * sizeof(uint64_t);
    }
    return sz;
}

void femon_fetch_encode(const femon_registry_t *reg, uint32_t sel,
                        uint8_t *buf)
{
    femon_fetch_hdr_t *hdr = (femon_fetch_hdr_t *)buf;
    uint8_t *p = buf + sizeof(*hdr);
    uint32_t i, n = 0;

    for (i = 0; i < reg->count; i++) {
        const femon_metric_t *m = &reg->m[i];
        femon_fetch_rec_t *rec;
        uint64_t *vals;

        if (sel != FEMON_SEL_ALL && sel != i) {
            continue;
        }
        rec = (femon_fetch_rec_t *)p;
        rec->id = i;
        rec->rsvd = 0;
        rec->nvals = m->nvals;
        vals = (uint64_t *)(p + sizeof(*rec));
        if (m->ptr) {
            uint32_t v;

            /* statistics read without synchronizing with the writer: a relaxed
             * load only stops the compiler from hoisting or caching it (free on x86-64) */
            for (v = 0; v < m->nvals; v++) {
                vals[v] = __atomic_load_n(&m->ptr[v], __ATOMIC_RELAXED);
            }
        } else {
            m->fill(m->opaque, vals);
        }
        p += sizeof(*rec) + (size_t)m->nvals * sizeof(uint64_t);
        n++;
    }
    hdr->version = reg->version;
    hdr->count = n;
}

void femon_registry_set_reset_hook(femon_registry_t *reg,
                                   void (*hook)(void *), void *opaque)
{
    reg->reset_hook = hook;
    reg->reset_opaque = opaque;
}

void femon_registry_reset(femon_registry_t *reg)
{
    for (uint32_t i = 0; i < reg->count; i++) {
        femon_metric_t *m = &reg->m[i];
        if (m->type == FEMON_MT_U64 && m->ptr) {
            memset(m->ptr, 0, (size_t)m->nvals * sizeof(uint64_t));
        }
    }
    femon_registry_reset_hists(reg);
    if (reg->reset_hook) {
        reg->reset_hook(reg->reset_opaque);
    }
}
