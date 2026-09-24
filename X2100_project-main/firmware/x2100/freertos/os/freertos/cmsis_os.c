/* ----------------------------------------------------------------------
 * $Date:        5. February 2013
 * $Revision:    V1.02
 *
 * Project:      CMSIS-RTOS API
 * Title:        cmsis_os.c
 *
 * Version 0.02
 *    Initial Proposal Phase
 * Version 0.03
 *    osKernelStart added, optional feature: main started as thread
 *    osSemaphores have standard behavior
 *    osTimerCreate does not start the timer, added osTimerStart
 *    osThreadPass is renamed to osThreadYield
 * Version 1.01
 *    Support for C++ interface
 *     - const attribute removed from the osXxxxDef_t typedef's
 *     - const attribute added to the osXxxxDef macros
 *    Added: osTimerDelete, osMutexDelete, osSemaphoreDelete
 *    Added: osKernelInitialize
 * Version 1.02
 *    Control functions for short timeouts in microsecond resolution:
 *    Added: osKernelSysTick, osKernelSysTickFrequency, osKernelSysTickMicroSec
 *    Removed: osSignalGet 
 *    
 *  
 *----------------------------------------------------------------------------
 *
 * Portions Copyright © 2016 STMicroelectronics International N.V. All rights reserved.
 * Portions Copyright (c) 2013 ARM LIMITED
 * All rights reserved.
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *  - Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  - Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *  - Neither the name of ARM  nor the names of its contributors may be used
 *    to endorse or promote products derived from this software without
 *    specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL COPYRIGHT HOLDERS AND CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *---------------------------------------------------------------------------*/

 /**
  ******************************************************************************
  * @file    cmsis_os.c
  * @author  MCD Application Team
  * @date    13-July-2017
  * @brief   CMSIS-RTOS API implementation for FreeRTOS V9.0.0
  ******************************************************************************
  * @attention
  *
  * Redistribution and use in source and binary forms, with or without 
  * modification, are permitted, provided that the following conditions are met:
  *
  * 1. Redistribution of source code must retain the above copyright notice, 
  *    this list of conditions and the following disclaimer.
  * 2. Redistributions in binary form must reproduce the above copyright notice,
  *    this list of conditions and the following disclaimer in the documentation
  *    and/or other materials provided with the distribution.
  * 3. Neither the name of STMicroelectronics nor the names of other 
  *    contributors to this software may be used to endorse or promote products 
  *    derived from this software without specific written permission.
  * 4. This software, including modifications and/or derivative works of this 
  *    software, must execute solely and exclusively on microcontroller or
  *    microprocessor devices manufactured by or for STMicroelectronics.
  * 5. Redistribution and use of this software other than as permitted under 
  *    this license is void and will automatically terminate your rights under 
  *    this license. 
  *
  * THIS SOFTWARE IS PROVIDED BY STMICROELECTRONICS AND CONTRIBUTORS "AS IS" 
  * AND ANY EXPRESS, IMPLIED OR STATUTORY WARRANTIES, INCLUDING, BUT NOT 
  * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY, FITNESS FOR A 
  * PARTICULAR PURPOSE AND NON-INFRINGEMENT OF THIRD PARTY INTELLECTUAL PROPERTY
  * RIGHTS ARE DISCLAIMED TO THE FULLEST EXTENT PERMITTED BY LAW. IN NO EVENT 
  * SHALL STMICROELECTRONICS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
  * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
  * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, 
  * OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF 
  * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING 
  * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
  * EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
  *
  ******************************************************************************
  */ 

#include <string.h>
#include <os.h>
#include "cmsis_os.h"

/*
 * ARM Compiler 4/5
 */
#if   defined ( __CC_ARM )

  #define __ASM            __asm                                      
  #define __INLINE         __inline                                     
  #define __STATIC_INLINE  static __inline

  #include "cmsis_armcc.h"

/*
 * GNU Compiler
 */
#elif defined ( __GNUC__ )

  #define __ASM            __asm                                      /*!< asm keyword for GNU Compiler          */
  #define __INLINE         inline                                     /*!< inline keyword for GNU Compiler       */
  #define __STATIC_INLINE  static inline

//  #include "cmsis_gcc.h"


/*
 * IAR Compiler
 */
