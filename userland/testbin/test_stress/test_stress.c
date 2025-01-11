#include <unistd.h>
#include <sys/wait.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>

#define NUM_CHILDREN 5

int main() {
    pid_t pids[NUM_CHILDREN];

    // Create multiple child processes
    for (int i = 0; i < NUM_CHILDREN; i++) {
        pids[i] = fork();
        if (pids[i] < 0) {
            // Fork failed
            printf("Fork failed for child %d\n", i);
            exit(1);
        }

        if (pids[i] == 0) {
            // Child process
            printf("Child %d: My PID is %d. Exiting with status %d.\n", i, getpid(), i + 1);
            _exit(i + 1);
        }
    }

    // Parent process waits for all children
    for (int i = 0; i < NUM_CHILDREN; i++) {
        int status;
        pid_t waited_pid = waitpid(pids[i], &status, 0);
        if (waited_pid > 0) {
            printf("Parent: Child %d exited with status %d.\n", waited_pid, WEXITSTATUS(status));
        } else {
            printf("Parent: waitpid failed for child %d\n", pids[i]);
        }
    }

    return 0;
}
