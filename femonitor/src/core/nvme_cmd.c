/**
 * nvme_cmd.c
 * NVMe Command Implementation for FEMONITOR
 */

#include "../../include/core/nvme_cmd.h"
#include "../../include/core/femon_generic.h"

#include <errno.h>
#include <fcntl.h>
#include <linux/nvme_ioctl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "../../include/common/config.h"
#include "../../include/views/debug_view.h"

/**
 * Common function to execute NVMe admin command
 *
 * @param device_path Path to the NVMe device
 * @param opcode Admin command opcode
 * @param buffer Buffer for data transfer
 * @param size Buffer size
 * @param cdw10 Command dword 10
 * @param cdw11 Command dword 11
 * @param cdw12 Command dword 12
 * @param cdw13 Command dword 13
 * @param cdw14 Command dword 14
 * @param cdw15 Command dword 15
 * @return 0 on success, non-zero on failure
 */
int nvme_admin_cmd(const char *device_path, uint8_t opcode, void *buffer,
                   size_t size, uint32_t cdw10, uint32_t cdw11, uint32_t cdw12,
                   uint32_t cdw13, uint32_t cdw14, uint32_t cdw15) {
  int fd;
  struct nvme_passthru_cmd cmd = {0};
  int ret = -1;

  // Open NVMe device
  fd = open(device_path, O_RDONLY);
  if (fd < 0) {
    debug_log_add("Failed to open device %s: %s", device_path, strerror(errno));
    return -1;
  }

  // Setup NVMe Admin Passthru command
  memset(&cmd, 0, sizeof(cmd));
  cmd.opcode = opcode;
  cmd.nsid = 1;  // Namespace ID
  cmd.addr = (unsigned long)(uintptr_t)buffer;
  cmd.data_len = size;
  cmd.cdw10 = cdw10;
  cmd.cdw11 = cdw11;
  cmd.cdw12 = cdw12;
  cmd.cdw13 = cdw13;
  cmd.cdw14 = cdw14;
  cmd.cdw15 = cdw15;

  // Execute command
  ret = ioctl(fd, NVME_IOCTL_ADMIN_CMD, &cmd);
  close(fd);

  if (ret) {
    debug_log_add("NVMe command execution failed (opcode 0x%02x): %s", opcode,
                  strerror(errno));
    return -1;
  }

  return 0;
}

/**
 * Execute BBSSD monitoring command to get data based on device info
 *
 * @param data Pointer to bbssd_data_t structure
 * @param device_path Path to the NVMe device
 * @param device_info Pointer to device info structure (if NULL, will be
 * detected)
 * @return 0 on success, non-zero on failure
 */
int nvme_get_bbssd_data(bbssd_data_t *data, const char *device_path,
                        const nvme_device_info_t *device_info) {
  /* rebuild the view model from the bbssd.* metrics of the 0xD2 snapshot */
  femon_snapshot_t snap;
  uint32_t n_ipc = 0, n_vpc = 0, n_pos = 0, n_cnt = 0, n_wp = 0;
  const uint64_t *ipc, *vpc, *pos, *cnt, *wp, *v;
  int tt_lines;
  (void)device_info;

  if (femon_snapshot_fetch(device_path, &snap) != 0) {
    debug_log_add("Failed to fetch BBSSD registry snapshot");
    return -1;
  }
  ipc = femon_snapshot_get(&snap, "bbssd.line.ipc", &n_ipc);
  vpc = femon_snapshot_get(&snap, "bbssd.line.vpc", &n_vpc);
  pos = femon_snapshot_get(&snap, "bbssd.line.pos", &n_pos);
  cnt = femon_snapshot_get(&snap, "bbssd.line.cnt_free_victim_full", &n_cnt);
  wp  = femon_snapshot_get(&snap, "bbssd.wp.pos", &n_wp);
  if (!ipc || !vpc || !pos || !cnt || n_cnt < 3 || !wp || n_wp < 6) {
    debug_log_add("BBSSD registry snapshot lacks expected metrics");
    femon_snapshot_free(&snap);
    return -1;
  }
  tt_lines = (int)n_ipc;

  v = femon_snapshot_get(&snap, "bbssd.gc.count", NULL); data->gc_count = v ? v[0] : 0;
  v = femon_snapshot_get(&snap, "bbssd.gc.pgs", NULL);   data->gc_pgs   = v ? v[0] : 0;
  data->wp_info.curline_id = (int)(int64_t)wp[0];
  data->wp_info.ch  = (int)wp[1];
  data->wp_info.lun = (int)wp[2];
  data->wp_info.pg  = (int)wp[3];
  data->wp_info.blk = (int)wp[4];
  data->wp_info.pl  = (int)wp[5];

  if (data->lm_info == NULL || data->lm_info->tt_lines < tt_lines) {
    void *nm = realloc(data->lm_info, sizeof(bbssd_line_mgmt_info_t) +
                                          (size_t)tt_lines * sizeof(bbssd_line_info_t));
    if (!nm) {
      debug_log_add("Failed to allocate line management info");
      femon_snapshot_free(&snap);
      return -1;
    }
    data->lm_info = nm;
  }
  data->lm_info->tt_lines        = tt_lines;
  data->lm_info->free_line_cnt   = (int)cnt[0];
  data->lm_info->victim_line_cnt = (int)cnt[1];
  data->lm_info->full_line_cnt   = (int)cnt[2];
  for (int i = 0; i < tt_lines; i++) {
    data->lm_info->lines[i].id  = i;
    data->lm_info->lines[i].ipc = (int)(int64_t)ipc[i];
    data->lm_info->lines[i].vpc = (int)(int64_t)vpc[i];
    data->lm_info->lines[i].pos = pos[i];
  }
  femon_snapshot_free(&snap);
  return 0;
}

