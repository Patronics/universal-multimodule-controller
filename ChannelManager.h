#include <stdint.h>
#include "UI.h"
//#include "usbh_helper.h"
#include "Adafruit_TinyUSB.h"
#include "pio_usb.h"
#include "MultiModule.h"

#ifndef CHANNEL_MANAGER_H
#define CHANNEL_MANAGER_H

//#define MAX(a,b) ((a) > (b) ? (a) : (b))

//MAX_INPUTS > MAX_CHANNELS to allow for unused inputs from other input sources. 128 allows for USB keyboard, multiple controllers, etc
#define MAX_INPUTS 128
//15 characters plus null-termination
#define INPUT_NAME_LEN 16
#define MIXER_NAME_LEN 16
#define OUTPUT_NAME_LEN 16

//may want to increase mixers beyond max_channels to allow more complex nested mixes
#define MAX_MIXERS MAX_CHANNELS

#define MAX_USB_DEVICE_DESCRIPTORS 8
#define MAX_INPUT_CHANNELS_PER_USB_DEVICE 16

#define USB_INPUT_CHANNEL_NAME_LEN 4  //3 chars plus null, such as "Lx", "A", "Ch2", etc

//valid for USB FS only, USB 2 reports may be up to 512 bytes long
#define HID_REPORT_BUFSIZE 64

typedef enum {
    INPUT_FUNCTION_CONST_FIXED,   //hardcoded, nonconfigurable value
    INPUT_FUNCTION_CONFIG_VALUE,  //default value, but configurable
    INPUT_FUNCTION_IO_ADC,        //taken by ADC input to pi pico
    INPUT_FUNCTION_IO_SWITCH,     //boolean value from hardwired switch
    INPUT_FUNCTION_USB_VALUE,     //analog value from USB device input    //todo implement/precisely define
    INPUT_FUNCTION_USB_SWITCH,    //boolean value from USB device switch  //todo implement/precisely define
    INPUT_FUNCTION_IR_SWITCH,     //boolean value from IR remote          //todo implement/precisely define
    INPUT_FUNCTION_USB_KB_NUMBER, //number 0-9 from USB keyboard
    INPUT_FUNCTION_USB_GAMEPAD_STICK, //analog stick, or other 1-byte values in USB report array
    INPUT_FUNCTION_MIXER,             //internal mixer 'virtual input'
    INPUT_FUNCTION_UNKNOWN,
} InputFunctionType;

typedef struct InputChannelDescriptor InputChannelDescriptor;

//the input function to get updated data. If id is used when generating a value, any given function MUST only be used to handle a single type as defined by InputFunctionType
typedef int (*GetChannelInputFn)(void* context, int id);

//function for configuring settings for a given input. If no settings available, null ptr instead
typedef bool (*ConfigChannelInputFn)(struct InputChannelDescriptor *channel, int value);

typedef void (*CleanupInputContextFn)(void* context, InputFunctionType inputFunctionType);

struct InputChannelDescriptor{
    GetChannelInputFn getLatestInputData; // Function pointer to retrieve latest input data, pass the ID as argument
    ConfigChannelInputFn configureChannelInput;
    CleanupInputContextFn cleanupInputContextFn;  //if non-null, call this function to cleanup context data (such as SimpleFreeInputContext for malloc)
    int id; // Unique ID number
    InputFunctionType inputFunctionType;  //type is used as a namespace for ID numbers, and to describe which operations are supported
    void * context; //reserved to allow passing additional context to the function, with exact layout expected to be consistient within a given InputFunctionType
    int minRange; // Minimum range value
    int maxRange; // Maximum range value
    char name[INPUT_NAME_LEN]; //short, NULL-terminated ASCII name
};

/* pool structure, allow adding/removing inputs when devices connect */
typedef struct {
    InputChannelDescriptor items[MAX_INPUTS];
    bool used[MAX_INPUTS];
    int capacity;
    int count;
} InputDescriptorPool;


