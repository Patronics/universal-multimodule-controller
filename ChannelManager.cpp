#include <stdint.h>
#include <cstddef>
#include <cstdio>
#include <Arduino.h>

#include "ChannelManager.h"
#include "MultiModule.h"

// Interpret context as an integer value stored via an integer-sized pointer.
// always returns the value specified by its context directly
// compatible with INPUT_FUNCTION_CONST_FIXED and INPUT_FUNCTION_CONFIG_VALUE input types
int fixedInputDataProducer(void* context, int id){
  intptr_t fixedValue = (intptr_t)context;
  (void)id; // id unused in this example, supress warning with no-op
  return (int)fixedValue;
}

//a sane default input, sets the channel output to its midpoint
int defaultInputDataProducer(void* context, int id){
  (void)context; // explicitly ignore incoming context
  // Provide a consistent hardcoded value for fixedInputDataProducer to return.
  // Use intptr_t cast so we can pass an integer via the void* context parameter.
  const int midpoint = 1024;
  return fixedInputDataProducer((void *)(intptr_t)midpoint, id);
}

int analogReadDataProducer(void* context, int id){
  intptr_t inputPin = (intptr_t)context;
  return analogRead(inputPin);
}

const InputChannelDescriptor defaultInputDescriptor = {
    .getLatestInputData = defaultInputDataProducer,
    .id = -1,
    .context = NULL,
    .minRange = 0,
    .maxRange = 2047,
    .name = ""
};

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

//defaults to INPUT_FUNCTION_CONST_FIXED
InputChannelDescriptor *AllocFixedValueInput(InputDescriptorPool *pool, const char* name, int value){
  InputChannelDescriptor *newInput = Pool_Allocate(pool);
  newInput->minRange = 0;
  newInput->maxRange = 2047;
  snprintf(newInput->name,INPUT_NAME_LEN, name);
  newInput->inputFunctionType = INPUT_FUNCTION_CONST_FIXED;
  newInput->getLatestInputData = fixedInputDataProducer;
  newInput->configureChannelInput = NULL;
  newInput->id = value;
  newInput->context = (void *)(intptr_t)value;
  return newInput;
}

//for use with INPUT_FUNCTION_CONFIG_VALUE, extends AllocFixedValueInput to enable reconfiguration
InputChannelDescriptor *AllocConfigValueInput(InputDescriptorPool *pool, const char* name, int value){
  InputChannelDescriptor *newInput = AllocFixedValueInput(pool, name, value);
  newInput->inputFunctionType = INPUT_FUNCTION_CONFIG_VALUE;
  newInput->configureChannelInput = NULL;  //TODO: implement this configuration function
  return newInput;
}
//use with INPUT_FUNCTION_IO_ADC type only
InputChannelDescriptor *AllocADCInput(InputDescriptorPool *pool, const char* name, pin_size_t pin){
  InputChannelDescriptor *newInput = Pool_Allocate(pool);
  newInput->minRange = 0;
  newInput->maxRange = 4095;
  snprintf(newInput->name,INPUT_NAME_LEN, name);
  newInput->inputFunctionType = INPUT_FUNCTION_IO_ADC;
  newInput->getLatestInputData = analogReadDataProducer;
  newInput->configureChannelInput = NULL;
  newInput->id = (int)pin;
  newInput->context = (void *)(intptr_t)pin;
  return newInput;
}

void initDefaultInputDescriptors(InputDescriptorPool *pool){
  Pool_Init(pool, &defaultInputDescriptor);
  AllocFixedValueInput(pool, "Zero (fixed)", 0);
  AllocFixedValueInput(pool, "Mid (fixed)", 1024);
  AllocFixedValueInput(pool, "Max (fixed)", 2047);
  AllocADCInput(pool, "ADC0", A0);
  AllocADCInput(pool, "ADC1", A1);
  AllocADCInput(pool, "ADC2", A2);
}

void initOutputAndDefaultInputChannelDescriptors(OutputChannelDescriptor *outputChannels,InputDescriptorPool *inputChannelsPool, int numChannels){
  for (int i=0; i < numChannels; i++){
    OutputChannelDescriptor *p = &outputChannels[i];  //get pointer to specific output channel
    p->inputChannelDescriptor = Pool_Allocate(inputChannelsPool);
    p->minRange = 0;
    p->maxRange = 2047;
    p->outputChannelNumber = i;
    p->inputChannelDescriptor->id=i;
    p->inputChannelDescriptor->inputFunctionType = INPUT_FUNCTION_CONFIG_VALUE;
    snprintf(p->inputChannelDescriptor->name, INPUT_NAME_LEN, "default (%d)", i);
    snprintf(p->name, INPUT_NAME_LEN, "channel %d", i);
  }
}