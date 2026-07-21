#include <stdint.h>
#include <cstddef>
#include <cstdio>
#include <Arduino.h>

#include "ChannelManager.h"
#include "MultiModule.h"
#include "UI.h"

extern InputChannelDescriptor* failsafeChannels[MAX_CHANNELS];

static int clamp(int v, int lo, int hi){
  if (v < lo) return lo;
  if (v > hi) return hi;
  return v;
}

//use DataProducer to get latest input data
int getLatestInputData(InputChannelDescriptor* input,int outChannelId){
  if(input == NULL){ //invalid input, null ptr
    SerialDebug<DEBUG_WARN|DEBUG_ERROR>("ERROR: tried to read input data from null pointer");
    return 0;
  }
  return input->getLatestInputData(input->context, input->id, outChannelId);
}

// Interpret context as an integer value stored via an integer-sized pointer.
// always returns the value specified by its context directly
// compatible with INPUT_FUNCTION_CONST_FIXED and INPUT_FUNCTION_CONFIG_VALUE input types
int fixedInputDataProducer(void* context, int id, int outChannelId){
  intptr_t fixedValue = (intptr_t)context;
  (void)id; // id unused in this example, supress warning with no-op
  return (int)fixedValue;
}

//a sane default input, sets the channel output to its midpoint
int defaultInputDataProducer(void* context, int id, int outChannelId){
  (void)context; // explicitly ignore incoming context
  // Provide a consistent hardcoded value for fixedInputDataProducer to return.
  // Use intptr_t cast so we can pass an integer via the void* context parameter.
  const int midpoint = NATIVE_MID_VALUE;
  return fixedInputDataProducer((void *)(intptr_t)midpoint, id, outChannelId);
}

int analogReadDataProducer(void* context, int id, int outChannelId){
  intptr_t inputPin = (intptr_t)context;
  return analogRead(inputPin);
}

int USBKeyboardNumberDataProducer(void* context, int id, int outChannelId){
  USBKeyboardContextType *usbKeyboardContext = (USBKeyboardContextType *) context;
  return usbKeyboardContext->latestValue;
}

int USBGamepadAnalogDataProducer(void* context, int id, int outChannelId){
  USBGamepadContextType *usbGamepadContext = (USBGamepadContextType *) context;
  return usbGamepadContext->parentDeviceDescriptor->latest_report[id];
}

//overload to easily use the system's native scale 0-2047 (11 bit values)
int normalizeScaleRange(int value, int inMin, int inMax){
  return normalizeScaleRange(value, inMin, inMax, NATIVE_MIN_VALUE, NATIVE_MAX_VALUE);
}

int normalizeScaleRange(int value, int inMin, int inMax, int outMin, int outMax){
  // clamp input to its expected range
  value = clamp(value, inMin, inMax);
   if(inMin != outMin || inMax != outMax){
    // avoid division by zero if input range is empty
    int inSpan = inMax - inMin;
    if (inSpan == 0) {
      // no span: map directly to midpoint
      value = (outMin+outMax)/2;
    } else {
      // perform integer linear mapping: out = outMin + (value - inMin) * outSpan / inSpan
      int outSpan = outMax - outMin;
      value = outMin + (int)((long)(value - inMin) * outSpan / inSpan);
    }
  }
  return value;
}

int evaluateSingleChannelMixerValue(MixerChannelDescriptor* mixer, bool channel1){
  if(channel1){
    int channel1Value = getLatestInputData(mixer->inputChannel1Descriptor, -1);
    channel1Value = normalizeScaleRange(channel1Value, mixer->inputChannel1Descriptor->minRange, mixer->inputChannel1Descriptor->maxRange);
    channel1Value = channel1Value * mixer->channel1Scale/100;
    channel1Value = channel1Value + mixer->channel1Offset;
    if(mixer->channel1Invert){
      channel1Value = NATIVE_MAX_VALUE - channel1Value + NATIVE_MIN_VALUE;
    }
    return channel1Value;
  }
  //else channel 2:
  int channel2Value = getLatestInputData(mixer->inputChannel2Descriptor, -1);
  channel2Value = normalizeScaleRange(channel2Value, mixer->inputChannel2Descriptor->minRange, mixer->inputChannel2Descriptor->maxRange);
  channel2Value = channel2Value * mixer->channel2Scale/100;
  channel2Value = channel2Value + mixer->channel2Offset;
  if(mixer->channel2Invert){
    channel2Value = NATIVE_MAX_VALUE - channel2Value + NATIVE_MIN_VALUE;
  }
  return channel2Value;
}

