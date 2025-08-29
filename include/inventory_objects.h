// Suggested new files:
//   include/inventory_objects.h
//   src/inventory_objects.c
// Purpose: collect per-device Object_List results and write data/inventory_objects.json

// filepath: /home/kavin/bacnet-client-app/bacnet-edge-clientapp/include/inventory_objects.h
#ifndef INVENTORY_OBJECTS_H
#define INVENTORY_OBJECTS_H
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t device_instance;
    char ip[64];
    unsigned port;
    size_t object_count;
    uint16_t *object_types;
    uint32_t *object_instances;
} DeviceObjects;

/* Append one device + its object list (deep copy) */
int inventory_objects_add(DeviceObjects **arr,
                          size_t *count,
                          size_t *capacity,
                          uint32_t device_instance,
                          const char *ip,
                          unsigned port,
                          const uint16_t *types,
                          const uint32_t *instances,
                          size_t obj_count);

/* Write JSON file:
 {
   "devices":[
     {"device_instance":X,"ip":"a.b.c.d","port":47808,
      "objects":[{"type":"analogInput","type_id":0,"instance":1}, ...]},
     ...
   ]
 }
*/
int inventory_objects_write(const char *path,
                            const DeviceObjects *arr,
                            size_t count);

/* Free all allocated memory (and array itself) */
void inventory_objects_free(DeviceObjects *arr, size_t count);

#ifdef __cplusplus
}
#endif
#endif