typedef enum {
    MIXER_OP_CH1_ONLY,    //ignore Channel2, only process channel 1 mix
    MIXER_OP_ADD,
    MIXER_OP_DIFF,    //get absolute value of difference
    MIXER_OP_SUB,  
    MIXER_OP_MUL,    //multiply values
    MIXER_OP_DIV,    //divide Channel1 by Channel2
    MIXER_OP_MIN,    //useful for lock-out switch
    MIXER_OP_MAX,
    MIXER_OP_ENUM_OVERFLOW,
    MIXER_OP_ENUM_UNDERFLOW=-1
} MixerCombineOperation;

static const char* MixerCombineOperationString [] = {
    "Ch1",
    "ADD",
    "DIFF",
    "SUB",
    "MUL",
    "DIV",
    "MIN",
    "MAX",
    "Err"
};
 

typedef struct {
    int id;
    MixerCombineOperation operation;
    InputChannelDescriptor *inputChannel1Descriptor;
    int channel1Offset;
    int channel1Scale;  //scale as percentage
    bool channel1Invert;
    InputChannelDescriptor *inputChannel2Descriptor;
    int channel2Offset;
    int channel2Scale;  //scale as percentage
    bool channel2Invert;
    char name[MIXER_NAME_LEN];
    InputChannelDescriptor *mixerResultDescriptor;
} MixerChannelDescriptor; //short, NULL-terminated ASCII name

typedef enum {
  MIXER_EDIT_STATE_CHANGE_MIXER_ITEM = 0,
  MIXER_EDIT_STATE_OPERATION,
  MIXER_EDIT_STATE_A_SOURCE,
  MIXER_EDIT_STATE_A_SCALE,
  MIXER_EDIT_STATE_A_INVERT,
  MIXER_EDIT_STATE_A_OFFSET,
  MIXER_EDIT_STATE_B_SOURCE,
  MIXER_EDIT_STATE_B_SCALE,
  MIXER_EDIT_STATE_B_INVERT,
  MIXER_EDIT_STATE_B_OFFSET,
  MIXER_EDIT_STATE_ENUM_OVERFLOW,
  MIXER_EDIT_STATE_ENUM_UNDERFLOW=-1
} mixerEditTargetState;

typedef struct {
  MixerChannelDescriptor *currentlyEditingMixer;
  mixerEditTargetState mixerActiveEditState;
} mixerMenuItemHandlerContext;

typedef struct {
    InputChannelDescriptor *inputChannelDescriptor; // Pointer to input channel descriptor
    uint8_t outputChannelNumber; // Output channel number 0-15 (matches index of array)
    int minRange; // Minimum range value
    int maxRange; // Maximum range value
    char name[OUTPUT_NAME_LEN]; //short, NULL-terminated ASCII name
} OutputChannelDescriptor;


/// HID Interface Protocol - extended (based on tinyUSB hid_interface_protocol_enum_t)
typedef enum
{
  USB_DESCRIPTOR_PROTOCOL_NONE     = 0, ///< None
  USB_DESCRIPTOR_PROTOCOL_KEYBOARD = 1, ///< Keyboard
  USB_DESCRIPTOR_PROTOCOL_MOUSE    = 2,  ///< Mouse
  USB_DESCRIPTOR_PROTOCOL_GAMEPAD  = 3   // new, for USB gamepads (may need additional sub-categories)
}hid_interface_protocol_extended_enum_t;

typedef struct {
    char     name[INPUT_NAME_LEN-USB_INPUT_CHANNEL_NAME_LEN];
    uint16_t vid, pid;
    uint8_t  reportLength;
    uint8_t  analogInputCount;
    uint8_t  analogInputReportOffsets[MAX_INPUT_CHANNELS_PER_USB_DEVICE];  //what byte in the HID report corresponds to this input
    char     analogInputNames[MAX_INPUT_CHANNELS_PER_USB_DEVICE][USB_INPUT_CHANNEL_NAME_LEN]; //array of names for channels
    uint8_t  digitalInputCount;
    uint8_t  digitalInputReportByteOffsets[MAX_INPUT_CHANNELS_PER_USB_DEVICE];  //what byte in the HID report contains this input
    uint8_t  digitalInputReportBitMask[MAX_INPUT_CHANNELS_PER_USB_DEVICE];      //mask to filter which bit from above byte contains this input
    char     digitalInputNames[MAX_INPUT_CHANNELS_PER_USB_DEVICE][USB_INPUT_CHANNEL_NAME_LEN];
} USBGamepadLayoutDefinition;


