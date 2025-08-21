#include <stdlib.h>
#include "model.h"
#include "discovery.h"
#include "watchlist.h"

#ifndef PROJECT_ROOT
#define PROJECT_ROOT "."
#endif

int main(int argc, char *argv[]) {
    (void)argc; (void)argv;

    DeviceInfo *devices = NULL;
    size_t count = 0;

    // 1) Discover devices (in-memory, no file persistence)
    int rc = discovery_collect_devices(&devices, &count);
    if (rc != 0) {
        free(devices);
        return rc;
    }

    // 2) Prompt user and write a single file with selection flags
    rc = watchlist_from_devices(devices, count, PROJECT_ROOT "/data/inventory.json");

    free(devices);
    return rc;
}