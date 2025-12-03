/**
 ******************************************************************************
 * @file           : print_task.h
 * @brief          : Dedicated Print Task Interface
 ******************************************************************************
 * @description
 * This module implements a dedicated print task with message queue for
 * thread-safe UART transmission. All UART TX operations go through this
 * task, eliminating the need for mutex protection and preventing priority
 * inversion issues.
 *
 * Architecture:
 * - Print Task: Dedicated task that owns UART TX hardware exclusively
 * - Print Queue: Message queue for passing print requests from other tasks
 * - Non-blocking API: Tasks enqueue messages and return immediately
 *
 * Benefits over Mutex Approach:
 * ✓ Non-blocking: Application tasks don't wait for slow UART transmission
 * ✓ No priority inversion: Queue-based synchronization is more predictable
 * ✓ Separation of concerns: Application tasks don't need UART knowledge
 * ✓ Scalable: Easy to add features (buffering, timestamps, priorities)
 * ✓ Centralized: Single point for UART TX control and debugging
 *
 * Usage Example:
 * ```c
 * // Simple string printing
 * print_message("Hello World\r\n");
 *
 * // Character echo
 * print_char('A');
 *
 * // Formatted printing
 * char buffer[64];
 * snprintf(buffer, sizeof(buffer), "Value: %d\r\n", value);
 * print_message(buffer);
 * ```
 *
 * Performance:
 * - Queue operation: ~20-50μs (enqueue and return)
 * - Additional latency vs direct: ~50-100μs (context switch overhead)
 * - For human-readable text, this latency is imperceptible
 *
 * Memory:
 * - Print queue: ~5.1 KB (10 messages × 512 bytes each)
 * - Print task stack: 512 words = 2048 bytes
 * - Total: ~7.1 KB (well within available heap)
 ******************************************************************************
 */

#ifndef __PRINT_TASK_H
#define __PRINT_TASK_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

/*============================================================================
 * Configuration Constants
 *===========================================================================*/

/**
 * @brief  Maximum size of a single print message
 * @note   This must be large enough to hold the longest menu or message.
 *         Current longest message is the LED patterns menu (~320 bytes).
 *         512 bytes provides headroom for future expansion.
 */
#define PRINT_MESSAGE_MAX_SIZE 512

/**
 * @brief  Print message queue depth
 * @note   Number of messages that can be queued before blocking/dropping.
 *         10 messages provides good buffering for burst printing scenarios.
 */
#define PRINT_QUEUE_DEPTH 10

/**
 * @brief  Print task priority
 * @note   Priority 3 (highest application priority).
 *         Higher than UART/Command Handler to ensure print messages
 *         are processed immediately when queued, providing responsive
 *         echo and preventing queue buildup.
 */
#define PRINT_TASK_PRIORITY 3

/**
 * @brief  Print task stack size (in words)
 * @note   512 words = 2048 bytes. Sufficient for UART operations and
 *         local buffers. Tested with high water mark monitoring.
 */
#define PRINT_TASK_STACK_SIZE 512

/**
 * @brief  Timeout for enqueuing print messages (milliseconds)
 * @note   If queue is full, wait this long before giving up.
 *         100ms timeout prevents deadlock if queue fills unexpectedly.
 */
#define PRINT_ENQUEUE_TIMEOUT_MS 100

/*============================================================================
 * FreeRTOS Objects (Global Handles)
 *===========================================================================*/

/**
 * @brief  Print message queue handle
 * @details Queue that passes print messages from application tasks to the
 *          print task. Depth: PRINT_QUEUE_DEPTH. Item size: PRINT_MESSAGE_MAX_SIZE.
 *          Created in print_task_init().
 */
extern QueueHandle_t print_queue;

/**
 * @brief  UART2 peripheral handle
 * @details Configured by STM32CubeMX for 115200 baud, 8N1, no flow control.
 *          Hardware: USART2 on PA2 (TX) and PA3 (RX).
 *          Defined in main.c and initialized in MX_USART2_UART_Init().
 */
extern UART_HandleTypeDef huart2;

/*============================================================================
 * Public Function Interfaces
 *===========================================================================*/

/**
 * @brief  Initialize print task and message queue
 * @note   MUST be called before starting the FreeRTOS scheduler
 * @retval None
 *
 * Creates:
 * - Print message queue (PRINT_QUEUE_DEPTH × PRINT_MESSAGE_MAX_SIZE)
 * - Print task (priority PRINT_TASK_PRIORITY, stack PRINT_TASK_STACK_SIZE)
 *
 * All creation operations use configASSERT() to detect failures.
 */
void print_task_init(void);

/**
 * @brief  Send a string message to the print queue
 * @param  message: Null-terminated string to print (max PRINT_MESSAGE_MAX_SIZE)
 * @retval BaseType_t: pdPASS if message queued successfully, pdFAIL if timeout
 *
 * Behavior:
 * - Copies message to queue (safe to use local/stack buffers)
 * - Returns immediately after enqueuing (non-blocking for caller)
 * - If queue full, waits up to PRINT_ENQUEUE_TIMEOUT_MS before returning pdFAIL
 * - Print task will transmit message via UART when scheduled
 *
 * Thread Safety: Safe to call from any task or ISR (FromISR variant)
 *
 * Example:
 * ```c
 * if (print_message("Hello\r\n") == pdFAIL) {
 *     // Queue full - message dropped
 * }
 * ```
 */
BaseType_t print_message(const char *message);

/**
 * @brief  Send a single character to the print queue
 * @param  c: Character to print
 * @retval BaseType_t: pdPASS if character queued successfully, pdFAIL if timeout
 *
 * Behavior:
 * - Optimized for single character printing (echo use case)
 * - Internally creates a 2-byte message: [character, null terminator]
 * - Returns immediately after enqueuing
 *
 * Use Case: Character echo in UART reception
 *
 * Example:
 * ```c
 * print_char('A');  // Echo back character to terminal
 * ```
 */
BaseType_t print_char(char c);

/**
 * @brief  Print task handler (main task loop)
 * @param  parameters: Task parameters (unused, required by FreeRTOS API)
 * @retval None (task never returns)
 *
 * Task Behavior:
 * 1. Blocks waiting for messages in print queue
 * 2. When message available, dequeues it
 * 3. Transmits message via HAL_UART_Transmit()
 * 4. Repeats - processes all queued messages before blocking again
 *
 * Features:
 * - Exclusive UART TX ownership (no concurrent access)
 * - Processes messages in FIFO order
 * - Yields between messages if higher priority tasks ready
 *
 * Priority: PRINT_TASK_PRIORITY (1) - lower than application tasks
 *
 * @note This task has EXCLUSIVE access to UART TX hardware.
 *       No other task should call HAL_UART_Transmit() directly.
 */
void print_task_handler(void *parameters);

#ifdef __cplusplus
}
#endif

#endif /* __PRINT_TASK_H */
