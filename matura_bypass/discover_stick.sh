#!/bin/bash

# Discovery script for Matura Bypass Challenge
# This script identifies the USB stick structure and key files.

echo "--- USB Stick Discovery Script ---"

# List block devices to help the user identify the USB stick
echo "1. Available Block Devices:"
lsblk -o NAME,SIZE,TYPE,MOUNTPOINT,FSTYPE

echo ""
echo "Please identify your USB stick from the list above (e.g., sdb, sdc)."
echo "If you are in WSL, make sure you have attached the USB stick using 'usbipd' if necessary,"
echo "or mounted it to a folder."

# Try to find common boot files if a path is provided
if [ -z "$1" ]; then
    echo "Usage: ./discover_stick.sh <mount_point_of_usb>"
    exit 1
fi

MOUNT_POINT=$1

if [ ! -d "$MOUNT_POINT" ]; then
    echo "Error: Directory $MOUNT_POINT does not exist."
    exit 1
fi

echo ""
echo "2. Searching for Boot Files in $MOUNT_POINT:"

echo "Looking for Kernel..."
find "$MOUNT_POINT" -name "vmlinuz*" -o -name "bzImage"

echo "Looking for Initrd..."
find "$MOUNT_POINT" -name "initrd*" -o -name "initramfs*"

echo "Looking for Bootloader Config..."
find "$MOUNT_POINT" -name "grub.cfg" -o -name "syslinux.cfg" -o -name "isolinux.cfg"

echo "Checking for Live OS structure..."
if [ -d "$MOUNT_POINT/live" ]; then
    echo "Found /live directory (likely Debian Live)."
    ls -F "$MOUNT_POINT/live"
fi

echo ""
echo "3. Identifying OS Version (if possible):"
if [ -f "$MOUNT_POINT/etc/os-release" ]; then
    cat "$MOUNT_POINT/etc/os-release"
elif [ -f "$MOUNT_POINT/issue" ]; then
    cat "$MOUNT_POINT/issue"
else
    echo "Could not find /etc/os-release or /issue on the root of the stick."
    echo "This might be because the stick uses a compressed squashfs for the root filesystem."
fi

echo ""
echo "4. Checking for SquashFS (Common in Live USBs):"
find "$MOUNT_POINT" -name "*.squashfs"

echo ""
echo "Discovery complete."