int evaluateMixerValue(MixerChannelDescriptor* mixer){
  if(!mixer->inputChannel1Descriptor){
    //invalid input/input not present, return failsafe instead
    return INT_MAX;
  }
  int channel1Value = evaluateSingleChannelMixerValue(mixer, true);
  if(mixer->operation == MIXER_OP_CH1_ONLY){
    channel1Value = clamp(channel1Value, mixer->mixerResultDescriptor->minRange, mixer->mixerResultDescriptor->maxRange);
    return channel1Value;
  }
  //otherwise use both channel 1 and channel 2 values
  if(!mixer->inputChannel2Descriptor){
    //invalid input/input not present, return failsafe instead
    return INT_MAX;
  }
  int channel2Value = evaluateSingleChannelMixerValue(mixer, false);
  int mixedValue;
  //useful precursors for signed operations, treat 1024 as midpoint
  int channel1Centered = channel1Value - NATIVE_MID_VALUE;
  int channel2Centered = channel2Value - NATIVE_MID_VALUE;
  switch (mixer->operation){
    case MIXER_OP_ADD:
      mixedValue = channel1Value + channel2Value;
      break;
    case MIXER_OP_ADDS:
      mixedValue = channel1Centered + channel2Centered + NATIVE_MID_VALUE;
      break;
    case MIXER_OP_AVG:
      mixedValue = (channel1Value + channel2Value + 1)/2;
      break;
    case MIXER_OP_DIFF:
      mixedValue = abs(channel1Value - channel2Value);
      break;
    case MIXER_OP_SUB:
      mixedValue = channel1Value - channel2Value;
      break;
    case MIXER_OP_SUBS:
      mixedValue = channel1Centered - channel2Centered + NATIVE_MID_VALUE;
      break;
    case MIXER_OP_MUL:
      mixedValue = channel1Value * channel2Value;
      mixedValue = (mixedValue + (1 << (NATIVE_BITCOUNT - 1))) >> NATIVE_BITCOUNT;       //normalize scale
      break;
    case MIXER_OP_MULS:
      mixedValue = channel1Centered * channel2Centered;
      mixedValue += (mixedValue >= 0) 
                   ? (1 << (NATIVE_BITCOUNT - 2))
                   : -(1 << (NATIVE_BITCOUNT - 2));    //symmetric rounding bias
      mixedValue >>= (NATIVE_BITCOUNT - 1);            //renormalize
      mixedValue = mixedValue + 1024;
      break;
    case MIXER_OP_DIV:
      if(channel2Value == 0){
        mixedValue = NATIVE_MAX_VALUE; //div by zero -> max
        break;
      }
      mixedValue = (NATIVE_MAX_VALUE * channel1Value + channel2Value/2) / channel2Value; //normalize scale
      break;
    case MIXER_OP_DIVS:
      if(channel2Centered == 0) { //div by zero -> max or -max
        mixedValue = (channel1Centered >= 0) ? NATIVE_MAX_VALUE : NATIVE_MIN_VALUE;
        break;
      }
      mixedValue = (channel1Centered << (NATIVE_BITCOUNT - 1));
      mixedValue += (channel2Centered > 0) ? (channel2Centered / 2) : -((-channel2Centered) / 2);  //signed rounding bias
      mixedValue = mixedValue / channel2Centered;
      mixedValue += 1024;
      break;
    case MIXER_OP_MIN:
      mixedValue = (channel1Value < channel2Value) ? channel1Value : channel2Value;
      break;
    case MIXER_OP_MAX:
      mixedValue = (channel1Value > channel2Value) ? channel1Value : channel2Value;
      break;
    case MIXER_OP_AMIN:
      if(abs(channel1Centered) <= abs(channel2Centered)){
        mixedValue = channel1Value;
      } else {
        mixedValue = channel2Value;
      }
      break;
    case MIXER_OP_AMAX:
      if(abs(channel1Centered) >= abs(channel2Centered)){
        mixedValue = channel1Value;
      } else {
        mixedValue = channel2Value;
      }
      break;
    default:
      mixedValue = 1025; //near-center default value for invalid configurations
  }
  mixedValue = clamp(mixedValue, mixer->mixerResultDescriptor->minRange, mixer->mixerResultDescriptor->maxRange);
  return mixedValue;
}

