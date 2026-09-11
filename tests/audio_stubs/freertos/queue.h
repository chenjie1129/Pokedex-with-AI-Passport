#pragma once
#include "FreeRTOS.h"
typedef void *QueueHandle_t;
QueueHandle_t xQueueCreate(unsigned count, unsigned size);
void vQueueDelete(QueueHandle_t queue);
BaseType_t xQueueOverwrite(QueueHandle_t queue, const void *value);
BaseType_t xQueueReceive(QueueHandle_t queue, void *value, uint32_t timeout);
BaseType_t xQueuePeek(QueueHandle_t queue, void *value, uint32_t timeout);
