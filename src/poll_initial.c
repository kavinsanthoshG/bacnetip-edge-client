// Reworked from demo readpropm: now provides callable functions (no main)
// NOTE: Only RPM (ReadPropertyMultiple) with PROP_ALL per object.

#include "poll_initial.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#if (__STDC_VERSION__ >= 199901L) && defined(__STDC_ISO_10646__)
#include <locale.h>
#endif
#include <stdbool.h>

/* BACnet Stack */
#include "bacnet/bacdef.h"
#include "bacnet/bactext.h"
#include "bacnet/bacerror.h"
#include "bacnet/iam.h"
#include "bacnet/rpm.h"
#include "bacnet/npdu.h"
#include "bacnet/apdu.h"
#include "bacnet/whois.h"
#include "bacnet/version.h"
#include "bacnet/basic/binding/address.h"
#include "bacnet/basic/object/device.h"
#include "bacnet/basic/sys/filename.h"
#include "bacnet/basic/services.h"
#include "bacnet/basic/tsm/tsm.h"
#include "bacnet/datalink/datalink.h"
#include "bacnet/datalink/dlenv.h"
#include "bacport.h"

#if BACNET_SVC_SERVER
#error "Disable server features (BACNET_SVC_SERVER=0) for this client poller."
#endif

/* ---- Internal State (mirrors original demo) ---- */
static uint8_t Rx_Buf[MAX_MPDU] = {0};

static uint32_t Target_Device_Object_Instance = BACNET_MAX_INSTANCE;
static BACNET_READ_ACCESS_DATA *Read_Access_Data = NULL;
static uint8_t Request_Invoke_ID = 0;
static BACNET_ADDRESS Target_Address;
static bool Error_Detected = false;

/* Forward */
static void free_read_access_chain(BACNET_READ_ACCESS_DATA *head);

static void MyErrorHandler(
    BACNET_ADDRESS *src,
    uint8_t invoke_id,
    BACNET_ERROR_CLASS error_class,
    BACNET_ERROR_CODE error_code)
{
    if (address_match(&Target_Address, src) &&
        (invoke_id == Request_Invoke_ID)) {
        fprintf(stderr, "BACnet Error: %s: %s\n",
            bactext_error_class_name(error_class),
            bactext_error_code_name(error_code));
        Error_Detected = true;
    }
}

static void MyAbortHandler(
    BACNET_ADDRESS *src, uint8_t invoke_id, uint8_t abort_reason, bool server)
{
    (void)server;
    if (address_match(&Target_Address, src) &&
        (invoke_id == Request_Invoke_ID)) {
        fprintf(stderr, "BACnet Abort: %s\n",
            bactext_abort_reason_name(abort_reason));
        Error_Detected = true;
    }
}

static void MyRejectHandler(
    BACNET_ADDRESS *src, uint8_t invoke_id, uint8_t reject_reason)
{
    if (address_match(&Target_Address, src) &&
        (invoke_id == Request_Invoke_ID)) {
        fprintf(stderr, "BACnet Reject: %s\n",
            bactext_reject_reason_name(reject_reason));
        Error_Detected = true;
    }
}

/* ACK handler: decode and print (library helper prints properties). */
static void My_Read_Property_Multiple_Ack_Handler(
    uint8_t *service_request,
    uint16_t service_len,
    BACNET_ADDRESS *src,
    BACNET_CONFIRMED_SERVICE_ACK_DATA *service_data)
{
    if (!address_match(&Target_Address, src) ||
        service_data->invoke_id != Request_Invoke_ID) {
        return;
    }

    BACNET_READ_ACCESS_DATA *rpm_data = calloc(1, sizeof(BACNET_READ_ACCESS_DATA));
    if (!rpm_data) {
        fprintf(stderr, "OOM decoding RPM Ack\n");
        Error_Detected = true;
        return;
    }
    int len = rpm_ack_decode_service_request(
        service_request, service_len, rpm_data);
    if (len > 0) {
        while (rpm_data) {
            rpm_ack_print_data(rpm_data);
            rpm_data = rpm_data_free(rpm_data);
        }
    } else {
        fprintf(stderr, "RPM Ack Malformed (%d)\n", len);
        /* manual free of partial chain */
        while (rpm_data) {
            BACNET_READ_ACCESS_DATA *next = rpm_data->next;
            free(rpm_data);
            rpm_data = next;
        }
        Error_Detected = true;
    }
}

