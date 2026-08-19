#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

int main (int argc, char *argv[])
{
    int fd;
    char buf[2] = {0};

    // Open the GPIO device file
    fd = open("/dev/my_led", O_RDWR);
    if (fd < 0) {
        perror("Failed to open /dev/my_led");
        return -1;
    }

    // Turn on the LED
    buf[0] = '1'; // Assuming '1' turns on the LED
    write(fd, buf, 1);

    // Wait for a while
    sleep(1);

    // Turn off the LED
    buf[0] = '0'; // Assuming '0' turns off the LED
    write(fd, buf, 1);

    // Close the device file
    close(fd);

    return 0;
}