#include <stdint.h>
#include <cstddef>
#include <cstdio>

#include "ChannelManager.h"

// Interpret context as an integer value stored via an integer-sized pointer.
// always returns the value specified by its context directly
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

void AllocFixedValueInput(InputDescriptorPool *pool, char* name, int value){
  InputChannelDescriptor *newInput = Pool_Allocate(pool);
  newInput->minRange = 0;
  newInput->maxRange = 2047;
  snprintf(newInput->name,INPUT_NAME_LEN, name);
  newInput->inputFunctionType = INPUT_FUNCTION_CONST_FIXED;
  newInput->getLatestInputData = fixedInputDataProducer;
  newInput->id = value;
  newInput->context = (void *)(intptr_t)value;
}

void initDefaultInputDescriptors(InputDescriptorPool *pool){
  Pool_Init(pool, &defaultInputDescriptor);
  AllocFixedValueInput(pool, "Zero (fixed)", 0);
  AllocFixedValueInput(pool, "Mid (fixed)", 1024);
  AllocFixedValueInput(pool, "Max (fixed)", 2047);
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