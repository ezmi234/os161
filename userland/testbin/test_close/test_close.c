#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>

int main() {
    int fd = open("test_close.txt", O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd < 0) {
        printf("open failed");
        return 1;
    }

    printf("File opened with fd: %d\n", fd);
    
    if (close(fd) < 0) {
        printf("close failed");
        return 1;
    }
    printf("File closed successfully.\n");

    return 0;
}