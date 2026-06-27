#pragma once

#define MAX_NR_HCI_ACL_PACKETS 4
// Allow a transient second connection: the Switch 2 BLE wake advertiser may
// accept a short LE connection while the Classic DualSense link is being torn
// down, so keep one slot of margin beyond the single Classic gamepad link.
#define MAX_NR_HCI_CONNECTIONS 2
#define MAX_NR_L2CAP_CHANNELS 4
#define MAX_NR_L2CAP_SERVICES 4

#define HCI_ACL_PAYLOAD_SIZE 1021
#define HCI_ACL_CHUNK_SIZE_ALIGNMENT 4
#define HCI_OUTGOING_PRE_BUFFER_SIZE 4

#define MAX_NR_RFCOMM_MULTIPLEXERS 0
#define MAX_NR_RFCOMM_SERVICES 0
#define MAX_NR_RFCOMM_CHANNELS 0

#define NVM_NUM_LINK_KEYS 4
#define NVM_NUM_DEVICE_DB_ENTRIES 4
#define HAVE_EMBEDDED_TIME_MS

// LE peripheral role is used only to emit the Switch 2 wake advertisement and
// to tolerate the brief connection the console opens before terminating it.
#define ENABLE_LE_PERIPHERAL

#define ENABLE_PRINTF_HEXDUMP
#define ENABLE_LOG_ERROR
#define ENABLE_LOG_INFO