#elif defined ( __ICCARM__ )

  #ifndef   __ASM
    #define __ASM                     __asm
  #endif
  #ifndef   __INLINE
    #define __INLINE                  inline
  #endif
  #ifndef   __STATIC_INLINE
    #define __STATIC_INLINE           static inline
  #endif

  #include <cmsis_iar.h>
#endif

extern void xPortSysTickHandler(void);

/* Convert from CMSIS type osPriority to FreeRTOS priority number */
static unsigned portBASE_TYPE makeFreeRtosPriority (osPriority priority)
{
  unsigned portBASE_TYPE fpriority = tskIDLE_PRIORITY;
  
  if (priority != osPriorityError) {
    fpriority += (priority - osPriorityIdle);
  }
  
  return fpriority;
}

#if (INCLUDE_uxTaskPriorityGet == 1)
/* Convert from FreeRTOS priority number to CMSIS type osPriority */
static osPriority makeCmsisPriority (unsigned portBASE_TYPE fpriority)
{
  osPriority priority = osPriorityError;
  
  if ((fpriority - tskIDLE_PRIORITY) <= (osPriorityRealtime - osPriorityIdle)) {
    priority = (osPriority)((int)osPriorityIdle + (int)(fpriority - tskIDLE_PRIORITY));
  }
  
  return priority;
}
#endif


/* Determine whether we are in thread mode or handler mode. */
static int inHandlerMode (void)
{
  return os_in_handler_mode();
}

#if (defined (osFeature_SysTick)  &&  (osFeature_SysTick != 0))     // System Timer available
/**
* @brief  Get the value of the Kernel SysTick timer
* @param  None
* @retval None
* @note   MUST REMAIN UNCHANGED: \b osKernelSysTick shall be consistent in every CMSIS-RTOS.
*/
uint32_t osKernelSysTick(void)
{
  if (inHandlerMode()) {
    return xTaskGetTickCountFromISR();
  }
  else {
    return xTaskGetTickCount();
  }
}
#endif    // System Timer available

/*********************** Thread Management *****************************/
/**
* @brief  Create a thread and add it to Active Threads and set it to state READY.
* @param  thread_def    thread definition referenced with \ref osThread.
* @param  argument      pointer that is passed to the thread function as start argument.
* @retval thread ID for reference by other functions or NULL in case of error.
* @note   MUST REMAIN UNCHANGED: \b osThreadCreate shall be consistent in every CMSIS-RTOS.
*/
osThreadId osThreadCreate (const osThreadDef_t *thread_def, void *argument)
{
  TaskHandle_t handle;
  
#if( configSUPPORT_STATIC_ALLOCATION == 1 ) &&  ( configSUPPORT_DYNAMIC_ALLOCATION == 1 )
  if((thread_def->buffer != NULL) && (thread_def->controlblock != NULL)) {
    handle = xTaskCreateStatic((TaskFunction_t)thread_def->pthread,(const portCHAR *)thread_def->name,
              thread_def->stacksize, argument, makeFreeRtosPriority(thread_def->tpriority),
              thread_def->buffer, thread_def->controlblock);
  }
  else {
    if (xTaskCreate((TaskFunction_t)thread_def->pthread,(const portCHAR *)thread_def->name,
              thread_def->stacksize, argument, makeFreeRtosPriority(thread_def->tpriority),
              &handle) != pdPASS)  {
      return NULL;
    } 
  }
#elif( configSUPPORT_STATIC_ALLOCATION == 1 )

    handle = xTaskCreateStatic((TaskFunction_t)thread_def->pthread,(const portCHAR *)thread_def->name,
              thread_def->stacksize, argument, makeFreeRtosPriority(thread_def->tpriority),
              thread_def->buffer, thread_def->controlblock);
#else
  if (xTaskCreate((TaskFunction_t)thread_def->pthread,(const portCHAR *)thread_def->name,
                   thread_def->stacksize, argument, makeFreeRtosPriority(thread_def->tpriority),
                   &handle) != pdPASS)  {
    return NULL;
  }     
#endif
  
  return handle;
}

/**
* @brief  Return the thread ID of the current running thread.
* @retval thread ID for reference by other functions or NULL in case of error.
* @note   MUST REMAIN UNCHANGED: \b osThreadGetId shall be consistent in every CMSIS-RTOS.
*/
osThreadId osThreadGetId (void)
{
#if ( ( INCLUDE_xTaskGetCurrentTaskHandle == 1 ) || ( configUSE_MUTEXES == 1 ) )
  return xTaskGetCurrentTaskHandle();
#else
	return NULL;
#endif
}

