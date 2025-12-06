/**
 ******************************************************************************
 * @file           : print_task.c
 * @brief          : Dedicated Print Task Implementation
 ******************************************************************************
 * @description
 * This module implements a dedicated print task that owns UART TX hardware
 * exclusively. Other tasks send messages via a queue, making UART printing
 * non-blocking and eliminating the need for mutex protection.
 *
 * Key Features:
 * - Exclusive UART TX ownership (no concurrent access issues)
 * - Non-blocking API for application tasks
 * - Queue-based message passing
 * - FIFO message ordering
 * - Simple error handling (queue full detection)
 *
 * Architecture Benefits:
 * - Eliminates priority inversion (queue is faster than mutex)
 * - Better separation of concerns (tasks don't need UART knowledge)
 * - Centralized UART control (easier to debug and extend)
 * - Scalable (can add features without changing application code)
 *
 * Memory Usage:
 * - Queue: ~5.1 KB (10 messages × 512 bytes)
 * - Task stack: ~2 KB (512 words)
 * - Total: ~7.1 KB (well within available 75 KB heap)
 *
 * Performance:
 * - Message enqueue: ~20-50μs
 * - Context switch overhead: ~30-50μs
 * - Total added latency: ~50-100μs (imperceptible for human-readable text)
 ******************************************************************************
 */

#include "print_task.h"
#include "watchdog.h"
#include <string.h>
#include <stdio.h>

/* FreeRTOS Objects */
QueueHandle_t print_queue = NULL;        // Message queue for print requests
extern UART_HandleTypeDef huart2;        // UART2 peripheral handle (from main.c)

/**
 * @brief  Initialize print task and message queue
 * @note   Must be called BEFORE starting the scheduler
 * @retval None
 *
 * Creates:
 * 1. Print Queue - Message queue for passing strings to print task
 *    Size: PRINT_QUEUE_DEPTH × PRINT_MESSAGE_MAX_SIZE bytes
 *    Purpose: Decouples application tasks from UART transmission
 *
 * 2. Print Task - Dedicated task for UART TX operations
 *    Priority: PRINT_TASK_PRIORITY (1) - lower than application tasks
 *    Stack: PRINT_TASK_STACK_SIZE (512 words)
 *    Purpose: Exclusive owner of UART TX hardware
 */
void print_task_init(void)
{
    // Create message queue for print requests
    // Queue holds complete messages (copied, not referenced)
    print_queue = xQueueCreate(PRINT_QUEUE_DEPTH, PRINT_MESSAGE_MAX_SIZE);
    configASSERT(print_queue != NULL);

    // Create print task
    // Priority 1: Lower than UART/Command tasks (printing not time-critical)
    BaseType_t status = xTaskCreate(print_task_handler,
                                    "Print_Task",
                                    PRINT_TASK_STACK_SIZE,
                                    NULL,
                                    PRINT_TASK_PRIORITY,
                                    NULL);
    configASSERT(status == pdPASS);
}

/**
 * @brief  Send a message to the print queue
 * @param  message: Null-terminated string to print
 * @retval BaseType_t: pdPASS if queued successfully, pdFAIL if timeout
 *
 * Behavior:
 * - Copies message into queue (safe to pass stack/local buffers)
 * - Blocks up to PRINT_ENQUEUE_TIMEOUT_MS if queue full
 * - Returns immediately after enqueuing (non-blocking for caller)
 * - Print task will transmit when scheduled
 *
 * Error Handling:
 * - If message exceeds PRINT_MESSAGE_MAX_SIZE, it will be truncated
 * - If queue full after timeout, returns pdFAIL (message dropped)
 *
 * Thread Safety: Safe to call from any task
 */
BaseType_t print_message(const char *message)
{
    char buffer[PRINT_MESSAGE_MAX_SIZE];

    // Validate input
    if (message == NULL) {
        return pdFAIL;
    }

    // Copy message to local buffer with size limit
    // strncpy ensures we don't overflow the buffer
    strncpy(buffer, message, PRINT_MESSAGE_MAX_SIZE - 1);
    buffer[PRINT_MESSAGE_MAX_SIZE - 1] = '\0';  // Ensure null termination

    // Send to queue (copies buffer into queue storage)
    // Timeout prevents deadlock if queue unexpectedly fills
    BaseType_t result = xQueueSend(print_queue, buffer, pdMS_TO_TICKS(PRINT_ENQUEUE_TIMEOUT_MS));

    return result;
}

