#!/bin/bash
# FEMONITOR Logging Control Script

# Check if script is run with sudo privileges
if [ "$EUID" -ne 0 ]; then
  echo "Please run with sudo privileges"
  exit 1
fi

# File for passing custom filename
FILENAME_REQUEST_FILE="/tmp/femonitor_filename_request"

# Find FEMONITOR PID
FEMONITOR_PID=$(pgrep femonitor)

if [ -z "$FEMONITOR_PID" ]; then
  echo "FEMONITOR is not running."
  exit 1
fi

# Parse command line options
action="status"
custom_filename=""

if [ $# -ge 1 ]; then
  action="$1"
fi

if [ $# -ge 2 ] && [ "$action" = "start" ]; then
  custom_filename="$2"
fi

case "$action" in
  start)
    if [ -z "$custom_filename" ]; then
      echo "Starting logging with auto-generated filename (PID: $FEMONITOR_PID)"
    else
      echo "Starting logging with custom filename: $custom_filename (PID: $FEMONITOR_PID)"
      
      # Write custom filename to request file
      echo -n "$custom_filename" > "$FILENAME_REQUEST_FILE"
      
      # Set permissions so FEMONITOR can read it
      chmod 644 "$FILENAME_REQUEST_FILE"
    fi
    
    # Send the start signal
    kill -SIGUSR2 $FEMONITOR_PID
    ;;
    
  stop)
    kill -SIGUSR1 $FEMONITOR_PID
    echo "Logging stop signal sent to FEMONITOR (PID: $FEMONITOR_PID)"
    ;;
    
  status)
    echo "FEMONITOR is running with PID: $FEMONITOR_PID"
    echo "Use '$0 start [filename]' to start logging or '$0 stop' to stop logging."
    ;;
    
  *)
    echo "Usage: $0 [start|stop|status] [custom_filename]"
    echo "  start [filename]  - Start logging with optional custom filename"
    echo "  stop              - Stop logging"
    echo "  status            - Show FEMONITOR status (default)"
    exit 1
    ;;
esac

exit 0