/**
* @brief  Terminate execution of a thread and remove it from Active Threads.
* @param   thread_id   thread ID obtained by \ref osThreadCreate or \ref osThreadGetId.
* @retval  status code that indicates the execution status of the function.
* @note   MUST REMAIN UNCHANGED: \b osThreadTerminate shall be consistent in every CMSIS-RTOS.
*/
osStatus osThreadTerminate (osThreadId thread_id)
{
#if (INCLUDE_vTaskDelete == 1)
  vTaskDelete(thread_id);
  return osOK;
#else
  return osErrorOS;
#endif
}

/**
* @brief  Pass control to next thread that is in state \b READY.
* @retval status code that indicates the execution status of the function.
* @note   MUST REMAIN UNCHANGED: \b osThreadYield shall be consistent in every CMSIS-RTOS.
*/
osStatus osThreadYield (void)
{
  taskYIELD();
  
  return osOK;
}

/**
* @brief   Change priority of an active thread.
* @param   thread_id     thread ID obtained by \ref osThreadCreate or \ref osThreadGetId.
* @param   priority      new priority value for the thread function.
* @retval  status code that indicates the execution status of the function.
* @note   MUST REMAIN UNCHANGED: \b osThreadSetPriority shall be consistent in every CMSIS-RTOS.
*/
osStatus osThreadSetPriority (osThreadId thread_id, osPriority priority)
{
#if (INCLUDE_vTaskPrioritySet == 1)
  vTaskPrioritySet(thread_id, makeFreeRtosPriority(priority));
  return osOK;
#else
  return osErrorOS;
#endif
}

/**
* @brief   Get current priority of an active thread.
* @param   thread_id     thread ID obtained by \ref osThreadCreate or \ref osThreadGetId.
* @retval  current priority value of the thread function.
* @note   MUST REMAIN UNCHANGED: \b osThreadGetPriority shall be consistent in every CMSIS-RTOS.
*/
osPriority osThreadGetPriority (osThreadId thread_id)
{
#if (INCLUDE_uxTaskPriorityGet == 1)
  if (inHandlerMode())
  {
    return makeCmsisPriority(uxTaskPriorityGetFromISR(thread_id));  
  }
  else
  {  
    return makeCmsisPriority(uxTaskPriorityGet(thread_id));
  }
#else
  return osPriorityError;
#endif
}


/****************************  Mutex Management ********************************/
/**
* @brief  Create and Initialize a Mutex object
* @param  mutex_def     mutex definition referenced with \ref osMutex.
* @retval  mutex ID for reference by other functions or NULL in case of error.
* @note   MUST REMAIN UNCHANGED: \b osMutexCreate shall be consistent in every CMSIS-RTOS.
*/
osMutexId osMutexCreate (const osMutexDef_t *mutex_def)
{
#if ( configUSE_MUTEXES == 1)

#if( configSUPPORT_STATIC_ALLOCATION == 1 ) && ( configSUPPORT_DYNAMIC_ALLOCATION == 1 )

  if (mutex_def->controlblock != NULL) {
    return xSemaphoreCreateMutexStatic( mutex_def->controlblock );
     }
  else {
    return xSemaphoreCreateMutex(); 
  }
#elif ( configSUPPORT_STATIC_ALLOCATION == 1 )
  return xSemaphoreCreateMutexStatic( mutex_def->controlblock );
#else  
    return xSemaphoreCreateMutex(); 
#endif
#else
  return NULL;
#endif
}

/**
* @brief Wait until a Mutex becomes available
* @param mutex_id      mutex ID obtained by \ref osMutexCreate.
* @param millisec      timeout value or 0 in case of no time-out.
* @retval  status code that indicates the execution status of the function.
* @note   MUST REMAIN UNCHANGED: \b osMutexWait shall be consistent in every CMSIS-RTOS.
*/
osStatus osMutexWait (osMutexId mutex_id, uint32_t millisec)
{
  TickType_t ticks;
  portBASE_TYPE taskWoken = pdFALSE;  
  
  
  if (mutex_id == NULL) {
    return osErrorParameter;
  }
  
  ticks = 0;
  if (millisec == osWaitForever) {
    ticks = portMAX_DELAY;
  }
  else if (millisec != 0) {
    ticks = millisec / portTICK_PERIOD_MS;
    if (ticks == 0) {
      ticks = 1;
    }
  }
  
  if (inHandlerMode()) {
    if (xSemaphoreTakeFromISR(mutex_id, &taskWoken) != pdTRUE) {
      return osErrorOS;
    }
	portEND_SWITCHING_ISR(taskWoken);
  } 
  else if (xSemaphoreTake(mutex_id, ticks) != pdTRUE) {
    return osErrorOS;
  }
  
  return osOK;
}

