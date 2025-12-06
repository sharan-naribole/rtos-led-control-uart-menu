/**
 ******************************************************************************
 * @file           : uart_task.h
 * @brief          : UART Communication Task Interface
 ******************************************************************************
 * @description
 * This header defines the interface for the UART reception task and related
 * FreeRTOS synchronization objects. The UART task handles character-by-character
 * input with echo and command buffering.
 *
 * Architecture: Stream Buffer Mode (Efficient)
 * - Interrupt-driven UART reception (HAL_UART_Receive_IT)
 * - Stream buffer for ISR-to-Task communication
 * - TRUE task blocking (yields CPU when idle)
 * - Zero CPU waste (no polling)
 *
 * Key Components:
 * - UART Reception Task: Receives user input via stream buffer
 * - Command Queue: Passes complete commands to handler task
 * - Stream Buffer: Efficient ISR-to-Task byte transfer
 *
 * Configuration:
 * - UART2 peripheral: 115200 baud, 8N1, no flow control
 * - Command buffer: 32 characters max (configurable)
 * - RX buffer: 128 characters (internal buffering)
 * - Stream buffer: 128 bytes (ISR-to-Task FIFO)
 * - Command queue depth: 5 commands
 *
 * Thread Safety:
 * UART TX operations are handled exclusively by the print task.
 * Use print_message() or print_char() from print_task.h for all output.
 ******************************************************************************
 */

#ifndef __UART_TASK_H
#define __UART_TASK_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"
#include "stream_buffer.h"

/*============================================================================
 * Configuration Constants
 *===========================================================================*/

/**
 * @brief  Internal buffer size for UART reception
 * @note   This is the working buffer for assembling commands before they're
 *         sent to the command queue. Should be larger than COMMAND_MAX_LENGTH
 *         to allow for overflow detection.
 */
#define UART_RX_BUFFER_SIZE 128

/**
 * @brief  Stream buffer size for ISR-to-Task communication
 * @note   Used for efficient interrupt-driven UART reception. The ISR deposits
 *         received bytes into this buffer, and the task reads from it.
 *         Trigger level is set to 1 (task wakes on any byte).
 */
#define UART_STREAM_BUFFER_SIZE 128

/**
 * @brief  Maximum length of a single command
 * @note   This is the size of each command slot in the queue. Commands longer
 *         than this will trigger a buffer overflow error and be discarded.
 */
#define COMMAND_MAX_LENGTH 32

/*============================================================================
 * FreeRTOS Objects (Global Handles)
 *===========================================================================*/

/**
 * @brief  Command queue handle
 * @details Queue that passes complete commands from UART task to command
 *          handler task. Depth: 5 commands. Item size: COMMAND_MAX_LENGTH.
 *          Created in uart_task_init().
 */
extern QueueHandle_t command_queue;

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
 * @brief  Initialize UART subsystem and FreeRTOS objects
 * @note   MUST be called before starting the FreeRTOS scheduler
 * @retval None
 *
 * Creates:
 * - UART mutex (binary semaphore for thread-safe TX)
 * - Command queue (5 slots × 32 chars each)
 * - Command handler task (priority 2, stack 256 words)
 *
 * All creation operations use configASSERT() to detect failures.
 */
void uart_task_init(void);

/**
 * @brief  UART reception task handler (main task loop)
 * @param  parameters: Task parameters (unused, required by FreeRTOS API)
 * @retval None (task never returns)
 *
 * Task Behavior:
 * 1. Prints welcome message and main menu on startup
 * 2. Enters infinite loop receiving characters one-by-one
 * 3. Echoes each character back to terminal for user feedback
 * 4. Handles special characters (CR/LF, backspace)
 * 5. Sends complete commands to queue and notifies handler task
 *
 * Features:
 * - Character echo (immediate feedback)
 * - Backspace handling with visual terminal update
 * - Buffer overflow detection and recovery
 * - Thread-safe error message printing
 *
 * Priority: 2 (same as command handler for balanced scheduling)
 */
void uart_task_handler(void *parameters);

/**
 * @brief  Print the main menu to UART
 * @retval None
 *
 * Displays the top-level menu with application options.
 * Uses uart_mutex to ensure thread-safe transmission.
 *
 * Menu Options:
 * 1 - LED Patterns (enters LED patterns submenu)
 * 2 - Exit Application (stops all LED patterns)
 *
 * @note Can be called from any task - mutex-protected
 */
void print_main_menu(void);

#ifdef __cplusplus
}
#endif

#endif /* __UART_TASK_H */
