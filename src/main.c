#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

#include "poll_initial.h"     /* poll_device_all_objects */
#include "bacnet/bacdef.h"    /* OBJECT_*, PROP_ALL names */
#include "bacnet/bactext.h"   /* bactext_object_type_name (printing inside poll) */

#ifndef PROJECT_ROOT
#define PROJECT_ROOT "."
#endif

int main(int argc, char *argv[]) {
    (void)argc; (void)argv;

    /* Hard‑coded inventory (single device with its objects) */
    uint32_t device_instance = 2523247;
    const char *ip = "192.168.1.100";
    unsigned port = 65348;

    /* List from provided JSON (order preserved). Each gets an RPM (PROP_ALL). */
    static const PollObject objects[] = {
        { OBJECT_DEVICE,          2523247 },
        { OBJECT_ANALOG_INPUT,    0 },
        { OBJECT_ANALOG_INPUT,    1 },
        { OBJECT_ANALOG_INPUT,    2 },
        { OBJECT_ANALOG_VALUE,    0 },
        { OBJECT_ANALOG_VALUE,    1 },
        { OBJECT_ANALOG_VALUE,    2 },
        { OBJECT_ANALOG_VALUE,    3 },
        { OBJECT_CHARACTERSTRING_VALUE, 1 },
        { OBJECT_BINARY_VALUE,    0 },
        { OBJECT_BINARY_VALUE,    1 },
        { OBJECT_MULTI_STATE_VALUE, 0 },
        { OBJECT_MULTI_STATE_VALUE, 1 }
    };

    size_t object_count = sizeof(objects)/sizeof(objects[0]);

    printf("Polling device %u (%s:%u) - %zu objects (PROP_ALL each)...\n",
           device_instance, ip, port, object_count);

    int failures = poll_device_all_objects(device_instance,
                                           ip,
                                           port,
                                           objects,
                                           object_count);

    if (failures > 0) {
        printf("Completed with %d object polling failures.\n", failures);
        return 1;
    }
printf("All objects polled successfully.\n");
    return 0;
}