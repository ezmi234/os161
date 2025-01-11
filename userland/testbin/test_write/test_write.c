#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>

int main() {
    const char *message = "Hello, sys_write!\n";

    // Writing to standard output (fd = 1)
    ssize_t bytes_written = write(1, message, strlen(message));
    if (bytes_written < 0) {
        printf("write to stdout failed");
        return 1;
    }
    printf("Successfully wrote %zd bytes to stdout.\n", bytes_written);

    // Writing to a file
    int fd = open("test_write.txt", O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd < 0) {
        printf("open failed");
        return 1;
    }

    bytes_written = write(fd, message, strlen(message));
    if (bytes_written < 0) {
        printf("write to file failed");
        return 1;
    }
    printf("Successfully wrote %zd bytes to file.\n", bytes_written);

    close(fd);
    return 0;
}