/**
 * Execute ZNSSD monitoring command to get data
 *
 * @param data Pointer to znssd_data_t structure
 * @param device_path Path to the NVMe device
 * @param device_info Optional device info structure (can be NULL)
 * @return 0 on success, non-zero on failure
 */
int nvme_get_znssd_data(znssd_data_t *data, const char *device_path,
                        const nvme_device_info_t *device_info) {
  /* rebuild the view model from the zns.* metrics of the 0xD2 snapshot */
  femon_snapshot_t snap;
  uint32_t nz = 0, nc = 0, np = 0, tmp;
  const uint64_t *wptr, *rst, *zs, *zt, *za, *zcap, *zslba, *dwp;
  const uint64_t *cx, *cc, *cw, *pio, *pc, *pw, *v;
  (void)device_info;

  if (femon_snapshot_fetch(device_path, &snap) != 0) {
    debug_log_add("Failed to fetch ZNSSD registry snapshot");
    return -1;
  }
  wptr  = femon_snapshot_get(&snap, "zns.zone.write_ptr", &nz);
  rst   = femon_snapshot_get(&snap, "zns.zone.reset_count", &tmp);
  zs    = femon_snapshot_get(&snap, "zns.zone.state", &tmp);
  zt    = femon_snapshot_get(&snap, "zns.zone.type", &tmp);
  za    = femon_snapshot_get(&snap, "zns.zone.attr", &tmp);
  zcap  = femon_snapshot_get(&snap, "zns.zone.cap", &tmp);
  zslba = femon_snapshot_get(&snap, "zns.zone.slba", &tmp);
  dwp   = femon_snapshot_get(&snap, "zns.zone.dev_wp", &tmp);
  cx    = femon_snapshot_get(&snap, "zns.ch.xfer_count", &nc);
  cc    = femon_snapshot_get(&snap, "zns.ch.conflict_count", &tmp);
  cw    = femon_snapshot_get(&snap, "zns.ch.wait_ns", &tmp);
  pio   = femon_snapshot_get(&snap, "zns.plane.io_count", &np);
  pc    = femon_snapshot_get(&snap, "zns.plane.conflict_count", &tmp);
  pw    = femon_snapshot_get(&snap, "zns.plane.wait_ns", &tmp);
  if (!wptr || !rst || !zs || !zt || !za || !zcap || !zslba || !dwp ||
      !cx || !cc || !cw || !pio || !pc || !pw) {
    debug_log_add("ZNSSD registry snapshot lacks expected metrics");
    femon_snapshot_free(&snap);
    return -1;
  }
  data->num_zones = nz;
  data->num_channels = nc;
  data->num_planes = np;
  v = femon_snapshot_get(&snap, "zns.reset.count", NULL); data->zr_count = v ? v[0] : 0;
  v = femon_snapshot_get(&snap, "zns.wc.hit",   NULL); data->wc_hit   = v ? v[0] : 0;
  v = femon_snapshot_get(&snap, "zns.wc.miss",  NULL); data->wc_miss  = v ? v[0] : 0;
  v = femon_snapshot_get(&snap, "zns.wc.evict", NULL); data->wc_evict = v ? v[0] : 0;
  v = femon_snapshot_get(&snap, "zns.wc.flush", NULL); data->wc_flush = v ? v[0] : 0;

  if (data->znssd_data == NULL)
    data->znssd_data = malloc(nz * sizeof(znssd_zone_info_t));
  if (data->channel_stats == NULL && nc > 0)
    data->channel_stats = malloc(nc * sizeof(znssd_channel_stat_t));
  if (data->plane_stats == NULL && np > 0)
    data->plane_stats = malloc(np * sizeof(znssd_plane_stat_t));
  if (!data->znssd_data || (nc > 0 && !data->channel_stats) ||
      (np > 0 && !data->plane_stats)) {
    debug_log_add("Failed to allocate ZNSSD view model");
    femon_snapshot_free(&snap);
    return -1;
  }
  for (uint32_t i = 0; i < nz; i++) {
    znssd_zone_info_t *z = &data->znssd_data[i];
    z->d.zt = (uint8_t)zt[i]; z->d.zs = (uint8_t)zs[i]; z->d.za = (uint8_t)za[i];
    z->d.zcap = zcap[i]; z->d.zslba = zslba[i]; z->d.wp = dwp[i];
    z->w_ptr = wptr[i]; z->cnt_reset = rst[i];
  }
  for (uint32_t c = 0; c < nc; c++) {
    data->channel_stats[c].total_xfer_count   = cx[c];
    data->channel_stats[c].conflict_count     = cc[c];
    data->channel_stats[c].total_wait_time_ns = cw[c];
  }
  for (uint32_t i = 0; i < np; i++) {
    data->plane_stats[i].total_io_count     = pio[i];
    data->plane_stats[i].conflict_count     = pc[i];
    data->plane_stats[i].total_wait_time_ns = pw[i];
  }
  femon_snapshot_free(&snap);
  return 0;
}

