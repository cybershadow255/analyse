# Matura Bypass Toolkit

This toolkit is designed to bypass restrictions on a Linux-based bootable USB stick (e.g., Debian Live). It allows you to pause the keylogger, access your Windows C: drive, and enable internet access.

## Prerequisites

1. **WSL (Windows Subsystem for Linux)**: Installed on your Windows laptop.
2. **Tools**: Inside WSL, install the following:
   ```bash
   sudo apt update
   sudo apt install -y cpio xz-utils file build-essential
   ```

## Setup Instructions

0. **Compile the Hotkey Listener**:
   Inside the `matura_bypass` folder in WSL:
   ```bash
   gcc hotkey_listener.c -o hotkey_listener
   ```

1. **Identify the USB Stick**:
   Plug the USB stick into your laptop while Windows is running. Note the drive letter (e.g., `D:`).
   In WSL, find where it is mounted (usually `/mnt/d`).
   Run the discovery script:
   ```bash
   bash discover_stick.sh /mnt/d
   ```
   Look for the path to the `initrd.img` file (it might be in a folder like `live` or `boot`).

2. **Apply the Patch**:
   Once you have the path to `initrd.img`, run the patcher (replace the path with your actual path):
   ```bash
   sudo bash patch_usb.sh /mnt/d/live/initrd.img .
   ```
   *This will create a backup named `initrd.img.bak` on the stick automatically.*

3. **Usage During the Challenge**:
   - Boot from the USB stick.
   - **Keylogger Toggle**: Press `Ctrl + Shift + K` to pause or resume the keylogger.
   - **Windows Drive**: Your C: drive should be automatically mounted at `/media/windows_c`.
   - **Internet**: The script attempts to restart networking. If you need a browser, you may need to install one or use a portable version.

4. **Restore the Stick**:
   After your challenge, put the stick back into Windows and run the restore script in WSL:
   ```bash
   sudo bash restore_usb.sh /mnt/d/live/initrd.img
   ```

## Disclaimer
This tool is for educational purposes and "challenges" as described. Use responsibly.