int mixerDataProducer(void* context, int id, int outChannelId){
  MixerChannelDescriptor *mixerContext = ((MixerChannelDescriptor *)context);
  int value = evaluateMixerValue(mixerContext);
  if (value == INT_MAX){
    //invalid value, return failsafe instead
    SerialDebug<DEBUG_WARN>("WARN: mixer has null input, attempting fallback");
    if(outChannelId == -1){ //no assigned output channel, likely in the mixers view
      SerialDebug<DEBUG_WARN>("WARN: no assigned output channel for mixer");
      return 0;
    }
    InputChannelDescriptor * failsafe = failsafeChannels[outChannelId];
    return failsafe->getLatestInputData(failsafe->context, failsafe->id,outChannelId);
  }
  return value;

}

const InputChannelDescriptor defaultInputDescriptor = {
    .getLatestInputData = defaultInputDataProducer,
    .configureChannelInput = NULL,
    .cleanupInputContextFn = NULL,
    .id = -1,
    .context = NULL,
    .minRange = 0,
    .maxRange = 2047,
    .name = ""
};

////// cleanup for input context, when needed
//when only freeing a malloc() is needed:
void SimpleFreeInputContext(void *ptr, InputFunctionType _functionType){
  if(ptr != NULL){
  free(ptr);
  }
}


/////  ------ input channel pool handling ------
/* initialize input pool (copy prototype into every slot but mark unused) */
void Pool_Init(InputDescriptorPool *pool, const InputChannelDescriptor *prototype) {
    pool->capacity = MAX_INPUTS;
    pool->count = 0;
    for (int i = 0; i < pool->capacity; ++i) {
        pool->items[i] = *prototype;   /* struct copy */
        pool->used[i] = false;
    }
}

/* allocate a descriptor from the pool, returns pointer or NULL if full */
InputChannelDescriptor *Pool_Allocate(InputDescriptorPool *pool) {
    for (int i = 0; i < pool->capacity; ++i) {
        if (!pool->used[i]) {
            pool->used[i] = true;
            pool->count++;
            /* customize defaults if desired (e.g., set id = i) */
            pool->items[i].id = i;
            return &pool->items[i];
        }
    }
    return NULL; /* no free slot */
}

/* release previously allocated descriptor */
void Pool_Release(InputDescriptorPool *pool, InputChannelDescriptor *desc) {
    ptrdiff_t idx = desc - pool->items;
    if (idx < 0 || idx >= pool->capacity) return; /* not from this pool */
    if (pool->used[idx]) {
        pool->used[idx] = false;
        pool->count--;
        /* restore to prototype if desired */
        pool->items[idx] = defaultInputDescriptor;
        pool->items[idx].id = -1;
    }
}

/* find descriptor by id (returns pointer or NULL) */
InputChannelDescriptor *Pool_FindByIdAndType(InputDescriptorPool *pool, int id, InputFunctionType type) {
    for (int i = 0; i < pool->capacity; ++i) {
        if (pool->used[i] && pool->items[i].id == id && pool->items[i].inputFunctionType == type) return &pool->items[i];
    }
    return NULL;
}

