#!/bin/bash

# Configuration
KEYLOGGER_PATTERN="keylog|logkeys|input-utils"
HOTKEY_FILE="/tmp/keylogger_paused"
LOG_FILE="/tmp/bypass.log"

log() {
    echo "$(date): $1" >> $LOG_FILE
}

if [ -f "$HOTKEY_FILE" ]; then
    # Resume
    PIDS=$(cat "$HOTKEY_FILE")
    if [ -z "$PIDS" ]; then
        log "Error: No PIDs found in $HOTKEY_FILE"
        rm "$HOTKEY_FILE"
        exit 1
    fi
    for pid in $PIDS; do
        kill -CONT "$pid" 2>/dev/null
    done
    rm "$HOTKEY_FILE"
    log "Keylogger RESUMED (PIDs: $PIDS)"
else
    # Pause
    PIDS=$(pgrep -f -i "$KEYLOGGER_PATTERN")
    if [ -n "$PIDS" ]; then
        for pid in $PIDS; do
            kill -STOP "$pid" 2>/dev/null
        done
        echo "$PIDS" > "$HOTKEY_FILE"
        log "Keylogger PAUSED (PIDs: $PIDS)"
    else
        log "Keylogger not found."
    fi
fi
