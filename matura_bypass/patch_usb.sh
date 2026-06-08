#!/bin/bash

# Matura Bypass USB Patcher Utility
# This script unpacks the initrd, injects the payload, and repacks it.

if [ "$EUID" -ne 0 ]; then
  echo "Please run as root (use sudo)"
  exit 1
fi

if [ -z "$1" ] || [ -z "$2" ]; then
    echo "Usage: sudo ./patch_usb.sh <path_to_initrd> <path_to_payload_folder>"
    exit 1
fi

INITRD_PATH=$1
PAYLOAD_FOLDER=$2
WORK_DIR=$(mktemp -d)
BACKUP_PATH="${INITRD_PATH}.bak"

echo "--- USB Patcher ---"
echo "Working directory: $WORK_DIR"

# 1. Backup original initrd
if [ ! -f "$BACKUP_PATH" ]; then
    echo "Creating backup: $BACKUP_PATH"
    cp "$INITRD_PATH" "$BACKUP_PATH"
else
    echo "Backup already exists, skipping."
fi

# 2. Identify compression and unpack
echo "Identifying initrd compression..."
FILE_TYPE=$(file "$INITRD_PATH")

cd "$WORK_DIR"
if echo "$FILE_TYPE" | grep -q "gzip"; then
    echo "Detected GZIP compression."
    zcat "$INITRD_PATH" | cpio -idm
elif echo "$FILE_TYPE" | grep -q "XZ"; then
    echo "Detected XZ compression."
    xzcat "$INITRD_PATH" | cpio -idm
elif echo "$FILE_TYPE" | grep -q "ASCII cpio archive"; then
    echo "Detected uncompressed CPIO archive."
    cpio -idm < "$INITRD_PATH"
else
    echo "Unknown compression or archive type: $FILE_TYPE"
    exit 1
fi

# 3. Inject Payload
echo "Injecting payload..."
mkdir -p "$WORK_DIR/opt/bypass"
cp "$PAYLOAD_FOLDER/payload.sh" "$WORK_DIR/opt/bypass/"
cp "$PAYLOAD_FOLDER/toggle.sh" "$WORK_DIR/opt/bypass/"
# Copy the binary if it exists
if [ -f "$PAYLOAD_FOLDER/hotkey_listener" ]; then
    cp "$PAYLOAD_FOLDER/hotkey_listener" "$WORK_DIR/opt/bypass/"
else
    echo "Warning: hotkey_listener binary not found. Did you compile it?"
fi
chmod +x "$WORK_DIR/opt/bypass/"*

# 4. Ensure dependencies (python3-evdev) - This is tricky in initrd,
# but we assume the live system has python3.
# We also need to make sure the scripts are available in the real root.

# 5. Modify 'init' to run our payload
# We look for the main init script. In most initrds it is at the root.
if [ -f "$WORK_DIR/init" ]; then
    echo "Patching /init script..."

    # We use a safer approach: find the place where the root is switched
    # and inject just before it, or append to the end if it's a script that
    # continues to run.

    cat << 'EOF' >> "$WORK_DIR/scripts/local-bottom/matura_bypass"
#!/bin/sh
PREREQ=""
prereqs() {
    echo "$PREREQ"
}
case $1 in
    prereqs)
        prereqs
        exit 0
        ;;
esac

# --- Matura Bypass Hook ---
# This script runs after the root filesystem is mounted but before switching to it.
# Live systems use a writable overlay (RAM based), so we copy to ${rootmnt}.

mkdir -p ${rootmnt}/opt/bypass
cp -r /opt/bypass/* ${rootmnt}/opt/bypass/

# For Debian Live, we can use live-config hooks which are executed automatically.
mkdir -p ${rootmnt}/lib/live/config
cat << 'INNER_EOF' > ${rootmnt}/lib/live/config/9999-matura-bypass
#!/bin/sh
# This runs inside the live system during boot
/bin/bash /opt/bypass/payload.sh &
INNER_EOF
chmod +x ${rootmnt}/lib/live/config/9999-matura-bypass

# Fallback: Also try rc.local
if [ -d "${rootmnt}/etc" ]; then
    if [ -f "${rootmnt}/etc/rc.local" ]; then
        sed -i '/exit 0/i /bin/bash /opt/bypass/payload.sh &' "${rootmnt}/etc/rc.local"
    else
        echo "#!/bin/bash\n/bin/bash /opt/bypass/payload.sh &\nexit 0" > "${rootmnt}/etc/rc.local"
    fi
    chmod +x "${rootmnt}/etc/rc.local"
fi
# --- End Matura Bypass Hook ---
EOF
    chmod +x "$WORK_DIR/scripts/local-bottom/matura_bypass"
else
    echo "Warning: /init script not found in root of initrd. Patching might fail."
fi

# 5. Repack
echo "Repacking initrd..."
find . | cpio -H newc -o | gzip -9 > "$INITRD_PATH"

echo "--- Patching Complete ---"
echo "Original backup kept at: $BACKUP_PATH"
rm -rf "$WORK_DIR"