/* find descriptor index in pool by id (returns int or -1) */
int Pool_FindIndexByIdAndType(InputDescriptorPool *pool, int id, InputFunctionType type) {
    for (int i = 0; i < pool->capacity; ++i) {
        if (pool->used[i] && pool->items[i].id == id && pool->items[i].inputFunctionType == type) return i;
    }
    return -1;
}

//find next populated index, or return -1 if none
int Pool_FindNextUsedIndex(InputDescriptorPool *pool, int index){
  for (int i = index; i < pool->capacity; ++i) {
    if (pool->used[i]){
      return i;
    }
  }
  return -1;
}

//find previous populated index, or return -1 if none
int Pool_FindPreviousUsedIndex(InputDescriptorPool *pool, int index){
  for (int i = index; i >= 0; --i) {
    if (pool->used[i]){
      return i;
    }
  }
  return -1;
}

InputChannelDescriptor* findInputDescriptorWithTypeNameAndId(InputDescriptorPool *pool, InputFunctionType type, const char* name, int id){
  InputChannelDescriptor *matchingInputDescriptor = NULL;
  if(type == INPUT_FUNCTION_USB_GAMEPAD_STICK){ //allow fuzzy matching of USB gamepad inputs
  matchingInputDescriptor = FindInputChannelDescriptorByInputNameSuffix(
    pool, name, INPUT_NAME_LEN);
  if(matchingInputDescriptor == NULL){
    matchingInputDescriptor = FindInputChannelDescriptorByInputNameSuffix(
      pool, name, 3); //fuzzy match last 3 chars eg. '-XY'
  }
  if(matchingInputDescriptor == NULL){ //still no USB matches, skip populating this channel
    return NULL;
  }
  } else {  //all other input types
    matchingInputDescriptor = Pool_FindByIdAndType(pool, id, type);
  }
  return matchingInputDescriptor;
}

//defaults to INPUT_FUNCTION_CONST_FIXED
InputChannelDescriptor *AllocFixedValueInput(InputDescriptorPool *pool, const char* name, int value){
  InputChannelDescriptor *newInput = Pool_Allocate(pool);
  newInput->minRange = NATIVE_MIN_VALUE;
  newInput->maxRange = NATIVE_MAX_VALUE;
  snprintf(newInput->name,INPUT_NAME_LEN, name);
  newInput->inputFunctionType = INPUT_FUNCTION_CONST_FIXED;
  newInput->getLatestInputData = fixedInputDataProducer;
  newInput->configureChannelInput = NULL;
  newInput->cleanupInputContextFn = NULL;
  newInput->id = value;
  newInput->context = (void *)(intptr_t)value;
  return newInput;
}

//update a reconfigurable channel value. Only valid for channels of type INPUT_FUNCTION_CONFIG_VALUE
bool SetConfigValueInput(InputChannelDescriptor *channel, int value){
  if (channel->inputFunctionType == INPUT_FUNCTION_CONFIG_VALUE){
    channel->context = (void *)(intptr_t)value;
    return true;
  }
  //attempted to configure invalid channel, do nothing and return error state
  return false;
}

//for use with INPUT_FUNCTION_CONFIG_VALUE, extends AllocFixedValueInput to enable reconfiguration
InputChannelDescriptor *AllocConfigValueInput(InputDescriptorPool *pool, const char* name, int value){
  InputChannelDescriptor *newInput = AllocFixedValueInput(pool, name, value);
  newInput->inputFunctionType = INPUT_FUNCTION_CONFIG_VALUE;
  newInput->configureChannelInput = SetConfigValueInput;
  newInput->cleanupInputContextFn = NULL;
  return newInput;
}
//use with INPUT_FUNCTION_IO_ADC type only
InputChannelDescriptor *AllocADCInput(InputDescriptorPool *pool, const char* name, pin_size_t pin){
  InputChannelDescriptor *newInput = Pool_Allocate(pool);
  newInput->minRange = 0;
  newInput->maxRange = 1023;
  snprintf(newInput->name,INPUT_NAME_LEN, name);
  newInput->inputFunctionType = INPUT_FUNCTION_IO_ADC;
  newInput->getLatestInputData = analogReadDataProducer;
  newInput->configureChannelInput = NULL;
  newInput->cleanupInputContextFn = NULL;
  newInput->id = (int)pin;
  newInput->context = (void *)(intptr_t)pin;
  return newInput;
}

