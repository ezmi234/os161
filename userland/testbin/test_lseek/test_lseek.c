#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>

int main() {
    int fd = open("test_write.txt", O_RDWR);
    if (fd < 0) {
        printf("open failed");
        return 1;
    }

    // Seek to the beginning of the file
    if (lseek(fd, 0, SEEK_SET) < 0) {
        printf("lseek failed");
        return 1;
    }
    printf("Seek to beginning successful.\n");

    // Seek to the end of the file
    off_t pos = lseek(fd, 0, SEEK_END);
    if (pos < 0) {
        printf("lseek failed");
        return 1;
    }
    printf("Seek to end successful, position: %ld\n", (long int)pos);

    close(fd);
    return 0;
}