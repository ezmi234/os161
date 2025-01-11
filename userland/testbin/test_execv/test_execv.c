#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>

int main() {
    char *args[] = { (char *)"testbin/palin", NULL };  
    printf("Calling execv to run hello...\n");
    if (execv("/testbin/palin", args) < 0) {
        printf("execv failed");
        exit(1);
    }
    return 0;  // Should not reach here
}
