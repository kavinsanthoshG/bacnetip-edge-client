#include "object_list.h"
#include <stdbool.h>
//above was added

#include <stdlib.h>
#include <time.h> /* for time */
#include <string.h> /* added (future use) */

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h> /* for time */
#if (__STDC_VERSION__ >= 199901L) && defined(__STDC_ISO_10646__)
#include <locale.h>
#endif
#define PRINT_ENABLED 1
/* BACnet Stack defines - first */
#include "bacnet/bacdef.h"
/* BACnet Stack API */
#include "bacnet/bactext.h"
#include "bacnet/bacerror.h"
#include "bacnet/iam.h"
#include "bacnet/arf.h"
#include "bacnet/npdu.h"
#include "bacnet/apdu.h"
#include "bacport.h"
#include "bacnet/whois.h"
#include "bacnet/version.h"
/* some demo stuff needed */
#include "bacnet/basic/binding/address.h"
#include "bacnet/basic/object/device.h"
#include "bacnet/basic/sys/filename.h"
#include "bacnet/basic/services.h"
#include "bacnet/basic/tsm/tsm.h"
#include "bacnet/datalink/datalink.h"
#include "bacnet/datalink/dlenv.h"
//added
#include "bacnet/bacapp.h"   /* bacapp_decode_application_data */
#include "bacnet/rp.h" 
#if BACNET_SVC_SERVER
#error "App requires server-only features disabled! Set BACNET_SVC_SERVER=0"
#endif

/* buffer used for receive */
static uint8_t Rx_Buf[MAX_MPDU] = { 0 };

/* converted command line arguments */
static uint32_t Target_Device_Object_Instance = BACNET_MAX_INSTANCE;
static uint32_t Target_Object_Instance = BACNET_MAX_INSTANCE;
static BACNET_OBJECT_TYPE Target_Object_Type = OBJECT_ANALOG_INPUT;
static BACNET_PROPERTY_ID Target_Object_Property = PROP_ACKED_TRANSITIONS;
static int32_t Target_Object_Index = BACNET_ARRAY_ALL;
/* the invoke id is needed to filter incoming messages */
static uint8_t Request_Invoke_ID = 0;
static BACNET_ADDRESS Target_Address;
static bool Error_Detected = false;

/* Added Capture buffer */
static BACnetObjectList *g_object_list_capture = NULL;
/* END */
//modified
static void capture_object_id(uint16_t type, uint32_t instance)
{
    if (!g_object_list_capture) return;
    BACnetObjectList *ol = g_object_list_capture;
    if (ol->count == ol->capacity) {
        size_t new_cap = (ol->capacity ? ol->capacity * 2 : 32);
        /* Allocate both first to keep operation atomic */
        uint16_t *new_types = (uint16_t *)realloc(ol->object_types, new_cap * sizeof(uint16_t));
        if (!new_types) return;
        uint32_t *new_insts = (uint32_t *)realloc(ol->object_instances, new_cap * sizeof(uint32_t));
        if (!new_insts) {
            /* rollback types growth to avoid mismatch */
            /* Note: cannot shrink back easily; simplest is to free and null to prevent misuse */
            free(new_types);
            return;
        }
        ol->object_types = new_types;
        ol->object_instances = new_insts;
        ol->capacity = new_cap;
    }
    ol->object_types[ol->count] = type;
    ol->object_instances[ol->count] = instance;
    ol->count++;
}
void bacnet_object_list_free(BACnetObjectList *ol)
{
    if (!ol) return;
    free(ol->object_types);
    free(ol->object_instances);
    ol->object_types = NULL;
    ol->object_instances = NULL;
    ol->count = 0;
    ol->capacity = 0;
}

int bacnet_object_list_init(BACnetObjectList *ol)
{
    if (!ol) return -1;
    ol->count = 0;
    ol->capacity = 32;
    ol->object_types = (uint16_t *)calloc(ol->capacity, sizeof(uint16_t));
    ol->object_instances = (uint32_t *)calloc(ol->capacity, sizeof(uint32_t));
    if (!ol->object_types || !ol->object_instances) {
        bacnet_object_list_free(ol);
        return -1;
    }
    return 0;
}