static void Init_Service_Handlers(void)
{
    Device_Init(NULL);
    apdu_set_unconfirmed_handler(SERVICE_UNCONFIRMED_I_AM, handler_i_am_bind);
    apdu_set_unrecognized_service_handler_handler(handler_unrecognized_service);
    apdu_set_confirmed_handler(SERVICE_CONFIRMED_READ_PROPERTY, handler_read_property);
    apdu_set_confirmed_ack_handler(
        SERVICE_CONFIRMED_READ_PROP_MULTIPLE,
        My_Read_Property_Multiple_Ack_Handler);
    apdu_set_error_handler(SERVICE_CONFIRMED_READ_PROPERTY, MyErrorHandler);
    apdu_set_abort_handler(MyAbortHandler);
    apdu_set_reject_handler(MyRejectHandler);
}

static void free_read_access_chain(BACNET_READ_ACCESS_DATA *head)
{
    while (head) {
        BACNET_READ_ACCESS_DATA *next = head->next;
        BACNET_PROPERTY_REFERENCE *p = head->listOfProperties;
        while (p) {
            BACNET_PROPERTY_REFERENCE *pn = p->next;
            free(p);
            p = pn;
        }
        free(head);
        head = next;
    }
}

/* Build one-object RPM chain requesting PROP_ALL */
static BACNET_READ_ACCESS_DATA *build_single_object_rpm(
    uint16_t obj_type, uint32_t obj_instance)
{
    BACNET_READ_ACCESS_DATA *obj =
        (BACNET_READ_ACCESS_DATA *)calloc(1, sizeof(BACNET_READ_ACCESS_DATA));
    if (!obj) return NULL;
    obj->object_type = obj_type;
    obj->object_instance = obj_instance;

    BACNET_PROPERTY_REFERENCE *prop =
        (BACNET_PROPERTY_REFERENCE *)calloc(1, sizeof(BACNET_PROPERTY_REFERENCE));
    if (!prop) {
        free(obj);
        return NULL;
    }
    /* PROP_ALL == 8 (special for RPM) */
    prop->propertyIdentifier = PROP_ALL;
    prop->propertyArrayIndex = BACNET_ARRAY_ALL;
    prop->next = NULL;
    obj->listOfProperties = prop;
    obj->next = NULL;
    return obj;
}

/* Add direct address (avoid broadcast) for BACnet/IP */
static void add_direct_device_address(uint32_t device_instance,
                                      const char *ip,
                                      unsigned port)
{
    if (!ip || !*ip) return;
    unsigned o1, o2, o3, o4;
    if (sscanf(ip, "%u.%u.%u.%u", &o1, &o2, &o3, &o4) != 4) return;
    BACNET_ADDRESS dest = {0};
    dest.mac[0] = (uint8_t)o1;
    dest.mac[1] = (uint8_t)o2;
    dest.mac[2] = (uint8_t)o3;
    dest.mac[3] = (uint8_t)o4;
    dest.mac[4] = (uint8_t)(port >> 8);
    dest.mac[5] = (uint8_t)(port & 0xFF);
    dest.mac_len = 6;
    dest.net = 0;  /* local */
    dest.len = 0;  /* no routing SADR */
    address_add(device_instance, MAX_APDU, &dest);
}

/* Ensure we have a binding (sends Who-Is if necessary) */
static bool ensure_binding(uint32_t device_instance, unsigned *out_max_apdu)
{
    unsigned max_apdu = 0;
    bool bound = address_bind_request(device_instance, &max_apdu, &Target_Address);
    if (!bound) {
        Send_WhoIs(device_instance, device_instance);
        /* Simple wait loop (non-blocking incremental) handled in main cycle */
    }
    if (out_max_apdu) *out_max_apdu = max_apdu;
    return bound;
}

