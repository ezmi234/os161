#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <sys/types.h>

int main() {
    char buffer[50];

    // Reading from stdin
    printf("Enter something: ");
    ssize_t bytes_read = read(0, buffer, sizeof(buffer) - 1);
    if (bytes_read < 0) {
        printf("read failed");
        return 1;
    }
    buffer[bytes_read] = '\0';
    printf("Read from stdin: %s\n", buffer);

    // Reading from a file
    int fd = open("test_write.txt", O_RDONLY);
    if (fd < 0) {
        printf("open failed");
        return 1;
    }

    bytes_read = read(fd, buffer, sizeof(buffer) - 1);
    if (bytes_read < 0) {
        printf("read failed");
        return 1;
    }
    buffer[bytes_read] = '\0';
    printf("Read from file: %s\n", buffer);

    close(fd);
    return 0;
}