/**
 * Detect device type and populate device info structure
 *
 * @param device_path Path to the NVMe device
 * @param device_info Pointer to device info structure to populate
 * @return 0 on success, non-zero on failure
 */
int nvme_detect_device_info(const char *device_path,
                            nvme_device_info_t *device_info) {
  uint8_t buffer[4096] = {0};
  int ret;

  if (!device_info) {
    debug_log_add("Invalid device_info pointer");
    return -1;
  }

  // Initialize device_info structure
  memset(device_info, 0, sizeof(nvme_device_info_t));

  // First try to use device-specific type detection command
  ret = nvme_admin_cmd(device_path, NVME_ADM_CMD_DEVTYPE, buffer, sizeof(buffer),
                       1023, 0, 0, 0, 0, 0);
  if (ret == 0) {
    // First byte indicates device type
    device_info->type = buffer[0];

    switch (device_info->type) {
      case DEVICE_BBSSD:
        debug_log_add("Detected BBSSD device: %s", device_path);
        break;

      case DEVICE_ZNSSD:
        debug_log_add("Detected ZNSSD device: %s", device_path);
        break;

      default:
        debug_log_add("Unknown device type: %d", buffer[0]);
        device_info->type = DEVICE_UNKNOWN;
        break;
    }
  } else {
    // Try the identify namespace command
    memset(buffer, 0, sizeof(buffer));
    ret = nvme_admin_cmd(device_path, NVME_ADMIN_CMD_IDENTIFY, buffer,
                         sizeof(buffer), 0, 1, 0, 0, 0, 0);
    if (ret != 0) {
      debug_log_add("Failed to identify device: %s", device_path);
      device_info->type = DEVICE_UNKNOWN;
      return -1;
    }

    // Check ZNS bit in identify data
    if (buffer[28] & 0x02) {
      debug_log_add("Detected ZNSSD device from identify data: %s",
                    device_path);
      device_info->type = DEVICE_ZNSSD;
    } else {
      // Default to BBSSD if no specific type detected
      debug_log_add("Assuming BBSSD device: %s", device_path);
      device_info->type = DEVICE_BBSSD;
    }
  }

  // Now get detailed device information based on detected type
  // memset(buffer, 0, sizeof(buffer));
  // ret = nvme_admin_cmd(device_path, DEVICE_INFO_OPCODE, buffer,
  // sizeof(buffer),
  //                      device_info->type, 0, 0, 0, 0, 0);

  if (ret == 0) {
    // Parse the buffer and populate device_info based on device type
    switch (device_info->type) {
      case DEVICE_BBSSD: {
        // Copy BBSSD specific information
        memcpy(&device_info->bbssd, buffer + sizeof(device_type_t),
               sizeof(device_info->bbssd));
        debug_log_add("BBSSD device info populated for %s", device_path);
        break;
      }

      case DEVICE_ZNSSD: {
        // Copy ZNSSD specific information
        memcpy(&device_info->znssd, buffer + sizeof(device_type_t),
               sizeof(device_info->znssd));
        debug_log_add("ZNSSD device info populated for %s", device_path);
        break;
      }

      default:
        debug_log_add("Cannot populate info for unknown device type");
        return -1;
    }

    return 0;
  } else {
    debug_log_add("Failed to get device info for %s", device_path);
    return -1;
  }
}

/**
 * Detect device type using NVMe identify command
 *
 * @param device_path Path to the NVMe device
 * @return Device type or DEVICE_UNKNOWN on error
 */
device_type_t nvme_detect_device_type(const char *device_path) {
  nvme_device_info_t device_info;

  // Use the new function to get device info
  if (nvme_detect_device_info(device_path, &device_info) == 0) {
    return device_info.type;
  }

  return DEVICE_UNKNOWN;
}

/**
 * Reset monitoring statistics on the device
 *
 * @param device_path Path to the NVMe device
 * @return 0 on success, non-zero on failure
 */
int nvme_reset_stats(const char *device_path) {
  /* 0xD4: clear registered cumulative counters and histograms */
  int ret = nvme_admin_cmd(device_path, NVME_ADM_CMD_FEMON_RESET, NULL, 0,
                           0, 0, 0, 0, 0, 0);
  if (ret != 0) {
    debug_log_add("Failed to reset statistics (0xD4)");
    return -1;
  }
  return 0;
}