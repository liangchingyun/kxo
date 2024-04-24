#include <fcntl.h>
#include <getopt.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <termios.h>
#include <unistd.h>

#include "game.h"

#define KMLDRV_STATUS_FILE "/sys/module/kmldrv/initstate"
#define KMLDRV_DEVICE_FILE "/dev/kmldrv"
#define KMLDRV_DEVICE_ATTR_FILE "/sys/class/kmldrv/kmldrv/kmldrv_state"

bool kmldrv_status_check(void)
{
    FILE *fp = fopen(KMLDRV_STATUS_FILE, "r");
    if (!fp) {
        printf("kmldrv status : not loaded\n");
        return false;
    }

    char read_buf[20];
    fgets(read_buf, 20, fp);
    read_buf[strcspn(read_buf, "\n")] = 0;
    if (!strcmp("live", read_buf))
        printf("kmldrv status : live\n");
    else {
        printf("kmldrv status : %s\n", read_buf);
        fclose(fp);
        return false;
    }
    fclose(fp);
    return true;
}

static struct termios orig_termios;

static void disableRawMode(void)
{
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
}

static void enableRawMode(void)
{
    tcgetattr(STDIN_FILENO, &orig_termios);
    atexit(disableRawMode);
    struct termios raw = orig_termios;
    raw.c_lflag &= ~(ECHO | ICANON);
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
}

static bool read_attr;

static void listen_keyboard_handler(void)
{
    int attr_fd = open(KMLDRV_DEVICE_ATTR_FILE, O_RDWR);
    char input;

    if (read(STDIN_FILENO, &input, 1) == 1) {
        char buf[20];
        switch (input) {
        case 16:
            read(attr_fd, buf, 6);
            buf[0] = (buf[0] - '0') ? '0' : '1';
            read_attr = !read_attr;
            write(attr_fd, buf, 6);
            break;
        case 17:
            read(attr_fd, buf, 6);
            buf[4] = '1';
            write(attr_fd, buf, 6);
            break;
        }
    }
    close(attr_fd);
}

int main(int argc, char *argv[])
{
    int c;

    while ((c = getopt(argc, argv, "d:s:ch")) != -1) {
        switch (c) {
        case 'h':
            printf(
                "kmldrv-user : A userspace tool which supports interactions "
                "with kmldrv from user-level\n");
            printf("Usage:\n\n");
            printf("\t./kmldrv-user [arguments]\n\n");
            printf("Arguments:\n\n");
            printf("\t--start - start a tic-tac-toe game\n");
            printf("\t--release - release kmldrv\n\n");
            printf("Control Options:\n\n");
            printf("\t Ctrl + S - Start a tic-tac-toe game\n");
            printf("\t Ctrl + P - Pause to show the game\n");
            printf("\t Ctrl + C - Continue to show the game\n");
            printf("\t Ctrl + R - Restart a tic-tac-toe game\n");
            printf("\t Ctrl + Q - Stop the tic-tac-toe game\n");
            return 0;
        default:
            printf("Invalid arguments\n");
            break;
        }
    }

    if (!kmldrv_status_check())
        exit(1);

    enableRawMode();
    int flags = fcntl(STDIN_FILENO, F_GETFL, 0);
    fcntl(STDIN_FILENO, F_SETFL, flags | O_NONBLOCK);

    char display_buf[DRAWBUFFER_SIZE];

    fd_set readset;
    int device_fd = open(KMLDRV_DEVICE_FILE, O_RDONLY);
    int max_fd = device_fd > STDIN_FILENO ? device_fd : STDIN_FILENO;
    read_attr = true;

    while (1) {
        FD_ZERO(&readset);
        FD_SET(STDIN_FILENO, &readset);
        FD_SET(device_fd, &readset);

        int result = select(max_fd + 1, &readset, NULL, NULL, NULL);
        if (result < 0) {
            printf("Error with select system call\n");
            exit(1);
        }

        if (FD_ISSET(STDIN_FILENO, &readset)) {
            FD_CLR(STDIN_FILENO, &readset);
            listen_keyboard_handler();
        } else if (read_attr && FD_ISSET(device_fd, &readset)) {
            FD_CLR(device_fd, &readset);
            read(device_fd, display_buf, DRAWBUFFER_SIZE);
            printf("%s", display_buf);
        }
    }

    disableRawMode();
    fcntl(STDIN_FILENO, F_SETFL, flags);

    close(device_fd);

    return 0;
}