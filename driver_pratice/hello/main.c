#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

int main(int argc, char **argv){
    int fd;
    char buf[1024];
    int len;

    if(argc < 2){
        printf("usage : %s -w <string>\n", argv[0]);
        printf("usage   %s -r\n", argv[0]);
        return -1;
    }
    fd = open("/dev/hello", O_RDWR);
    if(fd == -1){
        printf("cannot open file \n");
        return -1;
    }
    if((0 == strcmp(argv[1], "-w")) && (argc == 3)){
        len = strlen(argv[2]) + 1;
        len = len < 1024 ? len : 1024;
        write(fd, argv[2], len);
    }
    else{
        len = read(fd, buf, 1024);
        buf[1023] = '\0';
        printf("read : %s", buf);
    }
    close(fd);
    return 0;
}
