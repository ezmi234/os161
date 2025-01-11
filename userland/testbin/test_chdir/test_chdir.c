#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>

int main(void) {
    const char *new_dir = "/testbin";
    int result = chdir(new_dir);

    if (result == 0) {
        printf("Directory successfully changed to %s\n", new_dir);
    } else {
        printf("chdir failed");
    }
    return 0;
}