typedef struct {
  tusb_desc_device_t desc_device;
  uint16_t manufacturer[32];
  uint16_t product[48];
  uint16_t serial[16];
  bool mounted;
} usb_dev_info_t;

typedef struct {
    usb_dev_info_t usb_dev_info;
    uint8_t hidInterfaceType;  //intended to match values in hid_interface_protocol_extended_enum_t
    uint16_t vid, pid;
    uint8_t dev_addr, instance; //note: device may have multiple instances, in which case each will have a distinct USBInputDeviceDescriptor
    char name[INPUT_NAME_LEN];
    uint8_t latest_report[HID_REPORT_BUFSIZE];
    uint8_t deviceNum;          //descriptor number within USBDeviceDescriptors (max of MAX_USB_DEVICE_DESCRIPTORS)
    InputChannelDescriptor *inputChannels[MAX_INPUT_CHANNELS_PER_USB_DEVICE]; //array of pointers to inputs owned by this USB device
    const USBGamepadLayoutDefinition *layoutDef; //pointer to the relevant layout definition
    void* contextPointer; //pointer to the device-type-specific context, used for individual input devices, to free when USB device disconnected
} USBInputDeviceDescriptor;

typedef struct {
    USBInputDeviceDescriptor *parentDeviceDescriptor;
    int latestValue;
    unsigned long lastUpdateMillis;
} USBKeyboardContextType;

typedef struct {
    USBInputDeviceDescriptor *parentDeviceDescriptor;
    unsigned long lastUpdateMillis;
} USBGamepadContextType;

int Pool_FindIndexById(InputDescriptorPool *pool, int id);
void Pool_Init(InputDescriptorPool *pool, const InputChannelDescriptor *prototype);
void Pool_Release(InputDescriptorPool *pool, InputChannelDescriptor *desc);
InputChannelDescriptor *Pool_FindByIdAndType(InputDescriptorPool *pool, int id, InputFunctionType type);
int Pool_FindIndexByIdAndType(InputDescriptorPool *pool, int id, InputFunctionType type);
int Pool_FindNextUsedIndex(InputDescriptorPool *pool, int index);
int Pool_FindPreviousUsedIndex(InputDescriptorPool *pool, int index);

InputChannelDescriptor* findInputDescriptorWithTypeNameAndId(InputDescriptorPool *pool, InputFunctionType type, const char* name, int id);

void initDefaultInputDescriptors(InputDescriptorPool *pool);
void initOutputAndDefaultInputChannelDescriptors(OutputChannelDescriptor *outputChannels,InputDescriptorPool *inputChannelPool, MixerChannelDescriptor (&mixerChannels)[MAX_MIXERS], InputChannelDescriptor* (&failsafeChannels)[MAX_CHANNELS]);
void assignInputChannelDescriptor(OutputChannelDescriptor *outputChannel, InputChannelDescriptor *inputChannel);
int getLatestInputData(InputChannelDescriptor* input); //use DataProducer to get latest input data
int evaluateSingleChannelMixerValue(MixerChannelDescriptor* mixer, bool channel1);
int defaultInputDataProducer(void* context, int id);
int fixedInputDataProducer(void* context, int id);
int getFirstFreeUSBInputDescriptorIndex(USBInputDeviceDescriptor* arr);
void releaseUSBInputChannels(InputDescriptorPool *pool, USBInputDeviceDescriptor *desc, OutputChannelDescriptor *outChannelsArr, InputChannelDescriptor* (&failsafeChannels)[MAX_CHANNELS]);
USBInputDeviceDescriptor* findUSBDescriptorByDevAddrAndInstance(USBInputDeviceDescriptor* arr, uint8_t dev_addr, uint8_t instance);
InputChannelDescriptor *AllocateUSBKeyboardNumberInputChannel(InputDescriptorPool *pool, int id, const char* name, USBInputDeviceDescriptor * descriptor);
USBGamepadContextType* AllocateUSBGamepadStickChannels(InputDescriptorPool *pool, USBInputDeviceDescriptor * descriptor);
InputChannelDescriptor *FindInputChannelDescriptorByInputNameSuffix(InputDescriptorPool *pool, const char *inputName, int suffixLength);


#endif