#include "ds5onswitch/usb_descriptors.h"

#include "bsp/board_api.h"
#include "tusb.h"

#include <cstddef>
#include <cstring>

namespace ds5onswitch {
namespace {

enum {
    kItfHid = 0,
    kItfAudioControl = 1,
    kItfAudioSpeaker = 2,
    kItfAudioMicHeadset = 3,
    kItfTotalHidOnly = 1,
    kItfTotalMicOnly = 3,
    kItfTotalHeadset = 4,
};

enum {
    kStrLangId = 0,
    kStrManufacturer,
    kStrProduct,
};

constexpr uint8_t kEndpointHid = 0x01;
constexpr uint8_t kEndpointAudioOut = 0x02;
constexpr uint8_t kEndpointAudioIn = 0x83;

constexpr uint16_t kAudioOutPacketSize = 196;
constexpr uint16_t kAudioInPacketSize = 196;
constexpr uint16_t kConfigLenHidOnly =
    TUD_CONFIG_DESC_LEN + TUD_HID_INOUT_DESC_LEN;
constexpr uint16_t kConfigLenMicOnly =
    TUD_CONFIG_DESC_LEN + TUD_HID_INOUT_DESC_LEN + 101;
constexpr uint16_t kConfigLenHeadset =
    TUD_CONFIG_DESC_LEN + TUD_HID_INOUT_DESC_LEN + 185;

UsbAudioPersona g_audio_persona = UsbAudioPersona::HidOnly;

uint8_t const kSwitchProHidReportDescriptor[] = {
    0x05,0x01,0x15,0x00,0x09,0x04,0xa1,0x01,0x85,0x30,0x05,0x01,0x05,0x09,0x19,0x01,
    0x29,0x0a,0x15,0x00,0x25,0x01,0x75,0x01,0x95,0x0a,0x55,0x00,0x65,0x00,0x81,0x02,
    0x05,0x09,0x19,0x0b,0x29,0x0e,0x15,0x00,0x25,0x01,0x75,0x01,0x95,0x04,0x81,0x02,
    0x75,0x01,0x95,0x02,0x81,0x03,0x0b,0x01,0x00,0x01,0x00,0xa1,0x00,0x0b,0x30,0x00,
    0x01,0x00,0x0b,0x31,0x00,0x01,0x00,0x0b,0x32,0x00,0x01,0x00,0x0b,0x35,0x00,0x01,
    0x00,0x15,0x00,0x27,0xff,0xff,0x00,0x00,0x75,0x10,0x95,0x04,0x81,0x02,0xc0,0x0b,
    0x39,0x00,0x01,0x00,0x15,0x00,0x25,0x07,0x35,0x00,0x46,0x3b,0x01,0x65,0x14,0x75,
    0x04,0x95,0x01,0x81,0x02,0x05,0x09,0x19,0x0f,0x29,0x12,0x15,0x00,0x25,0x01,0x75,
    0x01,0x95,0x04,0x81,0x02,0x75,0x08,0x95,0x34,0x81,0x03,0x06,0x00,0xff,0x85,0x21,
    0x09,0x01,0x75,0x08,0x95,0x3f,0x81,0x03,0x85,0x81,0x09,0x02,0x75,0x08,0x95,0x3f,
    0x81,0x03,0x85,0x01,0x09,0x03,0x75,0x08,0x95,0x3f,0x91,0x83,0x85,0x10,0x09,0x04,
    0x75,0x08,0x95,0x3f,0x91,0x83,0x85,0x80,0x09,0x05,0x75,0x08,0x95,0x3f,0x91,0x83,
    0x85,0x82,0x09,0x06,0x75,0x08,0x95,0x3f,0x91,0x83,0xc0,
};

tusb_desc_device_t const kDeviceDescriptor = {
    .bLength = sizeof(tusb_desc_device_t),
    .bDescriptorType = TUSB_DESC_DEVICE,
    .bcdUSB = 0x0200,
    .bDeviceClass = 0x00,
    .bDeviceSubClass = 0x00,
    .bDeviceProtocol = 0x00,
    .bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor = 0x057e,
    .idProduct = 0x2009,
    .bcdDevice = 0x0100,
    .iManufacturer = kStrManufacturer,
    .iProduct = kStrProduct,
    .iSerialNumber = 0x00,
    .bNumConfigurations = 0x01,
};

uint8_t const kHidOnlyConfigurationDescriptor[] = {
    TUD_CONFIG_DESCRIPTOR(1, kItfTotalHidOnly, 0, kConfigLenHidOnly,
                          TUSB_DESC_CONFIG_ATT_SELF_POWERED, 500),
    TUD_HID_INOUT_DESCRIPTOR(kItfHid, 0, HID_ITF_PROTOCOL_NONE,
                             sizeof(kSwitchProHidReportDescriptor),
                             kEndpointHid, 0x80 | kEndpointHid, 64, 8),
};

uint8_t const kMicOnlyConfigurationDescriptor[] = {
    TUD_CONFIG_DESCRIPTOR(1, kItfTotalMicOnly, 0, kConfigLenMicOnly,
                          TUSB_DESC_CONFIG_ATT_SELF_POWERED, 500),
    TUD_HID_INOUT_DESCRIPTOR(kItfHid, 0, HID_ITF_PROTOCOL_NONE,
                             sizeof(kSwitchProHidReportDescriptor),
                             kEndpointHid, 0x80 | kEndpointHid, 64, 8),

    0x09, TUSB_DESC_INTERFACE,
    kItfAudioControl, 0x00, 0x00,
    TUSB_CLASS_AUDIO, AUDIO_SUBCLASS_CONTROL, AUDIO_FUNC_PROTOCOL_CODE_UNDEF,
    0x00,

    0x09, TUSB_DESC_CS_INTERFACE, AUDIO10_CS_AC_INTERFACE_HEADER,
    0x00, 0x01, 0x28, 0x00, 0x01, kItfAudioSpeaker,

    0x0c, TUSB_DESC_CS_INTERFACE, AUDIO10_CS_AC_INTERFACE_INPUT_TERMINAL,
    0x04, 0x02, 0x04, 0x00,
    0x02, 0x03, 0x00, 0x00, 0x00,

    0x0a, TUSB_DESC_CS_INTERFACE, AUDIO10_CS_AC_INTERFACE_FEATURE_UNIT,
    0x05, 0x04, 0x01, 0x03, 0x00, 0x00, 0x00,

    0x09, TUSB_DESC_CS_INTERFACE, AUDIO10_CS_AC_INTERFACE_OUTPUT_TERMINAL,
    0x06, U16_TO_U8S_LE(AUDIO_TERM_TYPE_USB_STREAMING), 0x04, 0x05, 0x00,

    0x09, TUSB_DESC_INTERFACE,
    kItfAudioSpeaker, 0x00, 0x00,
    TUSB_CLASS_AUDIO, AUDIO_SUBCLASS_STREAMING, AUDIO_FUNC_PROTOCOL_CODE_UNDEF,
    0x00,

    0x09, TUSB_DESC_INTERFACE,
    kItfAudioSpeaker, 0x01, 0x01,
    TUSB_CLASS_AUDIO, AUDIO_SUBCLASS_STREAMING, AUDIO_FUNC_PROTOCOL_CODE_UNDEF,
    0x00,

    0x07, TUSB_DESC_CS_INTERFACE, AUDIO10_CS_AS_INTERFACE_AS_GENERAL,
    0x06, 0x01, U16_TO_U8S_LE(AUDIO10_DATA_FORMAT_TYPE_I_PCM),

    0x0b, TUSB_DESC_CS_INTERFACE, AUDIO10_CS_AS_INTERFACE_FORMAT_TYPE,
    AUDIO10_FORMAT_TYPE_I, 0x02, 0x02, 0x10, 0x01, 0x80, 0xbb, 0x00,

    0x09, TUSB_DESC_ENDPOINT,
    kEndpointAudioIn, 0x05,
    U16_TO_U8S_LE(kAudioInPacketSize), 0x01, 0x00, 0x00,

    0x07, TUSB_DESC_CS_ENDPOINT, AUDIO10_CS_EP_SUBTYPE_GENERAL,
    0x00, 0x00, 0x00, 0x00,
};

uint8_t const kHeadsetConfigurationDescriptor[] = {
    TUD_CONFIG_DESCRIPTOR(1, kItfTotalHeadset, 0, kConfigLenHeadset,
                          TUSB_DESC_CONFIG_ATT_SELF_POWERED, 500),
    TUD_HID_INOUT_DESCRIPTOR(kItfHid, 0, HID_ITF_PROTOCOL_NONE,
                             sizeof(kSwitchProHidReportDescriptor),
                             kEndpointHid, 0x80 | kEndpointHid, 64, 8),

    0x09, TUSB_DESC_INTERFACE,
    kItfAudioControl, 0x00, 0x00,
    TUSB_CLASS_AUDIO, AUDIO_SUBCLASS_CONTROL, AUDIO_FUNC_PROTOCOL_CODE_UNDEF,
    0x00,

    0x0a, TUSB_DESC_CS_INTERFACE, AUDIO10_CS_AC_INTERFACE_HEADER,
    0x00, 0x01, 0x48, 0x00, 0x02, kItfAudioSpeaker, kItfAudioMicHeadset,

    0x0c, TUSB_DESC_CS_INTERFACE, AUDIO10_CS_AC_INTERFACE_INPUT_TERMINAL,
    0x01, U16_TO_U8S_LE(AUDIO_TERM_TYPE_USB_STREAMING), 0x06,
    0x02, 0x03, 0x00, 0x00, 0x00,

    0x0a, TUSB_DESC_CS_INTERFACE, AUDIO10_CS_AC_INTERFACE_FEATURE_UNIT,
    0x02, 0x01, 0x01, 0x03, 0x00, 0x00, 0x00,

    0x09, TUSB_DESC_CS_INTERFACE, AUDIO10_CS_AC_INTERFACE_OUTPUT_TERMINAL,
    0x03, U16_TO_U8S_LE(AUDIO_TERM_TYPE_OUT_HEADPHONES), 0x04, 0x02, 0x00,

    0x0c, TUSB_DESC_CS_INTERFACE, AUDIO10_CS_AC_INTERFACE_INPUT_TERMINAL,
    0x04, 0x02, 0x04, 0x03,
    0x02, 0x03, 0x00, 0x00, 0x00,

    0x0a, TUSB_DESC_CS_INTERFACE, AUDIO10_CS_AC_INTERFACE_FEATURE_UNIT,
    0x05, 0x04, 0x01, 0x03, 0x00, 0x00, 0x00,

    0x09, TUSB_DESC_CS_INTERFACE, AUDIO10_CS_AC_INTERFACE_OUTPUT_TERMINAL,
    0x06, U16_TO_U8S_LE(AUDIO_TERM_TYPE_USB_STREAMING), 0x01, 0x05, 0x00,

    0x09, TUSB_DESC_INTERFACE,
    kItfAudioSpeaker, 0x00, 0x00,
    TUSB_CLASS_AUDIO, AUDIO_SUBCLASS_STREAMING, AUDIO_FUNC_PROTOCOL_CODE_UNDEF,
    0x00,

    0x09, TUSB_DESC_INTERFACE,
    kItfAudioSpeaker, 0x01, 0x01,
    TUSB_CLASS_AUDIO, AUDIO_SUBCLASS_STREAMING, AUDIO_FUNC_PROTOCOL_CODE_UNDEF,
    0x00,

    0x07, TUSB_DESC_CS_INTERFACE, AUDIO10_CS_AS_INTERFACE_AS_GENERAL,
    0x01, 0x01, U16_TO_U8S_LE(AUDIO10_DATA_FORMAT_TYPE_I_PCM),

    0x0b, TUSB_DESC_CS_INTERFACE, AUDIO10_CS_AS_INTERFACE_FORMAT_TYPE,
    AUDIO10_FORMAT_TYPE_I, 0x02, 0x02, 0x10, 0x01, 0x80, 0xbb, 0x00,

    0x09, TUSB_DESC_ENDPOINT,
    kEndpointAudioOut, 0x09,
    U16_TO_U8S_LE(kAudioOutPacketSize), 0x01, 0x00, 0x00,

    0x07, TUSB_DESC_CS_ENDPOINT, AUDIO10_CS_EP_SUBTYPE_GENERAL,
    0x00, 0x00, 0x00, 0x00,

    0x09, TUSB_DESC_INTERFACE,
    kItfAudioMicHeadset, 0x00, 0x00,
    TUSB_CLASS_AUDIO, AUDIO_SUBCLASS_STREAMING, AUDIO_FUNC_PROTOCOL_CODE_UNDEF,
    0x00,

    0x09, TUSB_DESC_INTERFACE,
    kItfAudioMicHeadset, 0x01, 0x01,
    TUSB_CLASS_AUDIO, AUDIO_SUBCLASS_STREAMING, AUDIO_FUNC_PROTOCOL_CODE_UNDEF,
    0x00,

    0x07, TUSB_DESC_CS_INTERFACE, AUDIO10_CS_AS_INTERFACE_AS_GENERAL,
    0x06, 0x01, U16_TO_U8S_LE(AUDIO10_DATA_FORMAT_TYPE_I_PCM),

    0x0b, TUSB_DESC_CS_INTERFACE, AUDIO10_CS_AS_INTERFACE_FORMAT_TYPE,
    AUDIO10_FORMAT_TYPE_I, 0x02, 0x02, 0x10, 0x01, 0x80, 0xbb, 0x00,

    0x09, TUSB_DESC_ENDPOINT,
    kEndpointAudioIn, 0x05,
    U16_TO_U8S_LE(kAudioInPacketSize), 0x01, 0x00, 0x00,

    0x07, TUSB_DESC_CS_ENDPOINT, AUDIO10_CS_EP_SUBTYPE_GENERAL,
    0x00, 0x00, 0x00, 0x00,
};

static_assert(sizeof(kHidOnlyConfigurationDescriptor) == kConfigLenHidOnly);
static_assert(sizeof(kMicOnlyConfigurationDescriptor) == kConfigLenMicOnly);
static_assert(sizeof(kHeadsetConfigurationDescriptor) == kConfigLenHeadset);

char const *const kStringDescriptor[] = {
    "",
    "Nintendo Co., Ltd.",
    "Pro Controller",
};

uint16_t g_string_descriptor[32 + 1];

} // namespace

void usb_descriptors_set_audio_persona(UsbAudioPersona persona)
{
    g_audio_persona = persona;
}

UsbAudioPersona usb_descriptors_audio_persona()
{
    return g_audio_persona;
}

} // namespace ds5onswitch

