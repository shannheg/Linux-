#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

//运行实例./dpdrv 1(which) 1(status)
int main(int argc, char *argv[])
{
    char path[64];
    int fd;
    int which;

    if (argc != 3 || (argv[2][0] != '0' && argv[2][0] != '1') ||
        argv[2][1] != '\0') {
        fprintf(stderr, "Usage: %s <led-number> <0|1>\n", argv[0]);
        return 1;
    }

    which = strtol(argv[1], NULL, 10);
    if (which < 0) {
        fprintf(stderr, "LED number must be non-negative\n");
        return 1;
    }

    // Open the GPIO device file
    snprintf(path, sizeof(path), "/dev/leddrv-dpled%d", which);
    fd = open(path, O_RDWR);
    if (fd < 0) {
        perror(path);
        return 1;
    }

    // Turn on or turn off the LED according to argv[2]
    if (write(fd, argv[2], 1) != 1) {
        perror("write");
        close(fd);
        return 1;
    }

    // Close the device file
    close(fd);
    return 0;
}
