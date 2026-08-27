#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>

//运行实例./desktop 1(which) 1(status)
int main (int argc, char *argv[])
{
    int fd;
    char buf[1] = {0}; // Buffer to hold the value to write to the LED device
    // Open the GPIO device file
    fd = open("/dev/leddrv-dpled%d", O_RDWR, argv[1]);
    if (fd < 0) {
        perror("Failed to open /dev/leddrv-dpled");
        return -1;
    }

    // Turn on the LED
    write(fd, argv[2], 1);

    // Wait for a while
    sleep(1);

    // Turn off the LED
    buf[0] = '0'; // Assuming '0' turns off the LED
    write(fd, buf, 1);

    // Close the device file
    close(fd);

    return 0;
}