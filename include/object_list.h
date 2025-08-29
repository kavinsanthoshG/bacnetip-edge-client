#ifndef OBJECT_LIST_READ_H
#define OBJECT_LIST_READ_H
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    size_t count;
    size_t capacity;
    /* Parallel arrays of object identifiers */
    uint16_t *object_types;
    uint32_t *object_instances;
} BACnetObjectList;

/* Initialize (allocates initial capacity) */
int bacnet_object_list_init(BACnetObjectList *ol);
/* Free contents */
void bacnet_object_list_free(BACnetObjectList *ol);

/* Perform a ReadProperty(Device/Object/... Property, index=-1) and fill list if property==PROP_OBJECT_LIST.
 * Arguments:
 *  device_instance  - target device instance
 *  object_type      - object type to read (use OBJECT_DEVICE for object list)
 *  object_instance  - object instance for that type
 *  property_id      - property (use PROP_OBJECT_LIST to get full list)
 * Returns 0 on success, <0 on error (timeout, decode, etc.)
 */
int bacnet_read_object_list(uint32_t device_instance,
                            uint16_t object_type,
                            uint32_t object_instance,
                            uint32_t property_id,
                            BACnetObjectList *out_list);

#ifdef __cplusplus
}
#endif
#endif