# FEMU Monitor

A real-time monitoring and logging tool for FEMU-based NVMe SSD internal states, supporting both Conventional Namespace (CNS) and Zoned Namespace (ZNS) SSDs.

## Features

- **Real-time Monitoring**: Track CNS SSD lines and ZNS SSD zones status in real-time
- **Dual Device Support**: Simultaneous monitoring of CNS and ZNS SSDs in F2FS multi-device environments
- **Interactive TUI**: Clean ncurses-based terminal interface with multiple viewing modes
- **Comprehensive Metrics**:
  - CNS SSD: Line status distribution (free/victim/full), Valid/Invalid Page Count (VPC/IPC), device-level GC statistics
  - ZNS SSD: Zone status distribution, write pointer positions, zone reset statistics
- **Data Logging**: Export metrics to CSV format with configurable sampling intervals
- **Non-intrusive**: Uses NVMe admin commands to avoid I/O path interference

## Requirements

- Linux system with FEMU v9.0.1 or higher
- Root privileges (for NVMe admin commands)
- Required packages:
  - gcc
  - make
  - libncurses-dev
  - nvme-cli

## Installation

### Using Installation Script

```bash
sudo ./install.sh
```

### Manual Installation

```bash
make
sudo make install
```

## Usage

Basic usage:

```bash
sudo femonitor
```

Specify a device:

```bash
sudo femonitor -d /dev/nvme0n1
```

Change update interval:

```bash
sudo femonitor -i 1  # Update every 1 second (default)
```

Enable logging with custom path:

```bash
sudo femonitor -l /path/to/logfile.csv
```

Display help:

```bash
femonitor --help
```

## Interface Controls

### Main Controls
- `1` - Switch to CNS SSD monitoring view
- `2` - Switch to ZNS SSD monitoring view
- `L` - Toggle CSV logging on/off
- `D` - Toggle debug mode (show/hide debug messages)
- `q` - Quit the application

### View Controls
When in CNS SSD (`1`) or ZNS SSD (`2`) view:
- `a` - Toggle between overview and detailed view
  - Overview: Summary statistics and distributions
  - Detailed: Per-line (CNS) or per-zone (ZNS) visualization

### Available Views

#### CNS SSD Views
- **Overview**: Device summary, write pointer status, line distribution, GC statistics
- **Detailed** (press `a`): Per-line VPC/IPC visualization with usage rates

#### ZNS SSD Views
- **Overview**: Zone status distribution, write pointer distribution, zone reset count
- **Detailed** (press `a`): Per-zone status visualization with write pointer positions