/**
* @brief Release a Mutex that was obtained by \ref osMutexWait
* @param mutex_id      mutex ID obtained by \ref osMutexCreate.
* @retval  status code that indicates the execution status of the function.
* @note   MUST REMAIN UNCHANGED: \b osMutexRelease shall be consistent in every CMSIS-RTOS.
*/
osStatus osMutexRelease (osMutexId mutex_id)
{
  osStatus result = osOK;
  portBASE_TYPE taskWoken = pdFALSE;
  
  if (inHandlerMode()) {
    if (xSemaphoreGiveFromISR(mutex_id, &taskWoken) != pdTRUE) {
      return osErrorOS;
    }
    portEND_SWITCHING_ISR(taskWoken);
  }
  else if (xSemaphoreGive(mutex_id) != pdTRUE) 
  {
    result = osErrorOS;
  }
  return result;
}

/**
* @brief Delete a Mutex
* @param mutex_id  mutex ID obtained by \ref osMutexCreate.
* @retval  status code that indicates the execution status of the function.
* @note   MUST REMAIN UNCHANGED: \b osMutexDelete shall be consistent in every CMSIS-RTOS.
*/
osStatus osMutexDelete (osMutexId mutex_id)
{
  if (inHandlerMode()) {
    return osErrorISR;
  }

  vQueueDelete(mutex_id);

  return osOK;
}

/********************  Semaphore Management Functions **************************/

#if (defined (osFeature_Semaphore)  &&  (osFeature_Semaphore != 0))

/**
* @brief Create and Initialize a Semaphore object used for managing resources
* @param semaphore_def semaphore definition referenced with \ref osSemaphore.
* @param count         number of available resources.
* @retval  semaphore ID for reference by other functions or NULL in case of error.
* @note   MUST REMAIN UNCHANGED: \b osSemaphoreCreate shall be consistent in every CMSIS-RTOS.
*/
osSemaphoreId osSemaphoreCreate (const osSemaphoreDef_t *semaphore_def, int32_t count)
{ 
#if( configSUPPORT_STATIC_ALLOCATION == 1 ) && ( configSUPPORT_DYNAMIC_ALLOCATION == 1 )

  osSemaphoreId sema;
  
  if (semaphore_def->controlblock != NULL){
    if (count == 1) {
      return xSemaphoreCreateBinaryStatic( semaphore_def->controlblock );
    }
    else {
#if (configUSE_COUNTING_SEMAPHORES == 1 )
      return xSemaphoreCreateCountingStatic( count, count, semaphore_def->controlblock );
#else
      return NULL;
#endif
    }
  }
  else {
    if (count == 1) {
      vSemaphoreCreateBinary(sema);
      return sema;
    }
    else {
#if (configUSE_COUNTING_SEMAPHORES == 1 )	
      return xSemaphoreCreateCounting(count, count);
#else
      return NULL;
#endif    
    }
  }
#elif ( configSUPPORT_STATIC_ALLOCATION == 1 ) // configSUPPORT_DYNAMIC_ALLOCATION == 0
  if(count == 1) {
    return xSemaphoreCreateBinaryStatic( semaphore_def->controlblock );
  }
  else
  {
#if (configUSE_COUNTING_SEMAPHORES == 1 )
      return xSemaphoreCreateCountingStatic( count, count, semaphore_def->controlblock );
#else
      return NULL;
#endif    
  }
#else  // configSUPPORT_STATIC_ALLOCATION == 0  && configSUPPORT_DYNAMIC_ALLOCATION == 1
  osSemaphoreId sema;
 
  if (count == 1) {
    vSemaphoreCreateBinary(sema);
    return sema;
  }
  else {
#if (configUSE_COUNTING_SEMAPHORES == 1 )	
    return xSemaphoreCreateCounting(count, count);
#else
    return NULL;
#endif
  }
#endif
}

