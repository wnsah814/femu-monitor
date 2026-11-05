/**
 * femon_generic.c
 * Schema-driven generic metric client (0xD1 SCHEMA / 0xD2 FETCH).
 */

#include "../../include/core/femon_generic.h"

#include <linux/nvme_ioctl.h> /* renames nvme_admin_cmd -> nvme_passthru_cmd */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "../../include/core/nvme_cmd.h"

/* Generous ceiling for one schema transfer: header + 64 fixed records. */
#define FEMON_SCHEMA_BUF_SIZE \
  (sizeof(femon_schema_hdr_t) + 64 * sizeof(femon_schema_rec_t))

/**
 * Fetch the schema into a malloc'd buffer.
 *
 * @param device_path NVMe device path
 * @param hdr_out Filled with a copy of the schema header
 * @return malloc'd array of schema records (caller frees), NULL on error
 */
static femon_schema_rec_t *fetch_schema(const char *device_path,
                                        femon_schema_hdr_t *hdr_out) {
  uint8_t *buf = calloc(1, FEMON_SCHEMA_BUF_SIZE);
  femon_schema_rec_t *recs = NULL;

  if (!buf) return NULL;

  if (nvme_admin_cmd(device_path, NVME_ADM_CMD_FEMON_SCHEMA, buf,
                     FEMON_SCHEMA_BUF_SIZE, (FEMON_SCHEMA_BUF_SIZE / 4) - 1, 0,
                     0, 0, 0, 0) != 0) {
    fprintf(stderr, "femon: schema query (0xD1) failed on %s\n", device_path);
    free(buf);
    return NULL;
  }

  memcpy(hdr_out, buf, sizeof(*hdr_out));
  if (hdr_out->count == 0 || hdr_out->count > 64) {
    fprintf(stderr, "femon: implausible schema count %u\n", hdr_out->count);
    free(buf);
    return NULL;
  }

  recs = malloc(hdr_out->count * sizeof(femon_schema_rec_t));
  if (recs) {
    memcpy(recs, buf + sizeof(femon_schema_hdr_t),
           hdr_out->count * sizeof(femon_schema_rec_t));
  }
  free(buf);
  return recs;
}

static uint32_t rec_nvals(const femon_schema_rec_t *r) {
  uint32_t n = 1;
  for (int d = 0; d < r->ndims && d < FEMON_MAX_DIMS; d++) n *= r->dims[d];
  return n;
}

static const char *type_name(uint8_t t) {
  switch (t) {
    case FEMON_MT_U64:
      return "u64";
    case FEMON_MT_HIST:
      return "hist";
    default:
      return "?";
  }
}

int femon_generic_list(const char *device_path) {
  femon_schema_hdr_t hdr;
  femon_schema_rec_t *recs = fetch_schema(device_path, &hdr);

  if (!recs) return -1;

  printf("# femon schema: %s (version=%u, %u metrics)\n", device_path,
         hdr.version, hdr.count);
  printf("id,type,dims,name\n");
  for (uint32_t i = 0; i < hdr.count; i++) {
    const femon_schema_rec_t *r = &recs[i];
    char dims[64] = "scalar";

    if (r->ndims > 0) {
      char *p = dims;
      for (int d = 0; d < r->ndims && d < FEMON_MAX_DIMS; d++) {
        p += snprintf(p, sizeof(dims) - (p - dims), "[%u]", r->dims[d]);
      }
    }
    printf("%u,%s,%s,%s\n", r->id, type_name(r->type), dims, r->name);
  }
  free(recs);
  return 0;
}

/* Print one value row with the flattened index rendered per-dimension. */
static void print_value(const femon_schema_rec_t *r, uint32_t flat,
                        uint64_t val) {
  if (r->ndims == 0) {
    printf("%s,%lu\n", r->name, (unsigned long)val);
    return;
  }

  uint32_t idx[FEMON_MAX_DIMS] = {0};
  uint32_t rem = flat;
  for (int d = r->ndims - 1; d >= 0; d--) {
    idx[d] = rem % r->dims[d];
    rem /= r->dims[d];
  }
  printf("%s", r->name);
  for (int d = 0; d < r->ndims; d++) printf("[%u]", idx[d]);
  printf(",%lu\n", (unsigned long)val);
}

/**
 * Fetch every metric's values into per-metric arrays indexed by id.
 *
 * @param values_out malloc'd array of hdr->count pointers, each a malloc'd
 *                   u64 array of rec_nvals(rec) entries (caller frees both)
 * @return 0 on success
 */
