#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

#include "model.h"
#include "discovery.h"
#include "object_list.h"
#include "watchlist.h"
#include "bacnet/bacdef.h"
#include "bacnet/bactext.h"

#ifndef PROJECT_ROOT
#define PROJECT_ROOT "."
#endif

int main(int argc, char *argv[]) {
    (void)argc; (void)argv;

    DeviceInfo *devices = NULL;
    size_t count = 0;

    int rc = discovery_collect_devices(&devices, &count);
    if (rc != 0) {
        fprintf(stderr, "Discovery failed rc=%d\n", rc);
        free(devices);
        return rc;
    }

    if (count == 0) {
        printf("No devices discovered.\n");
        free(devices);
        return 0;
    }
    watchlist_from_devices(devices, count, PROJECT_ROOT "/data/inventory.json");
    printf("Discovered %zu devices. Reading Object_List for each...\n", count);

    int failures = 0;
    for (size_t i = 0; i < count; i++) {
        uint32_t device_instance = devices[i].device_instance;
        printf("\n[%zu/%zu] Device %u (%s:%u)\n",
               i + 1, count,
               device_instance,
               devices[i].ip[0] ? devices[i].ip : "-",
               devices[i].port);

        BACnetObjectList list;
        int orc = bacnet_read_object_list(
            device_instance,
            OBJECT_DEVICE,
            device_instance,
            PROP_OBJECT_LIST,
            &list);

        if (orc != 0) {
            fprintf(stderr, "  Object_List read failed rc=%d\n", orc);
            failures++;
            continue;
        }

        printf("  Object_List entries: %zu\n", list.count);
        for (size_t j = 0; j < list.count; j++) {
            const char *tname = bactext_object_type_name(list.object_types[j]);
            printf("    %zu: %s %u\n",
                   j,
                   tname ? tname : "Unknown",
                   list.object_instances[j]);
        }
        bacnet_object_list_free(&list);
    }

    free(devices);

    if (failures) {
        printf("\nCompleted with %d failures.\n", failures);
        return 1;
    }
    printf("\nAll device Object_List reads succeeded.\n");
     return 0;
}

