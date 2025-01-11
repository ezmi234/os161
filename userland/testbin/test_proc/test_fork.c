#include <unistd.h>
#include <sys/wait.h>
#include <stdio.h>

int main() {
    pid_t pid = fork();
    if (pid == 0) {
        printf("Child process starting.\n");
        _exit(42);  // Exit with code 42
    } else {
        int status;
        pid_t wpid = waitpid(pid, &status, 0);
        if (wpid > 0) {
            printf("Parent: Child %d exited with status %d\n", wpid, WEXITSTATUS(status));
        } else {
            printf("Parent: waitpid failed\n");
        }
    }
    return 0;
}