static int fetch_values(const char *device_path, const femon_schema_hdr_t *hdr,
                        const femon_schema_rec_t *recs, uint64_t ***values_out) {
  size_t fetch_size = sizeof(femon_fetch_hdr_t);
  uint8_t *buf = NULL;
  const uint8_t *p;
  femon_fetch_hdr_t fh;
  uint64_t **values = NULL;
  int ret = -1;

  for (uint32_t i = 0; i < hdr->count; i++) {
    fetch_size += sizeof(femon_fetch_rec_t) + rec_nvals(&recs[i]) * 8;
  }
  /* keep NUMD dword-exact */
  fetch_size = (fetch_size + 3) & ~(size_t)3;

  buf = calloc(1, fetch_size);
  values = calloc(hdr->count, sizeof(uint64_t *));
  if (!buf || !values) goto out;

  if (nvme_admin_cmd(device_path, NVME_ADM_CMD_FEMON_FETCH, buf, fetch_size,
                     (fetch_size / 4) - 1, FEMON_SEL_ALL, 0, 0, 0, 0) != 0) {
    fprintf(stderr, "femon: fetch (0xD2) failed on %s\n", device_path);
    goto out;
  }

  memcpy(&fh, buf, sizeof(fh));
  if (fh.version != hdr->version) {
    fprintf(stderr, "femon: schema changed during fetch (v%u -> v%u), retry\n",
            hdr->version, fh.version);
    goto out;
  }

  p = buf + sizeof(femon_fetch_hdr_t);
  for (uint32_t i = 0; i < fh.count; i++) {
    femon_fetch_rec_t fr;
    memcpy(&fr, p, sizeof(fr));
    p += sizeof(fr);
    if (fr.id >= hdr->count || fr.nvals != rec_nvals(&recs[fr.id])) {
      fprintf(stderr, "femon: fetch record %u malformed (id=%u nvals=%u)\n", i,
              fr.id, fr.nvals);
      goto out;
    }
    values[fr.id] = malloc(fr.nvals * sizeof(uint64_t));
    if (!values[fr.id]) goto out;
    memcpy(values[fr.id], p, fr.nvals * sizeof(uint64_t));
    p += fr.nvals * sizeof(uint64_t);
  }
  *values_out = values;
  values = NULL;
  ret = 0;

out:
  if (values) {
    for (uint32_t i = 0; i < hdr->count; i++) free(values[i]);
    free(values);
  }
  free(buf);
  return ret;
}

static void free_values(uint32_t count, uint64_t **values) {
  for (uint32_t i = 0; i < count; i++) free(values[i]);
  free(values);
}

int femon_generic_dump(const char *device_path) {
  femon_schema_hdr_t hdr;
  femon_schema_rec_t *recs = fetch_schema(device_path, &hdr);
  uint64_t **values = NULL;
  struct timespec ts;

  if (!recs) return -1;
  if (fetch_values(device_path, &hdr, recs, &values) != 0) {
    free(recs);
    return -1;
  }

  clock_gettime(CLOCK_REALTIME, &ts);
  printf("# femon fetch: %s version=%u count=%u timestamp_ns=%lld.%09ld\n",
         device_path, hdr.version, hdr.count, (long long)ts.tv_sec,
         ts.tv_nsec);
  printf("metric,value\n");
  for (uint32_t i = 0; i < hdr.count; i++) {
    if (!values[i]) continue;
    for (uint32_t v = 0; v < rec_nvals(&recs[i]); v++) {
      print_value(&recs[i], v, values[i][v]);
    }
  }

  free_values(hdr.count, values);
  free(recs);
  return 0;
}

int femon_generic_trace(const char *device_path, int enable) {
  if (nvme_admin_cmd(device_path, NVME_ADM_CMD_FEMON_TRACE, NULL, 0, 0,
                     enable ? 1 : 0, FEMON_SEL_ALL, 0, 0, 0) != 0) {
    fprintf(stderr, "femon: trace ctl (0xD3) failed on %s\n", device_path);
    return -1;
  }
  fprintf(stderr, "femon: tracepoints %s on %s\n",
          enable ? "enabled" : "disabled", device_path);
  return 0;
}

/* "[2^i,2^(i+1))us" label, µs rendered with k/M suffix above 4 digits */
static void hist_bucket_label(char *out, size_t sz, int i, int last) {
  char lo[16], hi[16];
  uint64_t lo_us = i ? (1ULL << i) : 0;
  uint64_t hi_us = 1ULL << (i + 1);

  for (int k = 0; k < 2; k++) {
    uint64_t v = k ? hi_us : lo_us;
    char *dst = k ? hi : lo;
    if (v >= 1000000)
      snprintf(dst, sizeof(lo), "%.4gM", v / 1e6);
    else if (v >= 10000)
      snprintf(dst, sizeof(lo), "%.4gk", v / 1e3);
    else
      snprintf(dst, sizeof(lo), "%lu", (unsigned long)v);
  }
  if (last)
    snprintf(out, sz, "[%sus,inf)", lo);
  else
    snprintf(out, sz, "[%sus,%sus)", lo, hi);
}

