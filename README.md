# femu-monitor

femonitor is a real-time monitoring tool for FEMU-based NVMe SSD emulators.
It reads the emulator's internal state, such as zone states, GC activity,
per-channel and per-plane I/O and contention, and write cache behavior,
through vendor-specific NVMe admin commands. These commands go through the
admin queue, outside the I/O path being measured.

This repository contains:

- **`femonitor/`**: the guest-side collector, with an ncurses TUI, a logging
  mode (CSV, started and stopped by Unix signals), and a snapshot mode (one CSV
  capture for scripts).
- **`femu/hw/femu/`**: our version of FEMU's `hw/femu`. It adds the emulator
  instrumentation (a metric registry, tracepoint histograms) and the five admin
  commands. It also has the model changes used in our evaluation (see
  [Relation to upstream FEMU](#relation-to-upstream-femu)).
- **`patches/confznspp/`**: the same instrumentation for
  [ConfZNS++](https://github.com/stonet-research/confznsplusplus), used for the
  zone placement experiments.

The previous version of this repository (the opcode 0xC1/0xC2 protocol and a
full FEMU 9.0.1 tree) is kept under the tag `icfec2026`.

## Layout

```
femonitor/        guest-side tool
femu/hw/femu/     FEMU hw/femu with the instrumentation (overlay)
FEMU/             upstream FEMU, pinned (git submodule)
setup.sh          FEMU submodule + overlay -> build/
run-zns.sh        boot a VM with a ZNS SSD
run-cns.sh        boot a VM with a conventional SSD
run-multi.sh      boot a VM with both (F2FS multi-device)
patches/confznspp/  ConfZNS++ port
```

## Build

FEMU (on the host):

```sh
git clone --recursive https://github.com/wnsah814/femu-monitor.git
cd femu-monitor
./setup.sh                          # copies FEMU/ to build/, links build/hw/femu to the overlay
cd build && mkdir -p build-femu && cd build-femu
../configure --enable-kvm --target-list=x86_64-softmmu
make -j$(nproc)
```

FEMU's dependencies are listed in `femu/hw/femu/scripts/pkgdep.sh`. Because
`build/hw/femu` is a symbolic link to `femu/hw/femu`, edits there are picked
up by the next `make`.

The ZNS NAND page size is 16 KiB by default. To build with 4 KiB pages, run
configure with `--extra-cflags=-DZNS_PAGE_KIB=4` (in a separate build
directory, e.g. `build-femu-4k`).

femonitor (in the guest; needs `gcc`, `make`, and `libncurses-dev`). The tool
includes the protocol header from `femu/hw/femu`, so copy both directories:

```sh
cd femonitor && make
sudo make install                   # optional: /usr/local/bin/femonitor
```

## Run

Each script boots one VM. The only required variable is the guest image; the
other variables are listed at the top of each script.

```sh
OSIMGF=/path/to/guest.qcow2 ./run-zns.sh
OSIMGF=/path/to/guest.qcow2 CNS_BLKS_PER_PL=128 ./run-cns.sh
OSIMGF=/path/to/guest.qcow2 CNS_SIZE_MB=12288 ZNS_SIZE_MB=65536 ./run-multi.sh
```

The guest is reachable with `ssh -p 8080 <user>@localhost` (8081 for
`run-multi.sh`). The emulated media is backed by host DRAM. For timing
measurements, raise `RLIMIT_MEMLOCK` so that the backend can be pinned;
otherwise FEMU warns and runs unpinned.

## Using femonitor

In the guest, as root:

```sh
femonitor -m zns -d /dev/nvme0n1                    # TUI, ZNS device
femonitor -m cns -d /dev/nvme0n1                    # TUI, conventional device
femonitor -m multi -d /dev/nvme0n1 -2 /dev/nvme1n1  # TUI, both devices
femonitor -m zns -d /dev/nvme0n1 -L -O logs         # start logging at launch
femonitor -m zns -d /dev/nvme0n1 -S > snap.csv      # one snapshot, CSV to stdout
femonitor -d /dev/nvme0n1 -l                        # list the device's metrics
femonitor -d /dev/nvme0n1 -G                        # dump all metrics as CSV
femonitor -d /dev/nvme0n1 -T on                     # enable tracepoint histograms
femonitor -d /dev/nvme0n1 -H                        # print histograms as ASCII bars
femonitor -d /dev/nvme0n1 -R -S                     # reset counters, then snapshot
```

While femonitor runs, `femonitor/control_logging.sh start [file]` and
`control_logging.sh stop` start and stop logging (SIGUSR2 and SIGUSR1).
`femonitor -h` lists all options.

### Admin commands

| Opcode | Name    | Function |
|--------|---------|----------|
| 0xD0   | DEVTYPE | Device type and geometry |
| 0xD1   | SCHEMA  | Names, types, and dimensions of the registered metrics |
| 0xD2   | FETCH   | Values of all metrics, or of one metric |
| 0xD3   | TRACE   | Enable or disable tracepoint histograms |
| 0xD4   | RESET   | Clear cumulative counters and histograms (gauges and state are kept) |

The payloads are defined in `femu/hw/femu/femon_protocol.h`, which both sides
include. A backend registers a metric by name at initialization
(`femon_registry.h`). The new metric then appears in the 0xD1 schema, and
femonitor can read it without any change to the tool.

Metric groups (run `femonitor -l` for the full list):

- **ZNS:** zone state, write pointer, capacity, and per-zone reset count;
  per-channel transfers, conflicts, and wait time; per-plane I/O, conflicts,
  and wait time; write cache hits, misses, evictions, and flushes; read-path
  page counts.
- **Conventional SSD:** per-line valid and invalid page counts and state; write
  pointer; GC count, copied pages, and GC time; host read wait time behind GC;
  DSM (TRIM) count.
- **Histograms:** modeled zone-reset and write latency; GC time; host read
  wait; NVMe completion posting lag; poller sweep interval; FETCH service
  time.

## Device options added to FEMU

These are `-device femu,...` properties. They need no rebuild.

| Option | Default | Meaning |
|--------|---------|---------|
| `zns_pg_rd_lat`, `zns_pg_wr_lat`, `zns_blk_er_lat` | 0 | ZNS NAND timing in ns (0 = the flash type's constants) |
| `zns_ch_xfer_lat` | 0 | ZNS channel transfer time per page in ns (0 = no channel bus model) |
| `zns_num_wc` | 14 | Number of ZNS write caches |
| `zns_wc_bypass` | 0 | 1 = no write cache; each write is programmed immediately |
| `zns_wc_upstream` | 0 | 1 = the upstream write cache (before the fixes below), for comparison |
| `zns_pe_suspend`, `zns_tsusp_ns` | 0, 0 | ZNS: a read suspends a program or erase; suspend overhead in ns |
| `pe_suspend`, `tsusp_ns` | 0, 0 | Conventional SSD: the same suspend model |
| `hot_cold_sep` | off | Conventional SSD: separate overwritten pages from write-once pages |

## Relation to upstream FEMU

`FEMU/` pins upstream FEMU at `e2d5413ff` (2026-06-19), the version we
evaluated. The overlay replaces its `hw/femu`. Several changes from our work
were later merged into upstream FEMU, in a different form:

| Change | Upstream commit |
|--------|-----------------|
| Key ZNS write caches by the request's zone | `99ce4fb2b` |
| Size the write caches from a `zns_num_wc` property | `66f432117` |
| Let a write miss take an empty cache 0 | `9a3449362` |
| Flush only the pages a partial write cache holds | `347a8a691` |
| Opt-in channel bus for the ZNS timing model | `2a4ebc2cb` |
| Book a read's data-out for the window in which it happens | `c01b8f0ff` |
| A read preempts an in-flight program or erase (P/E suspend) | `9ec423985` |

The overlay also includes two upstream changes made after the pin:
hot/cold separation (`4ded2e928`) and suspending only a program or erase,
not another read (`6a5c498a8`).

The monitoring instrumentation (registry, tracepoints, admin commands
0xD0–0xD4) is only in this repository.

Known limitation: a partial write cache flush always starts at plane 0. As a
result, evictions concentrate on plane 0 of a zone's block. Upstream
`8bcaa0623` resumes each zone's flush at the plane where the previous one
stopped. That change is not in the overlay.

## ConfZNS++ port

`patches/confznspp/femonitor-confznspp.patch` applies to ConfZNS++ `main`
(`1d589356b`). It adds the ZNS instrumentation (0xD0–0xD4, per-channel and
per-plane metrics split by direction, per-zone resets, and latency
histograms). It also adds `zns_ch_backfill`, which limits a read's channel
reservation to its transfer window. It supports ZNS only.

```sh
git clone https://github.com/stonet-research/confznsplusplus.git
cd confznsplusplus && git checkout 1d589356b
git apply /path/to/femu-monitor/patches/confznspp/femonitor-confznspp.patch
mkdir build-femu && cd build-femu
../configure --enable-kvm --target-list=x86_64-softmmu && make -j$(nproc)
cd .. && OSIMGF=/path/to/guest.qcow2 POLICY=FU ./run-femon.sh
```

## License

The FEMU and ConfZNS++ changes are derived from QEMU/FEMU and are under the
GNU General Public License, version 2. femonitor is also released under
GPLv2. See `LICENSE`.