InputChannelDescriptor *AllocDefaultMixerInput(InputDescriptorPool *pool, int mixerIndex, MixerChannelDescriptor * mixerChannel){
  InputChannelDescriptor *newInput = Pool_Allocate(pool);
  newInput->minRange = NATIVE_MIN_VALUE;
  newInput->maxRange = NATIVE_MAX_VALUE;
  snprintf(newInput->name,INPUT_NAME_LEN, "Mix %d", mixerIndex);
  newInput->inputFunctionType = INPUT_FUNCTION_MIXER;
  newInput->getLatestInputData = mixerDataProducer;
  newInput->configureChannelInput = NULL;
  newInput->cleanupInputContextFn = NULL;
  newInput->id = mixerIndex;
  newInput->context = (void *)mixerChannel;
  return newInput;
}

void initDefaultInputDescriptors(InputDescriptorPool *pool){
  Pool_Init(pool, &defaultInputDescriptor);
  AllocFixedValueInput(pool, "Zero (fixed)", NATIVE_MIN_VALUE);
  AllocFixedValueInput(pool, "Mid (fixed)", NATIVE_MID_VALUE);
  AllocFixedValueInput(pool, "Max (fixed)", NATIVE_MAX_VALUE);
  AllocADCInput(pool, "ADC0", A0);
  AllocADCInput(pool, "ADC1", A1);
  AllocADCInput(pool, "ADC2", A2);
  AllocADCInput(pool, "ADC3", A3);
  AllocADCInput(pool, "ADC4", A4);
  AllocADCInput(pool, "ADC5", A5);
  AllocADCInput(pool, "ADC6", A6);
  AllocADCInput(pool, "ADC7", A7);
}

void initMixerInputDescriptors(InputDescriptorPool *inputChannelsPool, MixerChannelDescriptor (&mixerChannels)[MAX_MIXERS]){
  for (int i=0; i<MAX_MIXERS; i++){
    mixerChannels[i].id = i;
    mixerChannels[i].inputChannel1Descriptor = Pool_FindByIdAndType(inputChannelsPool, 0, INPUT_FUNCTION_CONST_FIXED);
    mixerChannels[i].inputChannel2Descriptor = Pool_FindByIdAndType(inputChannelsPool, 0, INPUT_FUNCTION_CONST_FIXED);
    mixerChannels[i].operation = MIXER_OP_ADD;
    mixerChannels[i].channel1Scale = 100;
    mixerChannels[i].channel2Scale = 100;
    mixerChannels[i].channel1Offset = 0;
    mixerChannels[i].channel2Offset = 0;
    mixerChannels[i].channel1Invert = false;
    mixerChannels[i].channel2Invert = false;
    snprintf(mixerChannels[i].name,INPUT_NAME_LEN, "Mix %d", i);
    mixerChannels[i].mixerResultDescriptor = AllocDefaultMixerInput(inputChannelsPool, i, &mixerChannels[i]);
  }
}

void initOutputAndDefaultInputChannelDescriptors(OutputChannelDescriptor *outputChannels,InputDescriptorPool *inputChannelsPool, MixerChannelDescriptor (&mixerChannels)[MAX_MIXERS], InputChannelDescriptor* (&failsafeChannels)[MAX_CHANNELS]){
  initDefaultInputDescriptors(inputChannelsPool);
  initMixerInputDescriptors(inputChannelsPool, mixerChannels);
  //build full list of default channels
  for (int i=0; i < MAX_CHANNELS; i++){
    OutputChannelDescriptor *p = &outputChannels[i];  //get pointer to specific output channel
    char buf[INPUT_NAME_LEN];
    snprintf(buf, INPUT_NAME_LEN, "default (%d)", i);
    p->inputChannelDescriptor = AllocConfigValueInput(inputChannelsPool, buf, 1024);
    p->minRange = 0;
    p->maxRange = 2047;
    p->outputChannelNumber = i;
    p->inputChannelDescriptor->id=i;
    snprintf(p->name, OUTPUT_NAME_LEN, "channel %d", i);
    failsafeChannels[i] = p->inputChannelDescriptor;
  }
}

