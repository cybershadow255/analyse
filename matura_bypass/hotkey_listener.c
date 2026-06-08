#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <linux/input.h>
#include <string.h>

// Configuration
// Ctrl+Shift+K. Note: Scancodes might vary, but these are standard.
#define KEY1 KEY_LEFTCTRL
#define KEY2 KEY_LEFTSHIFT
#define KEY3 KEY_K
#define TOGGLE_SCRIPT "/opt/bypass/toggle.sh"

int main() {
    int fd;
    struct input_event ev;
    char name[256] = "Unknown";
    char dev_path[32];
    int key1_pressed = 0, key2_pressed = 0, key3_pressed = 0;

    // Try to find the keyboard device
    for (int i = 0; i < 32; i++) {
        sprintf(dev_path, "/dev/input/event%d", i);
        fd = open(dev_path, O_RDONLY);
        if (fd != -1) {
            ioctl(fd, EVIOCGNAME(sizeof(name)), name);
            if (strstr(name, "eyboard") || strstr(name, "EYBOARD")) {
                printf("Found keyboard at %s: %s\n", dev_path, name);
                break;
            }
            close(fd);
            fd = -1;
        }
    }

    if (fd == -1) {
        perror("No keyboard found");
        return 1;
    }

    // Grab the device to receive events (optional, might block other apps)
    // ioctl(fd, EVIOCGRAB, 1);

    while (1) {
        read(fd, &ev, sizeof(struct input_event));
        if (ev.type == EV_KEY) {
            if (ev.code == KEY1) key1_pressed = (ev.value > 0);
            if (ev.code == KEY2) key2_pressed = (ev.value > 0);
            if (ev.code == KEY3) key3_pressed = (ev.value > 0);

            if (key1_pressed && key2_pressed && key3_pressed && ev.value == 1) {
                // Hotkey triggered!
                system(TOGGLE_SCRIPT);
            }
        }
    }

    close(fd);
    return 0;
}