int femon_generic_hist(const char *device_path) {
  femon_schema_hdr_t hdr;
  femon_schema_rec_t *recs = fetch_schema(device_path, &hdr);
  uint64_t **values = NULL;
  int seen = 0;

  if (!recs) return -1;
  if (fetch_values(device_path, &hdr, recs, &values) != 0) {
    free(recs);
    return -1;
  }

  for (uint32_t i = 0; i < hdr.count; i++) {
    const femon_schema_rec_t *r = &recs[i];
    uint32_t nb;
    uint64_t total = 0, peak = 0;

    if (r->type != FEMON_MT_HIST || !values[i]) continue;
    seen++;
    /* payload: FEMON_HIST_BUCKETS buckets + n/sum/min/max tail (ns) */
    nb = rec_nvals(r) >= FEMON_HIST_NVALS ? FEMON_HIST_BUCKETS : rec_nvals(r);
    for (uint32_t b = 0; b < nb; b++) {
      total += values[i][b];
      if (values[i][b] > peak) peak = values[i][b];
    }
    if (rec_nvals(r) >= FEMON_HIST_NVALS) {
      uint64_t n = values[i][nb], sum = values[i][nb + 1];
      uint64_t vmin = values[i][nb + 2], vmax = values[i][nb + 3];

      printf("# %s (samples=%lu, mean=%.1fus, min=%.1fus, max=%.1fus)\n",
             r->name, (unsigned long)n, n ? sum / 1000.0 / n : 0.0,
             vmin / 1000.0, vmax / 1000.0);
      if (total != n) {
        fprintf(stderr,
                "femon: WARNING %s bucket-sum %lu != n %lu (snapshot skew)\n",
                r->name, (unsigned long)total, (unsigned long)n);
      }
    } else {
      printf("# %s (samples=%lu)\n", r->name, (unsigned long)total);
    }
    if (total == 0) {
      printf("  (empty — enable tracing with -T on, then run a workload)\n");
      continue;
    }
    for (uint32_t b = 0; b < nb; b++) {
      char label[48];
      int bar = peak ? (int)(values[i][b] * 40 / peak) : 0;

      hist_bucket_label(label, sizeof(label), b, b == nb - 1);
      printf("%-16s|%-40.*s| %lu\n", label, bar,
             "@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@",
             (unsigned long)values[i][b]);
    }
  }
  if (!seen) fprintf(stderr, "femon: no histogram metrics registered\n");

  free_values(hdr.count, values);
  free(recs);
  return 0;
}


/* ------------------------------------------------------------------ snapshot
 * Per-path schema cache: re-read when the schema generation (version) changes.
 * Normal polling is a single 0xD2 (one admin command per cycle, even at 1 ms). */
#define FEMON_CACHE_MAX 4
static struct {
  char path[256];
  int valid;
  femon_schema_hdr_t hdr;
  femon_schema_rec_t *recs;
} schema_cache[FEMON_CACHE_MAX];

static int cache_slot(const char *device_path) {
  int free_slot = -1;
  for (int i = 0; i < FEMON_CACHE_MAX; i++) {
    if (schema_cache[i].valid && strcmp(schema_cache[i].path, device_path) == 0)
      return i;
    if (!schema_cache[i].valid && free_slot < 0) free_slot = i;
  }
  if (free_slot < 0) free_slot = 0;
  return free_slot;
}

static int cache_load(int slot, const char *device_path) {
  femon_schema_hdr_t hdr;
  femon_schema_rec_t *recs = fetch_schema(device_path, &hdr);
  if (!recs) return -1;
  free(schema_cache[slot].recs);
  schema_cache[slot].recs = recs;
  schema_cache[slot].hdr = hdr;
  snprintf(schema_cache[slot].path, sizeof(schema_cache[slot].path), "%s",
           device_path);
  schema_cache[slot].valid = 1;
  return 0;
}

int femon_snapshot_fetch(const char *device_path, femon_snapshot_t *snap) {
  int slot = cache_slot(device_path);
  memset(snap, 0, sizeof(*snap));
  if (!schema_cache[slot].valid || strcmp(schema_cache[slot].path, device_path)) {
    if (cache_load(slot, device_path) != 0) return -1;
  }
  for (int attempt = 0; attempt < 2; attempt++) {
    if (fetch_values(device_path, &schema_cache[slot].hdr,
                     schema_cache[slot].recs, &snap->values) == 0) {
      snap->hdr = schema_cache[slot].hdr;
      snap->recs = schema_cache[slot].recs;
      return 0;
    }
    /* e.g. schema generation mismatch: refresh the cache and retry once */
    if (cache_load(slot, device_path) != 0) return -1;
  }
  return -1;
}

const uint64_t *femon_snapshot_get(const femon_snapshot_t *snap, const char *name,
                                   uint32_t *nvals) {
  if (!snap->recs) return NULL;
  for (uint32_t i = 0; i < snap->hdr.count; i++) {
    if (strncmp(snap->recs[i].name, name, FEMON_NAME_LEN) == 0) {
      if (nvals) *nvals = rec_nvals(&snap->recs[i]);
      return snap->values[i];
    }
  }
  if (nvals) *nvals = 0;
  return NULL;
}

void femon_snapshot_free(femon_snapshot_t *snap) {
  if (snap->values) free_values(snap->hdr.count, snap->values);
  snap->values = NULL;
  snap->recs = NULL;
}
