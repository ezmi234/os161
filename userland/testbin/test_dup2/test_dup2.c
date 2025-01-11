#include <unistd.h>
#include <stdio.h>
#include <fcntl.h>

int main(void) {
    int new_fd = 5; // File descriptor to duplicate to

    int fd = open("output.txt", O_WRONLY | O_CREAT | O_TRUNC, 0644); // Open file for writing
    printf("open; fd: %d\n", fd);
    if (fd == -1) {
        printf("Failed to open file");
        return 1; // Error opening the file
    }

    for (int i = 0; i <= new_fd; i++) {
        int result = dup2(i, new_fd);
        printf("fd: %d; ", i);
        if (result == new_fd) {
            printf("dup2 successful: fd %d duplicated to fd %d\n", i, new_fd);
        } else {
            printf("dup2 failed\n");
        }
    }
    
    /*
    int result = dup2(1, new_fd); // Duplicate stdout (fd 1) to new_fd
    if (result == new_fd) {
        printf("dup2 successful: fd %d duplicated to fd %d\n", fd, new_fd);
        //printf(new_fd, "This message is written to fd %d (duplicated stdout)\n", new_fd);
    } else {
        printf("dup2 failed");
    }
    */
    return 0;
}

/*
#include <unistd.h>
#include <stdio.h>
#include <fcntl.h>
#include <string.h>

int main(void) {
    const char *filename = "testfile.txt";

    // Apri un file in scrittura per testare dup2
    int fd = open(filename, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        printf("Error opening file");
        return 1;
    }

    int new_fd = fd; // Salviamo il file descriptor da duplicare
    int result = dup2(STDOUT_FILENO, new_fd); // Duplichiamo stdout su fd

    if (result == new_fd) {
        printf("dup2 successful: stdout duplicated to fd %d\n", new_fd);

        // Scrive un messaggio sul nuovo file descriptor (che punta a stdout duplicato)
        //dprintf(new_fd, "This message is written to fd %d (duplicated stdout)\n", new_fd);
    } else {
        printf("dup2 failed");
    }

    // Chiudiamo il file descriptor
    close(fd);

    return 0;
}
*/