/**
* @brief Wait until a Semaphore token becomes available
* @param  semaphore_id  semaphore object referenced with \ref osSemaphore.
* @param  millisec      timeout value or 0 in case of no time-out.
* @retval  number of available tokens, or -1 in case of incorrect parameters.
* @note   MUST REMAIN UNCHANGED: \b osSemaphoreWait shall be consistent in every CMSIS-RTOS.
*/
int32_t osSemaphoreWait (osSemaphoreId semaphore_id, uint32_t millisec)
{
  TickType_t ticks;
  portBASE_TYPE taskWoken = pdFALSE;  
  
  
  if (semaphore_id == NULL) {
    return osErrorParameter;
  }
  
  ticks = 0;
  if (millisec == osWaitForever) {
    ticks = portMAX_DELAY;
  }
  else if (millisec != 0) {
    ticks = millisec / portTICK_PERIOD_MS;
    if (ticks == 0) {
      ticks = 1;
    }
  }
  
  if (inHandlerMode()) {
    if (xSemaphoreTakeFromISR(semaphore_id, &taskWoken) != pdTRUE) {
      return osErrorOS;
    }
	portEND_SWITCHING_ISR(taskWoken);
  }  
  else if (xSemaphoreTake(semaphore_id, ticks) != pdTRUE) {
    return osErrorOS;
  }
  
  return osOK;
}

/**
* @brief Release a Semaphore token
* @param  semaphore_id  semaphore object referenced with \ref osSemaphore.
* @retval  status code that indicates the execution status of the function.
* @note   MUST REMAIN UNCHANGED: \b osSemaphoreRelease shall be consistent in every CMSIS-RTOS.
*/
osStatus osSemaphoreRelease (osSemaphoreId semaphore_id)
{
  osStatus result = osOK;
  portBASE_TYPE taskWoken = pdFALSE;
  
  
  if (inHandlerMode()) {
    if (xSemaphoreGiveFromISR(semaphore_id, &taskWoken) != pdTRUE) {
      return osErrorOS;
    }
    portEND_SWITCHING_ISR(taskWoken);
  }
  else {
    if (xSemaphoreGive(semaphore_id) != pdTRUE) {
      result = osErrorOS;
    }
  }
  
  return result;
}

/**
* @brief Delete a Semaphore
* @param  semaphore_id  semaphore object referenced with \ref osSemaphore.
* @retval  status code that indicates the execution status of the function.
* @note   MUST REMAIN UNCHANGED: \b osSemaphoreDelete shall be consistent in every CMSIS-RTOS.
*/
osStatus osSemaphoreDelete (osSemaphoreId semaphore_id)
{
  if (inHandlerMode()) {
    return osErrorISR;
  }

  vSemaphoreDelete(semaphore_id);

  return osOK; 
}

#endif    /* Use Semaphores */




/*******************   Message Queue Management Functions  *********************/

#if (defined (osFeature_MessageQ)  &&  (osFeature_MessageQ != 0)) /* Use Message Queues */

/**
* @brief Create and Initialize a Message Queue
* @param queue_def     queue definition referenced with \ref osMessageQ.
* @param  thread_id     thread ID (obtained by \ref osThreadCreate or \ref osThreadGetId) or NULL.
* @retval  message queue ID for reference by other functions or NULL in case of error.
* @note   MUST REMAIN UNCHANGED: \b osMessageCreate shall be consistent in every CMSIS-RTOS.
*/
osMessageQId osMessageCreate (const osMessageQDef_t *queue_def, osThreadId thread_id)
{
  (void) thread_id;
  
#if( configSUPPORT_STATIC_ALLOCATION == 1 ) && ( configSUPPORT_DYNAMIC_ALLOCATION == 1 )

  if ((queue_def->buffer != NULL) && (queue_def->controlblock != NULL)) {
    return xQueueCreateStatic(queue_def->queue_sz, queue_def->item_sz, queue_def->buffer, queue_def->controlblock);
  }
  else {
    return xQueueCreate(queue_def->queue_sz, queue_def->item_sz);
  }
#elif ( configSUPPORT_STATIC_ALLOCATION == 1 )
  return xQueueCreateStatic(queue_def->queue_sz, queue_def->item_sz, queue_def->buffer, queue_def->controlblock);
#else  
  return xQueueCreate(queue_def->queue_sz, queue_def->item_sz);
#endif
}