InputChannelDescriptor *AllocateUSBKeyboardNumberInputChannel(InputDescriptorPool *pool, int id, const char* name, USBInputDeviceDescriptor * descriptor) {
  USBKeyboardContextType *newContext = (USBKeyboardContextType *)malloc(sizeof (USBKeyboardContextType));
  InputChannelDescriptor *newInput = Pool_Allocate(pool);
  newContext->parentDeviceDescriptor = descriptor;
  newContext->latestValue = 0;
  newContext->lastUpdateMillis = 0;
  newInput->minRange = 0;
  newInput->maxRange = 9;
  snprintf(newInput->name,INPUT_NAME_LEN, name);
  newInput->inputFunctionType = INPUT_FUNCTION_USB_KB_NUMBER;
  newInput->getLatestInputData = USBKeyboardNumberDataProducer;
  newInput->configureChannelInput = NULL;
  newInput->cleanupInputContextFn = NULL;
  newInput->id = id;
  newInput->context = newContext;
  descriptor->inputChannels[id] = newInput;
  return newInput;
}

InputChannelDescriptor *AllocateUSBGamepadStickInputChannel(InputDescriptorPool *pool, USBGamepadContextType *context, int id, const char* name, USBInputDeviceDescriptor * descriptor) {
  InputChannelDescriptor *newInput = Pool_Allocate(pool);
  context->parentDeviceDescriptor = descriptor;
  context->lastUpdateMillis = 0;
  newInput->minRange = 0x00;
  newInput->maxRange = 0xff;
  snprintf(newInput->name,INPUT_NAME_LEN, name);
  newInput->inputFunctionType = INPUT_FUNCTION_USB_GAMEPAD_STICK;
  newInput->getLatestInputData = USBGamepadAnalogDataProducer;
  newInput->configureChannelInput = NULL;
  newInput->cleanupInputContextFn = NULL;
  newInput->id = id;
  newInput->context = context;
  return newInput;
}

USBGamepadContextType* AllocateUSBGamepadStickChannels(InputDescriptorPool *pool, USBInputDeviceDescriptor * descriptor){
    USBGamepadContextType *newContext = (USBGamepadContextType *)malloc(sizeof (USBGamepadContextType));
    descriptor->contextPointer = newContext;
    for(int i=0; i<descriptor->layoutDef->analogInputCount; i++){
      //const int stickIndexes[] = {1, 2, 3, 4, 8, 9}; //TODO: replace hardcoded indexes with values as configured by controller type in controllerData.cpp
      char buf[INPUT_NAME_LEN];
      snprintf(buf, INPUT_NAME_LEN, "%s-%s", descriptor->layoutDef->name, descriptor->layoutDef->analogInputNames[i]);
      descriptor->inputChannels[i] = AllocateUSBGamepadStickInputChannel(pool,newContext, descriptor->layoutDef->analogInputReportOffsets[i], buf, descriptor);
    }
    return newContext;
}

USBInputDeviceDescriptor* findUSBDescriptorByDevAddrAndInstance(USBInputDeviceDescriptor* arr, uint8_t dev_addr, uint8_t instance){
  for (int i=0; i<MAX_USB_DEVICE_DESCRIPTORS; i++){
    if(arr[i].dev_addr == dev_addr && arr[i].instance == instance){
      return &arr[i];
    }
  }
  return NULL;
}

int getFirstFreeUSBInputDescriptorIndex(USBInputDeviceDescriptor* arr){
  for(int i=0; i<MAX_USB_DEVICE_DESCRIPTORS; i++){
    if(arr[i].vid==0){ //no valid VID == 0;
      return i;
    }
  }
  return -1;
}