/**
 * @brief  Send a single character to the print queue
 * @param  c: Character to print
 * @retval BaseType_t: pdPASS if queued successfully, pdFAIL if timeout
 *
 * Behavior:
 * - Optimized for single character printing (echo use case)
 * - Creates minimal message: [character, null terminator]
 * - Returns immediately after enqueuing
 *
 * Use Case:
 * - Character echo in UART reception task
 * - Single character feedback/indicators
 */
BaseType_t print_char(char c)
{
    char buffer[2];

    // Create 2-byte message: character + null terminator
    buffer[0] = c;
    buffer[1] = '\0';

    // Send to queue
    BaseType_t result = xQueueSend(print_queue, buffer, pdMS_TO_TICKS(PRINT_ENQUEUE_TIMEOUT_MS));

    return result;
}

/**
 * @brief  Print task main loop - processes messages from queue
 * @param  parameters: Task parameters (unused)
 * @retval None (task never returns)
 *
 * Task Behavior:
 * 1. Block waiting for message in queue (portMAX_DELAY = infinite wait)
 * 2. When message arrives:
 *    - Dequeue message into local buffer
 *    - Transmit via HAL_UART_Transmit()
 *    - Loop back to wait for next message
 *
 * Features:
 * - FIFO message ordering (queue guarantees)
 * - Processes all queued messages before blocking
 * - Yields to higher priority tasks between messages
 *
 * UART Access:
 * - This task has EXCLUSIVE access to UART TX
 * - No other task should call HAL_UART_Transmit() directly
 * - No mutex needed (single owner of hardware resource)
 *
 * Error Handling:
 * - If HAL_UART_Transmit() fails, message is discarded
 * - Could add error logging or LED indication in future
 *
 * Performance Notes:
 * - Priority 1 ensures higher priority tasks (UART RX, Command Handler)
 *   can preempt this task
 * - UART transmission time: ~87μs per character @ 115200 baud
 * - Longest message (menu): ~400 chars = ~35ms transmission time
 * - During transmission, higher priority tasks can still run
 */
void print_task_handler(void *parameters)
{
    char message_buffer[PRINT_MESSAGE_MAX_SIZE];

    // Register with watchdog (5 second timeout = 2.5× the 2s blocking period)
    watchdog_id_t wd_id = watchdog_register("Print_Task", 5000);
    if (wd_id == WATCHDOG_INVALID_ID) {
        // Can't use print_message here (would cause recursion), so silently fail
        // Watchdog will still monitor other tasks
    }

    while (1) {
        /*
         * Main Print Loop
         * ---------------
         * Block waiting for messages in the queue. When a message arrives,
         * dequeue it and transmit via UART. This task owns UART TX exclusively.
         *
         * Flow:
         * 1. Block on queue (task sleeps, no CPU usage)
         * 2. Message available -> task wakes up
         * 3. Dequeue message into local buffer
         * 4. Transmit message via UART
         * 5. Return to step 1
         */

        // Block waiting for message with finite timeout
        // Timeout allows periodic watchdog feeding even when no print activity
        if (xQueueReceive(print_queue, message_buffer, pdMS_TO_TICKS(2000)) == pdPASS) {
            // Message received - transmit it
            // HAL_MAX_DELAY: Wait indefinitely for UART to be ready
            // This is safe because we're the only task using UART TX
            HAL_UART_Transmit(&huart2,
                            (uint8_t *)message_buffer,
                            strlen(message_buffer),
                            HAL_MAX_DELAY);
        }

        // Feed watchdog to prove task is alive
        // Fed on every iteration (whether message received or timeout)
        if (wd_id != WATCHDOG_INVALID_ID) {
            watchdog_feed(wd_id);
        }

        /*
         * Loop continues regardless of receive status.
         * Watchdog is fed on every iteration to prove task is alive.
         */
    }
}
