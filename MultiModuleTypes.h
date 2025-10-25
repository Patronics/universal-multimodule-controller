#include <stdint.h>

#ifndef MULTIMODULETYPES_H
#define MULTIMODULETYPES_H

#define MAX_CHANNELS 16
#define BITS_PER_CHANNEL 11
// For Stream[27..35]:
#define MAX_ADD_DATA_BYTES 9

#define MAX_TELEM_DATA_BYTES 32

// multi-module protocol documented at https://github.com/pascallanger/DIY-Multiprotocol-TX-Module/blob/bf38415420519b616c2a4beb849af74519e661fa/Multiprotocol/Multiprotocol.h#L889

#define HEADER_CONSTANT_BITS 0b01010100 // 0b010101: Constant bits for header configuration,
typedef union {  //valid values range 0x54-0x57
    struct {
        uint8_t sub_protocol_range : 1; // Bit 0: bit 5 of subprotocol, 0 for values 0..31, 1 for values 32..63
        uint8_t is_failsafe : 1;        // Bit 1: 0 for channel mode, 1 for failsafe
        uint8_t reserved_bits : 6;      // Bits 2-7: must be 0b010101 for use with multi-module
    };
    uint8_t value;                     // Full byte access
} StreamHeaderFlags;

typedef union {
    struct {
        uint8_t sub_protocol : 5;   // Bits 0-4
        uint8_t rangeCheckBit : 1;   // Bit 5
        uint8_t autoBindBit : 1;     // Bit 6
        uint8_t bindBit : 1;         // Bit 7
    };
    uint8_t value;                  // Full byte access
} SubProtocolFlags;

typedef union {
    struct {
        uint8_t rxNum : 4;          // Bits 0-3
        uint8_t type : 3;           // Bits 4-6
        uint8_t power : 1;          // Bit 7 (0=High, 1=Low)
    };
    uint8_t value;                  // Full byte access
} RxNumPowerType;

typedef union {
    struct {
        uint8_t disable_CH_Mapping : 1; // Bit 0
        uint8_t disable_Telemetry : 1;   // Bit 1
        uint8_t future_Use : 1;          // Bit 2
        uint8_t telemetry_Invert : 1;     // Bit 3
        uint8_t rxNum : 2;                // Bits 4-5, high bits of rxnum
        uint8_t sub_protocol : 2;         // Bits 6-7, high bits of subprotocol
    };
    uint8_t value;                      // Full byte access
} ExtendedProtocolData;

// Protocol Stream definition to send over serial
typedef struct __attribute__((packed)) {
    StreamHeaderFlags header;                            // Stream[0]: Header Byte
    SubProtocolFlags sub_protocol_flags;       // Stream[1]: sub_protocol | Flags
    RxNumPowerType rx_num_power_type;          // Stream[2]: RxNum | Power | Type
    int8_t option_protocol;                    // Stream[3]: Option Protocol
    uint8_t channels[MAX_CHANNELS * BITS_PER_CHANNEL / 8];  // Stream[4] to [25]: 16 Channels (11 bits each)
    ExtendedProtocolData extended_protocol;    // Stream[26]: Extended Protocol data
    uint8_t additional_data[MAX_ADD_DATA_BYTES]; // Stream[27..35]: Additional protocol data (optional)
} MultiProtocolStream;

typedef union {
    uint8_t value; // Access the entire byte
    struct {
        uint8_t input_signal_detected : 1; // 0x01
        uint8_t serial_mode_enabled : 1;    // 0x02
        uint8_t protocol_is_valid : 1;      // 0x04
        uint8_t module_is_in_binding_mode : 1; // 0x08
        uint8_t module_waits_for_bind_event : 1; // 0x10
        uint8_t protocol_supports_failsafe : 1;      // 0x20
        uint8_t protocol_supports_disable_channel_mapping : 1; // 0x40
        uint8_t data_buffer_almost_full : 1; // 0x80
    } bits; // Access flags individually
} MultiModuleStatusFlagsByte;

typedef union {
    uint8_t value; // Access the entire byte
    struct {
        uint8_t ch1 : 2; // Channel 1
        uint8_t ch2 : 2; // Channel 2
        uint8_t ch3 : 2; // Channel 3
        uint8_t ch4 : 2; // Channel 4
    } channels; // Access individual channels
} ChannelOrder;

const char* channelNames[] = {"A", "E", "T", "R"};

typedef union {
    uint8_t value; // Complete byte access
    struct {
        uint8_t option_text : 4; // High 4 bits for option text
        uint8_t num_sub_protocols : 4; // Low 4 bits for number of sub-protocols
    } parts; // Individual access to subsections
} OptionTextAndNumSubProtocols;

//note: following isn't the full telemetry response, but the expected response type when type==1
// MultiModule Status definition
typedef struct __attribute__((packed)) {
    MultiModuleStatusFlagsByte flags;           // Flags
    uint8_t major;                              // Version Major
    uint8_t minor;                              // Version Minor
    uint8_t revision;                           // Revision
    uint8_t patchlevel;                         // Patchlevel
    ChannelOrder channel_order;                 // Channel order
    uint8_t next_protocol;                      // Next valid protocol
    uint8_t prev_protocol;                      // Previous valid protocol
    char protocol_name[7];                       // Protocol name (null-terminated if len<7)
    OptionTextAndNumSubProtocols option_text_and_num_sub_protocols;  // Option text (e.g. OPTION_NONE, OPTION_OPTION) (high 4 bits);    // Number of sub protocols (low 4 bits)
    char sub_protocol_name[8];                       // Sub protocol names (null-terminated if len<8)
} MultiModuleStatus;

#define MULTI_MODULE_STATUS_TYPE 1

#define FLYSKY_AFHDS2_TELEM_STATUS_TYPE 6

#endif