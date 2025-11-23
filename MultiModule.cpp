#include "MultiModule.h"
#include "UI.h"
#include <stdint.h>

/*note that protocol and subProtocol names are inconsistient in documentation, and thus some of the struct names may be confusing.
In the multi-module docs,
protocolNum is sometimes referred to as "subProtocol" but other times as "protocol"
and subprotocolNum is sometimes referred to as "type" but other times as "subProtocol"
*/
void setProtocolModeBits(MultiProtocolStream *s, uint8_t protocolNum, uint8_t subprotocolNum){
  s->sub_protocol_flags.sub_protocol = protocolNum & 0x1F; //bits 0-4
  s->header.sub_protocol_range_inv = ((protocolNum >> 5) ^ 0x01) & 0x01; //inverted bit 5
  s->extended_protocol.sub_protocol = (protocolNum >> 6) & 0x03; //bits 6-7
  s->rx_num_power_type.type = subprotocolNum & 0x07; //type is 3 bits total
}

const char* channelNames[] = {"A", "E", "T", "R"};


//set value of channel in multiProtocolStream, return set value (or -1, -2 for invalid inputs)
int setChannelValue(MultiProtocolStream *stream, int channel_index, uint16_t value) {
    if (channel_index < 0 || channel_index >= MAX_CHANNELS) {
        //Serial.println("error: invalid channel index");
        return -1;
    }
    if (value >= (1 << BITS_PER_CHANNEL)) {
        //Serial.println("error: channel value exceeds max range");
        return -2;
    }

    int byte_start = channel_index * BITS_PER_CHANNEL / 8; // Calculate starting byte
    int bit_start = channel_index * BITS_PER_CHANNEL % 8;   // Calculate bit position within byte

    // Create a mask to clear the specific bits (11 bits = 0x7FF)
    uint16_t mask = (1 << BITS_PER_CHANNEL) - 1; // Creates a mask of 11 bits (0b00000000000111111111111)
    mask <<= bit_start; // Position the mask at the correct bit position

    // Clear previous bits in both bytes
    stream->channels[byte_start] &= ~(mask & 0xFF);
    stream->channels[byte_start + 1] &= ~(mask >> 8);

    // Set the lower byte
    stream->channels[byte_start] |= (value << bit_start);

    // Set the higher byte
    stream->channels[byte_start + 1] |= (value >> (8 - bit_start));
    return value;
}