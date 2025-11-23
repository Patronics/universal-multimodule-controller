#include <stdint.h>
#include "UI.h"

#ifndef CHANNEL_MANAGER_H
#define CHANNEL_MANAGER_H

//#define MAX(a,b) ((a) > (b) ? (a) : (b))

//MAX_INPUTS > MAX_CHANNELS to allow for unused inputs from other input sources. 128 allows for USB keyboard, multiple controllers, etc
#define MAX_INPUTS 128
//15 characters plus null-termination
#define INPUT_NAME_LEN 16
#define OUTPUT_NAME_LEN 16

#define MAX_USB_DEVICE_DESCRIPTORS 8
#define MAX_INPUT_CHANNELS_PER_USB_DEVICE 16

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
    INPUT_FUNCTION_UNKNOWN,
} InputFunctionType;

//the input function to get updated data. If id is used when generating a value, any given function MUST only be used to handle a single type as defined by InputFunctionType
typedef int (*GetChannelInputFn)(void* context, int id);

//function for configuring settings for a given input. If no settings available, null ptr instead
typedef int (*ConfigChannelInputFn)(void* context, int id, NavButton btnPressed);

typedef void (*CleanupInputContextFn)(void* context, InputFunctionType inputFunctionType);

typedef struct {
    GetChannelInputFn getLatestInputData; // Function pointer to retrieve latest input data, pass the ID as argument
    ConfigChannelInputFn configureChannelInput;
    CleanupInputContextFn cleanupInputContextFn;  //if non-null, call this function to cleanup context data (such as SimpleFreeInputContext for malloc)
    int id; // Unique ID number
    InputFunctionType inputFunctionType;  //type is used as a namespace for ID numbers, and to describe which operations are supported
    void * context; //reserved to allow passing additional context to the function, with exact layout expected to be consistient within a given InputFunctionType
    int minRange; // Minimum range value
    int maxRange; // Maximum range value
    char name[INPUT_NAME_LEN]; //short, NULL-terminated ASCII name
} InputChannelDescriptor;

/* pool structure, allow adding/removing inputs when devices connect */
typedef struct {
    InputChannelDescriptor items[MAX_INPUTS];
    bool used[MAX_INPUTS];
    int capacity;
    int count;
} InputDescriptorPool;

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
    uint8_t hidInterfaceType;  //intended to match values in hid_interface_protocol_extended_enum_t
    uint16_t vid, pid;
    uint8_t dev_addr, instance; //note: device may have multiple instances, in which case each will have a distinct USBInputDeviceDescriptor
    char name[INPUT_NAME_LEN];
    uint8_t latest_report[HID_REPORT_BUFSIZE];
    uint8_t deviceNum;          //descriptor number within USBDeviceDescriptors (max of MAX_USB_DEVICE_DESCRIPTORS)
    InputChannelDescriptor *inputChannels[MAX_INPUT_CHANNELS_PER_USB_DEVICE]; //array of pointers to inputs owned by this USB device
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

void initDefaultInputDescriptors(InputDescriptorPool *pool);
void initOutputAndDefaultInputChannelDescriptors(OutputChannelDescriptor *outputChannels,InputDescriptorPool *inputChannelPool, int numChannels);
void assignInputChannelDescriptor(OutputChannelDescriptor *outputChannel, InputChannelDescriptor *inputChannel);
int defaultInputDataProducer(void* context, int id);
int fixedInputDataProducer(void* context, int id);
int getFirstFreeUSBInputDescriptorIndex(USBInputDeviceDescriptor* arr);
void releaseUSBInputChannels(InputDescriptorPool *pool, USBInputDeviceDescriptor *desc);
USBInputDeviceDescriptor* findUSBDescriptorByDevAddrAndInstance(USBInputDeviceDescriptor* arr, uint8_t dev_addr, uint8_t instance);
InputChannelDescriptor *AllocateUSBKeyboardNumberInputChannel(InputDescriptorPool *pool, int id, const char* name, USBInputDeviceDescriptor * descriptor);
USBGamepadContextType* AllocateUSBGamepadStickChannels(InputDescriptorPool *pool, USBInputDeviceDescriptor * descriptor);



#endif