static void MyErrorHandler(
    BACNET_ADDRESS *src,
    uint8_t invoke_id,
    BACNET_ERROR_CLASS error_class,
    BACNET_ERROR_CODE error_code)
{
    if (address_match(&Target_Address, src) &&
        (invoke_id == Request_Invoke_ID)) {
        printf(
            "BACnet Error: %s: %s\n",
            bactext_error_class_name((int)error_class),
            bactext_error_code_name((int)error_code));
        Error_Detected = true;
    }
}

static void MyAbortHandler(
    BACNET_ADDRESS *src, uint8_t invoke_id, uint8_t abort_reason, bool server)
{
    (void)server;
    if (address_match(&Target_Address, src) &&
        (invoke_id == Request_Invoke_ID)) {
        printf(
            "BACnet Abort: %s\n", bactext_abort_reason_name((int)abort_reason));
        Error_Detected = true;
    }
}

static void
MyRejectHandler(BACNET_ADDRESS *src, uint8_t invoke_id, uint8_t reject_reason)
{
    if (address_match(&Target_Address, src) &&
        (invoke_id == Request_Invoke_ID)) {
        printf(
            "BACnet Reject: %s\n",
            bactext_reject_reason_name((int)reject_reason));
        Error_Detected = true;
    }
}

/** Handler for a ReadProperty ACK.
 * @ingroup DSRP
 * Doesn't actually do anything, except, for debugging, to
 * print out the ACK data of a matching request.
 *
 * @param service_request [in] The contents of the service request.
 * @param service_len [in] The length of the service_request.
 * @param src [in] BACNET_ADDRESS of the source of the message
 * @param service_data [in] The BACNET_CONFIRMED_SERVICE_DATA information
 *                          decoded from the APDU header of this message.
 */
