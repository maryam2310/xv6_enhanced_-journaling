#include "types.h"
#include "stat.h"
#include "user.h"

int main() {
    printf(1, "Sleeping for 5 seconds...\n");
    sleep(500);   // 5 seconds (500 ticks)
    printf(1, "Awake now!\n");
    exit();
}