/**
* @brief Put a Message to a Queue.
* @param  queue_id  message queue ID obtained with \ref osMessageCreate.
* @param  info      message information.
* @param  millisec  timeout value or 0 in case of no time-out.
* @retval status code that indicates the execution status of the function.
* @note   MUST REMAIN UNCHANGED: \b osMessagePut shall be consistent in every CMSIS-RTOS.
*/
osStatus osMessagePut (osMessageQId queue_id, uint32_t info, uint32_t millisec)
{
  portBASE_TYPE taskWoken = pdFALSE;
  TickType_t ticks;
  
  ticks = millisec / portTICK_PERIOD_MS;
  if (ticks == 0) {
    ticks = 1;
  }
  
  if (inHandlerMode()) {
    if (xQueueSendFromISR(queue_id, &info, &taskWoken) != pdTRUE) {
      return osErrorOS;
    }
    portEND_SWITCHING_ISR(taskWoken);
  }
  else {
    if (xQueueSend(queue_id, &info, ticks) != pdTRUE) {
      return osErrorOS;
    }
  }
  
  return osOK;
}

/**
* @brief Get a Message or Wait for a Message from a Queue.
* @param  queue_id  message queue ID obtained with \ref osMessageCreate.
* @param  millisec  timeout value or 0 in case of no time-out.
* @retval event information that includes status code.
* @note   MUST REMAIN UNCHANGED: \b osMessageGet shall be consistent in every CMSIS-RTOS.
*/
osEvent osMessageGet (osMessageQId queue_id, uint32_t millisec)
{
  portBASE_TYPE taskWoken;
  TickType_t ticks;
  osEvent event;
  
  event.def.message_id = queue_id;
  event.value.v = 0;
  
  if (queue_id == NULL) {
    event.status = osErrorParameter;
    return event;
  }
  
  taskWoken = pdFALSE;
  
  ticks = 0;
  if (millisec == osWaitForever) {
    ticks = portMAX_DELAY;
  }
  else if (millisec != 0) {
    ticks = millisec / portTICK_PERIOD_MS;
    if (ticks == 0) {
      ticks = 1;
    }
  }
  
  if (inHandlerMode()) {
    if (xQueueReceiveFromISR(queue_id, &event.value.v, &taskWoken) == pdTRUE) {
      /* We have mail */
      event.status = osEventMessage;
    }
    else {
      event.status = osOK;
    }
    portEND_SWITCHING_ISR(taskWoken);
  }
  else {
    if (xQueueReceive(queue_id, &event.value.v, ticks) == pdTRUE) {
      /* We have mail */
      event.status = osEventMessage;
    }
    else {
      event.status = (ticks == 0) ? osOK : osEventTimeout;
    }
  }
  
  return event;
}

/**
* @brief  Receive an item from a queue without removing the item from the queue.
* @param  queue_id  message queue ID obtained with \ref osMessageCreate.
* @param  millisec  timeout value or 0 in case of no time-out.
* @retval event information that includes status code.
*/
osEvent osMessagePeek (osMessageQId queue_id, uint32_t millisec)
{
  TickType_t ticks;
  osEvent event;
  
  event.def.message_id = queue_id;
  
  if (queue_id == NULL) {
    event.status = osErrorParameter;
    return event;
  }
  
  ticks = 0;
  if (millisec == osWaitForever) {
    ticks = portMAX_DELAY;
  }
  else if (millisec != 0) {
    ticks = millisec / portTICK_PERIOD_MS;
    if (ticks == 0) {
      ticks = 1;
    }
  }
  
  if (xQueuePeek(queue_id, &event.value.v, ticks) == pdTRUE) 
  {
    /* We have mail */
    event.status = osEventMessage;
  }
  else 
  {
    event.status = (ticks == 0) ? osOK : osEventTimeout;
  }
  
  return event;
}

/**
* @brief  Get the number of messaged stored in a queue.
* @param  queue_id  message queue ID obtained with \ref osMessageCreate.
* @retval number of messages stored in a queue.
*/
uint32_t osMessageWaiting(osMessageQId queue_id)
{
  if (inHandlerMode()) {
    return uxQueueMessagesWaitingFromISR(queue_id);
  }
  else
  {
    return uxQueueMessagesWaiting(queue_id);
  }
}

/**
* @brief Delete a Message Queue
* @param  queue_id  message queue ID obtained with \ref osMessageCreate.
* @retval  status code that indicates the execution status of the function.
*/
osStatus osMessageDelete (osMessageQId queue_id)
{
  if (inHandlerMode()) {
    return osErrorISR;
  }

  vQueueDelete(queue_id);

  return osOK; 
}

#endif     /* Use Message Queues */
