#include <unistd.h>
#include <stdio.h>
#include <sys/types.h>

int main() {
    pid_t pid = fork();
    if (pid < 0) {
        printf("Fork failed\n");
        return 1;
    }
    if (pid == 0) {
        printf("I am the child process\n");
    } else {
        printf("I am the parent process, child PID: %d\n", pid);
    }
    return 0;
}