/* Poll all objects (one RPM per object) */
int poll_device_all_objects(uint32_t device_instance,
                            const char *ip,
                            unsigned port,
                            const PollObject *objects,
                            size_t object_count)
{
    if (!objects || object_count == 0) return 0;

    /* Init stack once */
    Device_Set_Object_Instance_Number(BACNET_MAX_INSTANCE);
    address_init();
    dlenv_init();
    static bool cleanup_registered = false;
    if (!cleanup_registered) {
        atexit(datalink_cleanup);
        cleanup_registered = true;
    }
#if (__STDC_VERSION__ >= 199901L) && defined(__STDC_ISO_10646__)
    setlocale(LC_ALL, "");
#endif
    Init_Service_Handlers();

    Target_Device_Object_Instance = device_instance;
    Error_Detected = false;

    /* Direct address to skip I-Am if IP provided */
    add_direct_device_address(device_instance, ip, port);

    unsigned max_apdu = 0;
    bool bound = ensure_binding(device_instance, &max_apdu);

    int failure_count = 0;

    for (size_t i = 0; i < object_count; i++) {
        const PollObject *obj = &objects[i];
        printf("\n-- Device %u Object %s (%u) instance %u --\n",
               device_instance,
               bactext_object_type_name(obj->type_id),
               obj->type_id,
               obj->instance);

        Read_Access_Data = build_single_object_rpm(obj->type_id, obj->instance);
        if (!Read_Access_Data) {
            fprintf(stderr, "  OOM building RPM request\n");
            failure_count++;
            continue;
        }

        Request_Invoke_ID = 0;
        Error_Detected = false;

        time_t last_seconds = time(NULL);
        time_t current_seconds;
        unsigned timeout_ms = 100;
        time_t timeout_seconds = (apdu_timeout() / 1000) * apdu_retries();
        time_t elapsed_seconds = 0;

        /* Main per-object loop */
        for (;;) {
            current_seconds = time(NULL);
            if (current_seconds != last_seconds) {
                tsm_timer_milliseconds(
                    (uint16_t)((current_seconds - last_seconds) * 1000));
                datalink_maintenance_timer(current_seconds - last_seconds);
            }
            if (Error_Detected) break;

            if (!bound) {
                bound = ensure_binding(device_instance, &max_apdu);
            }
            if (bound) {
                if (Request_Invoke_ID == 0) {
                    Request_Invoke_ID = Send_Read_Property_Multiple_Request(
                        NULL, 0, device_instance, Read_Access_Data);
                    if (Request_Invoke_ID == 0) {
                        fprintf(stderr, "  Failed to send RPM request\n");
                        Error_Detected = true;
                        break;
                    }
                } else if (tsm_invoke_id_free(Request_Invoke_ID)) {
                    /* Completed */
                    break;
                } else if (tsm_invoke_id_failed(Request_Invoke_ID)) {
                    fprintf(stderr, "  TSM Timeout\n");
                    tsm_free_invoke_id(Request_Invoke_ID);
                    Error_Detected = true;
                    break;
                }
            } else {
                elapsed_seconds += (current_seconds - last_seconds);
                if (elapsed_seconds > timeout_seconds) {
                    fprintf(stderr, "  APDU Timeout waiting for bind (device %u)\n",
                            device_instance);
                    Error_Detected = true;
                    break;
                }
            }

            BACNET_ADDRESS src = {0};
            uint16_t pdu_len = datalink_receive(
                &src, Rx_Buf, MAX_MPDU, timeout_ms);
            if (pdu_len) {
                npdu_handler(&src, Rx_Buf, pdu_len);
            }
            last_seconds = current_seconds;
        }

        free_read_access_chain(Read_Access_Data);
        Read_Access_Data = NULL;

            if (Error_Detected) {
            failure_count++;
        }
    }

    return failure_count;
}

int poll_deviceobjects_all_properties(const DeviceObjects *dev)
{
    if (!dev) return -1;
    /* Build array of PollObject from DeviceObjects */
    PollObject *objs = NULL;
    if (dev->object_count) {
        objs = (PollObject *)calloc(dev->object_count, sizeof(PollObject));
        if (!objs) return -1;
        for (size_t i = 0; i < dev->object_count; i++) {
            objs[i].type_id = dev->object_types[i];
            objs[i].instance = dev->object_instances[i];
        }
    }
    int rc = poll_device_all_objects(
        dev->device_instance,
        dev->ip,
        dev->port,
        objs,
        dev->object_count);
    free(objs);
    return rc;
}

int poll_inventory_all_properties(const DeviceObjects *devices,
                                  size_t device_count)
{
    if (!devices) return -1;
    int total_failures = 0;
    for (size_t i = 0; i < device_count; i++) {
        printf("\n========== Polling Device %u (%s:%u) ==========\n",
               devices[i].device_instance,
               devices[i].ip,
               devices[i].port);
        int rc = poll_deviceobjects_all_properties(&devices[i]);
        if (rc > 0) {
            total_failures += rc;
            printf("Device %u: %d object failures\n",
                   devices[i].device_instance, rc);
        }
    }
    return total_failures;
}

/* (Optional) If you want a standalone test binary for this module:
   Uncomment below and add compile definition POLL_INITIAL_STANDALONE
#if defined(POLL_INITIAL_STANDALONE)
int main(int argc, char *argv[]) {
    (void)argc; (void)argv;
    // Example single device/object poll (device 2523247, analog-input 0)
    PollObject po = { OBJECT_ANALOG_INPUT, 0 };
    poll_device_all_objects(2523247, "192.168.1.100", 65348, &po, 1);
    return 0;
}
#endif
*/