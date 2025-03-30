#include <unistd.h>
#include <fcntl.h>
#include <stdio.h> 
#include <string.h> 
#include <stdlib.h> 
#include <sys/types.h> 

int main() {
    int fd = open("test_open.txt", O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd < 0) {
        printf("open failed");
        return 1;
    }

    printf("Opened file with fd: %d\n", fd);
    close(fd);

    return 0;
}