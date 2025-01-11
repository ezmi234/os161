#include <unistd.h>
#include <stdio.h>

int main(void) {
    pid_t pid = getpid();
    printf("Current Process ID: %d\n", pid);
    return 0;
}