/* Modified ACK handler: decode and capture OBJECT_LIST elements */
static void My_Read_Property_Ack_Handler(
    uint8_t *service_request,
    uint16_t service_len,
    BACNET_ADDRESS *src,
    BACNET_CONFIRMED_SERVICE_ACK_DATA *service_data)
{
    int len = 0;
    BACNET_READ_PROPERTY_DATA data;

    if (address_match(&Target_Address, src) &&
        (service_data->invoke_id == Request_Invoke_ID)) {
        len = rp_ack_decode_service_request(service_request, service_len, &data);
        if (len < 0) {
            printf("<decode failed!>\n");
            Error_Detected = true;
            return;
        }
        /* If not object list, just print fallback */
        if (data.object_property != PROP_OBJECT_LIST) {
            rp_ack_print_data(&data);
            return;
        }
        /* Parse the application data buffer for OBJECT_ID application tags */
        const uint8_t *buf = data.application_data;
        unsigned offset = 0;
        while (offset < data.application_data_len) {
            BACNET_APPLICATION_DATA_VALUE value;
            int vlen = bacapp_decode_application_data(
                &buf[offset],
                data.application_data_len - offset,
                &value);
            if (vlen <= 0) break;
            if (value.tag == BACNET_APPLICATION_TAG_OBJECT_ID) {
                capture_object_id(
                    value.type.Object_Id.type,
                    value.type.Object_Id.instance);
            }
            offset += vlen;
        }
        if (g_object_list_capture) {
            printf("Captured %zu object identifiers\n", g_object_list_capture->count);
        }
    }
}
static void Init_Service_Handlers(void)
{
    Device_Init(NULL);
    /* we need to handle who-is
       to support dynamic device binding to us */
    apdu_set_unconfirmed_handler(SERVICE_UNCONFIRMED_WHO_IS, handler_who_is);
    /* handle i-am to support binding to other devices */
    apdu_set_unconfirmed_handler(SERVICE_UNCONFIRMED_I_AM, handler_i_am_bind);
    /* set the handler for all the services we don't implement
       It is required to send the proper reject message... */
    apdu_set_unrecognized_service_handler_handler(handler_unrecognized_service);
    /* we must implement read property - it's required! */
    apdu_set_confirmed_handler(
        SERVICE_CONFIRMED_READ_PROPERTY, handler_read_property);
    /* handle the data coming back from confirmed requests */
    apdu_set_confirmed_ack_handler(
        SERVICE_CONFIRMED_READ_PROPERTY, My_Read_Property_Ack_Handler);
    /* handle any errors coming back */
    apdu_set_error_handler(SERVICE_CONFIRMED_READ_PROPERTY, MyErrorHandler);
    apdu_set_abort_handler(MyAbortHandler);
    apdu_set_reject_handler(MyRejectHandler);
}
/* Internal helper to run a single read with current Target_* globals */
static int run_read_cycle(unsigned timeout_ms)
{
    BACNET_ADDRESS src = {0};
    uint16_t pdu_len = 0;
    unsigned timeout = (timeout_ms ? timeout_ms : 100);
    unsigned max_apdu = 0;
    time_t elapsed_seconds = 0;
    time_t last_seconds = 0;
    time_t current_seconds = 0;
    time_t timeout_seconds = 0;
    bool found = false;

    Request_Invoke_ID = 0;
    Error_Detected = false;

    address_init();
    Device_Set_Object_Instance_Number(BACNET_MAX_INSTANCE);
    Init_Service_Handlers();
    dlenv_init();
    static bool cleanup_registered = false;
    if (!cleanup_registered) {
        atexit(datalink_cleanup);
        cleanup_registered = true;
    }

    last_seconds = time(NULL);
    timeout_seconds = (apdu_timeout() / 1000) * apdu_retries();

    found = address_bind_request(
        Target_Device_Object_Instance, &max_apdu, &Target_Address);
    if (!found) {
        Send_WhoIs(Target_Device_Object_Instance,
                   Target_Device_Object_Instance);
    }

    for (;;) {
        current_seconds = time(NULL);
        if (current_seconds != last_seconds) {
            tsm_timer_milliseconds(
                (uint16_t)((current_seconds - last_seconds) * 1000));
            datalink_maintenance_timer(current_seconds - last_seconds);
        }
        if (Error_Detected) break;

        if (!found) {
            found = address_bind_request(
                Target_Device_Object_Instance, &max_apdu, &Target_Address);
        }
        if (found) {
            if (Request_Invoke_ID == 0) {
                Request_Invoke_ID = Send_Read_Property_Request(
                    Target_Device_Object_Instance,
                    Target_Object_Type,
                    Target_Object_Instance,
                    Target_Object_Property,
                    Target_Object_Index);
            } else if (tsm_invoke_id_free(Request_Invoke_ID)) {
                /* Completed successfully */
                break;
            } else if (tsm_invoke_id_failed(Request_Invoke_ID)) {
                fprintf(stderr, "Error: TSM Timeout!\n");
                tsm_free_invoke_id(Request_Invoke_ID);
                Error_Detected = true;
                break;
            }
        } else {
            elapsed_seconds += (current_seconds - last_seconds);
            if (elapsed_seconds > timeout_seconds) {
                fprintf(stderr, "Error: APDU Timeout!\n");
                Error_Detected = true;
                break;
            }
        }

        pdu_len = datalink_receive(&src, &Rx_Buf[0], MAX_MPDU, timeout);
        if (pdu_len) {
            npdu_handler(&src, &Rx_Buf[0], pdu_len);
        }
        last_seconds = current_seconds;
    }

    return Error_Detected ? -1 : 0;
}





//the main function for reading object_list
int bacnet_read_object_list(uint32_t device_instance,
                            uint16_t object_type,
                            uint32_t object_instance,
                            uint32_t property_id,
                            BACnetObjectList *out_list)
{
    if (!out_list) return -1;
    if (device_instance > BACNET_MAX_INSTANCE) return -1;
    if (bacnet_object_list_init(out_list) != 0) return -1;

    Target_Device_Object_Instance = device_instance;
    Target_Object_Type = (BACNET_OBJECT_TYPE)object_type;
    Target_Object_Instance = object_instance;
    Target_Object_Property = (BACNET_PROPERTY_ID)property_id;
    Target_Object_Index = BACNET_ARRAY_ALL; /* fixed for full list */

    g_object_list_capture = (property_id == PROP_OBJECT_LIST) ? out_list : NULL;

    int rc = run_read_cycle(100);
    g_object_list_capture = NULL;
    if (rc != 0) {
        bacnet_object_list_free(out_list);
        return rc;
    }
    return 0;
}
