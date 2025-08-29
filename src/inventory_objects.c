#include "inventory_objects.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "bacnet/bactext.h"

int inventory_objects_add(DeviceObjects **arr,
                          size_t *count,
                          size_t *capacity,
                          uint32_t device_instance,
                          const char *ip,
                          unsigned port,
                          const uint16_t *types,
                          const uint32_t *instances,
                          size_t obj_count)
{
    if (!arr || !count || !capacity) return -1;
    if (*count == *capacity) {
        size_t new_cap = (*capacity ? *capacity * 2 : 8);
        DeviceObjects *tmp = (DeviceObjects *)realloc(*arr, new_cap * sizeof(DeviceObjects));
        if (!tmp) return -1;
        *arr = tmp;
        *capacity = new_cap;
    }
    DeviceObjects *slot = &(*arr)[*count];
    memset(slot, 0, sizeof(*slot));
    slot->device_instance = device_instance;
    if (ip) {
        strncpy(slot->ip, ip, sizeof(slot->ip) - 1);
    }
    slot->port = port;
    slot->object_count = obj_count;
    if (obj_count) {
        slot->object_types = (uint16_t *)calloc(obj_count, sizeof(uint16_t));
        slot->object_instances = (uint32_t *)calloc(obj_count, sizeof(uint32_t));
        if (!slot->object_types || !slot->object_instances) {
            free(slot->object_types);
            free(slot->object_instances);
            memset(slot, 0, sizeof(*slot));
            return -1;
        }
        memcpy(slot->object_types, types, obj_count * sizeof(uint16_t));
        memcpy(slot->object_instances, instances, obj_count * sizeof(uint32_t));
    }
    (*count)++;
    return 0;
}

int inventory_objects_write(const char *path,
                            const DeviceObjects *arr,
                            size_t count)
{
    if (!path) return -1;
    FILE *f = fopen(path, "w");
    if (!f) {
        perror("fopen inventory_objects.json");
        return -1;
    }
    fprintf(f, "{\n  \"devices\": [\n");
    for (size_t i = 0; i < count; i++) {
        const DeviceObjects *d = &arr[i];
        fprintf(f,
            "    {\"device_instance\":%u,\"ip\":\"%s\",\"port\":%u,\"objects\":[",
            d->device_instance, d->ip, d->port);
        for (size_t j = 0; j < d->object_count; j++) {
            const char *tname = bactext_object_type_name(d->object_types[j]);
            fprintf(f,
                "%s{\"type\":\"%s\",\"type_id\":%u,\"instance\":%u}",
                (j ? "," : ""),
                tname ? tname : "Unknown",
                d->object_types[j],
                d->object_instances[j]);
        }
        fprintf(f, "]}%s\n", (i + 1 < count) ? "," : "");
    }
    fprintf(f, "  ]\n}\n");
    fclose(f);
    return 0;
}

void inventory_objects_free(DeviceObjects *arr, size_t count)
{
    if (!arr) return;
    for (size_t i = 0; i < count; i++) {
        free(arr[i].object_types);
        free(arr[i].object_instances);
    }
    free(arr);
}