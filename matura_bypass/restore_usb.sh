#!/bin/bash

# Matura Bypass Restore Utility

if [ "$EUID" -ne 0 ]; then
  echo "Please run as root (use sudo)"
  exit 1
fi

if [ -z "$1" ]; then
    echo "Usage: sudo ./restore_usb.sh <path_to_initrd>"
    exit 1
fi

INITRD_PATH=$1
BACKUP_PATH="${INITRD_PATH}.bak"

echo "--- USB Restore ---"

if [ -f "$BACKUP_PATH" ]; then
    echo "Restoring backup to $INITRD_PATH..."
    cp "$BACKUP_PATH" "$INITRD_PATH"
    echo "Restore complete."
else
    echo "Error: Backup file $BACKUP_PATH not found."
    exit 1
fi
