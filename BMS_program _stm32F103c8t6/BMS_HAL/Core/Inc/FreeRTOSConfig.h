/*
 * FreeRTOS V202212.01
 * Copyright (C) 2020 Amazon.com, Inc. or its affiliates.  All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 * the Software, and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * https://www.FreeRTOS.org
 * https://github.com/FreeRTOS
 *
 */

#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H

/*-----------------------------------------------------------
 * Application specific definitions.
 *
 * These definitions should be adjusted for your particular hardware and
 * application requirements.
 *
 * THESE PARAMETERS ARE DESCRIBED WITHIN THE 'CONFIGURATION' SECTION OF THE
 * FreeRTOS API DOCUMENTATION AVAILABLE ON THE FreeRTOS.org WEB SITE.
 *
 * See http://www.freertos.org/a00110.html
 *----------------------------------------------------------*/

/* ======================== 内核基本配置 ======================== */
#define configUSE_PREEMPTION            1       /* 1=抢占式调度, 0=协作式 */
#define configUSE_IDLE_HOOK             1       /* 空闲钩子: 喂IWDG + WFI省电 */
#define configUSE_TICK_HOOK             0       /* Tick钩子 */
#define configCPU_CLOCK_HZ              ( 72000000UL )   /* SYSCLK=72MHz */
#define configTICK_RATE_HZ              ( ( TickType_t ) 1000 )  /* 1ms/tick */
#define configMAX_PRIORITIES            ( 5 )   /* 优先级0~4 */
#define configMINIMAL_STACK_SIZE        ( ( unsigned short ) 128 ) /* Idle任务栈=128字(512B) */
#define configTOTAL_HEAP_SIZE           ( ( size_t ) ( 10 * 1024 ) ) /* FreeRTOS堆=10KB (RAM共20KB) */
#define configMAX_TASK_NAME_LEN         ( 16 )
#define configUSE_TRACE_FACILITY        0
#define configUSE_16_BIT_TICKS          0
#define configIDLE_SHOULD_YIELD         1

/* ======================== 同步原语 ======================== */
#define configUSE_MUTEXES               1       /* 启用互斥量 (保护I2C总线) */
#define configUSE_COUNTING_SEMAPHORES   0
#define configUSE_RECURSIVE_MUTEXES     0
#define configQUEUE_REGISTRY_SIZE       0

/* ======================== 内存管理 ======================== */
#define configSUPPORT_STATIC_ALLOCATION     0   /* 仅使用动态分配 */
#define configSUPPORT_DYNAMIC_ALLOCATION    1

/* ======================== 可选API ======================== */
#define INCLUDE_vTaskPrioritySet        1
#define INCLUDE_uxTaskPriorityGet       1
#define INCLUDE_vTaskDelete             1
#define INCLUDE_vTaskCleanUpResources   0
#define INCLUDE_vTaskSuspend            1
#define INCLUDE_vTaskDelayUntil         1
#define INCLUDE_vTaskDelay              1
#define INCLUDE_xTaskGetSchedulerState  1

/* ======================== Cortex-M3 中断优先级 ========================
 *  STM32F1使用4位优先级 (__NVIC_PRIO_BITS=4), 优先级范围0~15
 *  数值越小优先级越高
 *
 *  configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY = 5
 *    表示: 优先级5~15的ISR可安全调用FreeRTOS "FromISR"API
 *          优先级0~4的ISR不可调用FreeRTOS API (但响应更快)
 *
 *  本工程ISR优先级分配:
 *    TIM3 (HAL时基)  = 1  (不调FreeRTOS API, 可用高优先级)
 *    EXTI2 (BQ报警)  = 6  (需调FromISR通知, 需≥5)
 *    USART1          = 6  (同上)
 *    SysTick/PendSV  = 15 (内核最低, FreeRTOS管理)
 */
#define configLIBRARY_KERNEL_INTERRUPT_PRIORITY         15
#define configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY    5

#define configKERNEL_INTERRUPT_PRIORITY         ( configLIBRARY_KERNEL_INTERRUPT_PRIORITY << (8 - 4) )
#define configMAX_SYSCALL_INTERRUPT_PRIORITY    ( configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY << (8 - 4) )

/* ======================== 调试断言 ======================== */
#define configASSERT( x ) if( ( x ) == 0 ) { taskDISABLE_INTERRUPTS(); for( ;; ); }

/* ======================== 中断处理器映射 ========================
 *  将FreeRTOS内核的中断处理器名映射到STM32的标准ISR名
 *  这样FreeRTOS的port.c中的函数直接替代startup中的[WEAK]定义
 *  注意: stm32f1xx_it.c中必须注释掉对应的空函数体!
 */
#define xPortPendSVHandler  PendSV_Handler
#define vPortSVCHandler     SVC_Handler


#endif /* FREERTOS_CONFIG_H */