void releaseUSBInputChannels(InputDescriptorPool *pool, USBInputDeviceDescriptor *desc, OutputChannelDescriptor (&outChannelsArr)[MAX_CHANNELS], MixerChannelDescriptor (&mixerChannels)[MAX_MIXERS], InputChannelDescriptor* (&failsafeChannels)[MAX_CHANNELS]){
  SerialDebug<DEBUG_USB>("Freeing USB INPUT");
  for(int i=0; i < MAX_INPUT_CHANNELS_PER_USB_DEVICE; i++){
    if(desc -> inputChannels[i] != NULL){
      //clean-up any outputs based on this input channel, restore them to default state
      for(int j=0; j< MAX_CHANNELS; j++){
        if(outChannelsArr[j].inputChannelDescriptor == desc -> inputChannels[i]){
          outChannelsArr[j].inputChannelDescriptor = failsafeChannels[j];
          SerialDebug<DEBUG_USB|DEBUG_WARN|DEBUG_LOG>("Restoring active channel ");
          SerialDebug<DEBUG_USB|DEBUG_WARN|DEBUG_LOG>(j);
          SerialDebugln<DEBUG_USB|DEBUG_WARN|DEBUG_LOG>(" to failsafe value");
        }
      }
      for(int j=0; j< MAX_MIXERS; j++){
        if(mixerChannels[j].inputChannel1Descriptor == desc -> inputChannels[i]){
          mixerChannels[j].inputChannel1Descriptor = NULL;
          SerialDebug<DEBUG_USB|DEBUG_WARN|DEBUG_LOG>("Restoring mixer channel ");
          SerialDebug<DEBUG_USB|DEBUG_WARN|DEBUG_LOG>(j);
          SerialDebugln<DEBUG_USB|DEBUG_WARN|DEBUG_LOG>("(input 1) to failsafe value");
        }
        if(mixerChannels[j].inputChannel2Descriptor == desc -> inputChannels[i]){
          mixerChannels[j].inputChannel2Descriptor = NULL;
          SerialDebug<DEBUG_USB|DEBUG_WARN|DEBUG_LOG>("Restoring mixer channel ");
          SerialDebug<DEBUG_USB|DEBUG_WARN|DEBUG_LOG>(j);
          SerialDebugln<DEBUG_USB|DEBUG_WARN|DEBUG_LOG>("(input 2) to failsafe value");
        }
      }
      //cleanup any data from the input function (generally unused)
      if (desc -> inputChannels[i] -> cleanupInputContextFn != NULL){
        desc -> inputChannels[i] -> cleanupInputContextFn(desc -> inputChannels[i]->context, desc -> inputChannels[i]->inputFunctionType);
      }
      Pool_Release(pool, desc -> inputChannels[i]);
    }
  }
  free(desc->contextPointer);
}

int ends_with(const char *str, const char *suffix) {
  size_t str_len = strlen(str);
  size_t suffix_len = strlen(suffix);

  return (str_len >= suffix_len) &&
         (!memcmp(str + str_len - suffix_len, suffix, suffix_len));
}

char * last_n( const char *s, size_t n )
{
    size_t length = strlen( s );

    return ( char * )( length < n ? s : s + length - n );
}

//helpful as fuzzy input name match, prefer exact match (such as "8Bitdo Pro2-LX"), but enable fuzzy match "OtherBrand-LX"
InputChannelDescriptor *FindInputChannelDescriptorByInputNameSuffix(InputDescriptorPool *pool, const char *inputName, int suffixLength){
  char *compareSuffix = last_n(inputName, suffixLength);
  int inputIndex  = Pool_FindNextUsedIndex(pool, 0);
  while(inputIndex != -1){
    InputChannelDescriptor * inputChannel = &(pool->items[inputIndex]);
    if(ends_with(inputChannel->name, compareSuffix)){
      return inputChannel;
    }
    inputIndex = Pool_FindNextUsedIndex(pool, inputIndex+1);
  }
  return NULL;
}