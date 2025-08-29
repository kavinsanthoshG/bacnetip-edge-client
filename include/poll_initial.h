#ifndef POLL_INITIAL_H
#define POLL_INITIAL_H

#include <stdint.h>
#include <stddef.h>
#include "inventory_objects.h"   /* for DeviceObjects */

/* Simple object descriptor (mirrors one entry in inventory_objects.json) */
typedef struct {
    uint16_t type_id;
    uint32_t instance;
} PollObject;

/* Poll (RPM, PROP_ALL) all properties for one device, all its objects.
 * Uses provided IP/port (BACnet/IP) to avoid Who-Is broadcast.
 * Prints results via rpm_ack_print_data (same as readpropm demo).
 * Returns 0 on success (all objects tried), >0 number of object failures, <0 fatal init error.
 */
int poll_device_all_objects(uint32_t device_instance,
                            const char *ip,
                            unsigned port,
                            const PollObject *objects,
                            size_t object_count);

/* Convenience: poll using a DeviceObjects entry (from inventory_objects.c). */
int poll_deviceobjects_all_properties(const DeviceObjects *dev);

/* Convenience: iterate array of DeviceObjects. Returns total failures. */
int poll_inventory_all_properties(const DeviceObjects *devices,
                                  size_t device_count);

#endif