extern "C" uint8_t const *tud_descriptor_device_cb()
{
    return reinterpret_cast<uint8_t const *>(&ds5onswitch::kDeviceDescriptor);
}

extern "C" uint8_t const *tud_descriptor_configuration_cb(uint8_t index)
{
    (void)index;
    using ds5onswitch::UsbAudioPersona;
    switch (ds5onswitch::usb_descriptors_audio_persona()) {
    case UsbAudioPersona::MicOnly:
        return ds5onswitch::kMicOnlyConfigurationDescriptor;
    case UsbAudioPersona::Headset:
        return ds5onswitch::kHeadsetConfigurationDescriptor;
    case UsbAudioPersona::HidOnly:
    default:
        return ds5onswitch::kHidOnlyConfigurationDescriptor;
    }
}

extern "C" uint8_t const *tud_hid_descriptor_report_cb(uint8_t instance)
{
    (void)instance;
    return ds5onswitch::kSwitchProHidReportDescriptor;
}

extern "C" uint16_t const *tud_descriptor_string_cb(uint8_t index, uint16_t langid)
{
    (void)langid;
    std::size_t chr_count = 0;

    if (index == ds5onswitch::kStrLangId) {
        ds5onswitch::g_string_descriptor[1] = 0x0409;
        chr_count = 1;
    } else {
        if (index >= sizeof(ds5onswitch::kStringDescriptor) /
                         sizeof(ds5onswitch::kStringDescriptor[0])) {
            return nullptr;
        }
        const char *str = ds5onswitch::kStringDescriptor[index];
        chr_count = std::strlen(str);
        const std::size_t max_count =
            sizeof(ds5onswitch::g_string_descriptor) /
                sizeof(ds5onswitch::g_string_descriptor[0]) -
            1;
        if (chr_count > max_count) {
            chr_count = max_count;
        }
        for (std::size_t i = 0; i < chr_count; ++i) {
            ds5onswitch::g_string_descriptor[1 + i] =
                static_cast<uint8_t>(str[i]);
        }
    }

    ds5onswitch::g_string_descriptor[0] =
        static_cast<uint16_t>((TUSB_DESC_STRING << 8u) |
                              (2u * chr_count + 2u));
    return ds5onswitch::g_string_descriptor;
}
