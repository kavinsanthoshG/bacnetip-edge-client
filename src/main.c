#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include "model.h"
#include "discovery.h"
#include "watchlist.h"
#include "object_list.h"
#include "bacnet/bacdef.h"
#include "bacnet/bactext.h"
#ifndef PROJECT_ROOT
#define PROJECT_ROOT "."
#endif

int main(int argc, char *argv[]) {
    (void)argc; (void)argv;
    uint32_t device_instance     = 2523247;
    uint16_t object_type         = OBJECT_DEVICE;   /* 8 */
    uint32_t object_instance     = 2523247;
    uint32_t property_id         = PROP_OBJECT_LIST; /* 76 */
    DeviceInfo *devices = NULL;
    size_t count = 0;
BACnetObjectList list;
    int rc = bacnet_read_object_list(
        device_instance,
        object_type,
        object_instance,
        property_id,
        &list);
    // 1) Discover devices (in-memory, no file persistence)
    // int rc = discovery_collect_devices(&devices, &count);
    // if (rc != 0) {
    //     free(devices);
    //     return rc;
    // }
        if (rc != 0) {
        fprintf(stderr, "ReadProperty(Object_List) failed (rc=%d)\n", rc);
        return 1;
    }

    // 2) Prompt user and write a single file with selection flags
    rc = watchlist_from_devices(devices, count, PROJECT_ROOT "/data/inventory.json");
        printf("Object_List for device %u: %zu entries\n",
           device_instance, list.count);
    for (size_t i = 0; i < list.count; i++) {
        const char *type_name = bactext_object_type_name(list.object_types[i]);
        printf("  %zu: %s %u\n",
               i,
               type_name ? type_name : "Unknown",
               list.object_instances[i]);
    }

    bacnet_object_list_free(&list);
    free(devices);
    return rc;
}