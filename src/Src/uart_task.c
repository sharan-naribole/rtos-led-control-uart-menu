/**
 ******************************************************************************
 * @file           : uart_task.c
 * @brief          : UART Communication Task Implementation
 ******************************************************************************
 * @description
 * This module implements a character-by-character UART reception task with
 * command buffering and processing. It uses FreeRTOS primitives for
 * inter-task communication and synchronization.
 *
 * Key Features:
 * - Non-blocking character reception with echo
 * - Command buffering with overflow protection
 * - Backspace handling
 * - Queue-based command passing to handler task
 * - All UART TX operations delegated to print task
 *
 * Synchronization Strategy:
 * - command_queue: Decouples reception from command processing
 * - Task notification: Wakes up command handler when command ready
 * - print_message()/print_char(): Thread-safe UART TX via print task
 *
 * Thread Safety:
 * - UART TX: All output goes through print task (no direct HAL calls)
 * - Buffer access: Single task only (no protection needed)
 *
 * @note UART RX is handled by this task, UART TX is handled by print task.
 *       This separation eliminates the need for mutex protection.
 ******************************************************************************
 */

#include "uart_task.h"
#include "command_handler.h"
#include "print_task.h"
#include <string.h>
#include <stdio.h>

/* FreeRTOS Objects */
QueueHandle_t command_queue = NULL;           // Command queue (UART -> Handler)
extern UART_HandleTypeDef huart2;             // UART2 peripheral handle

/* Reception State */
static char rx_buffer[UART_RX_BUFFER_SIZE];   // Command assembly buffer
static uint16_t rx_index = 0;                 // Current position in buffer
static TaskHandle_t command_handler_task_handle = NULL;

/**
 * @brief  Initialize UART subsystem and create FreeRTOS objects
 * @note   Must be called BEFORE starting the scheduler
 * @retval None
 *
 * Creates:
 * 1. Command Queue - Holds up to 5 commands (32 chars each)
 *    Flow: UART task -> Queue -> Command handler task
 *    Prevents: Blocking UART reception during command processing
 *
 * 2. Command Handler Task - Processes commands from queue
 *    Priority: 2 (same as UART task for balanced scheduling)
 *    Stack: 256 words (sufficient for menu printing)
 *
 * Note: Print task is initialized separately via print_task_init()
 */
void uart_task_init(void)
{
    // Create command queue: 5 slots × 32 chars
    // Size chosen to buffer rapid commands without blocking
    command_queue = xQueueCreate(5, COMMAND_MAX_LENGTH * sizeof(char));
    configASSERT(command_queue != NULL);

    // Create command handler task
    // Priority 2: Same as UART task for fair scheduling
    BaseType_t status = xTaskCreate(command_handler_task,
                                    "CMD_Handler",
                                    256,           // Stack size in words
                                    NULL,          // No parameters
                                    2,             // Priority
                                    &command_handler_task_handle);
    configASSERT(status == pdPASS);
}

static void print_welcome_message(void)
{
    const char *welcome =
        "\r\n\r\n"
        "****************************************\r\n"
        "*                                      *\r\n"
        "*   LED Pattern Control Application   *\r\n"
        "*        FreeRTOS UART Interface       *\r\n"
        "*                                      *\r\n"
        "****************************************\r\n";

    print_message(welcome);
}

void print_main_menu(void)
{
    const char *menu =
        "\r\n========================================\r\n"
        "              MAIN MENU\r\n"
        "========================================\r\n"
        "  1 - LED Patterns\r\n"
        "  2 - Exit Application\r\n"
        "========================================\r\n"
        "Enter selection: ";

    print_message(menu);
}

/**
 * @brief  UART Reception Task - Main task loop
 * @param  parameters: Task parameters (unused)
 * @retval None (task never returns)
 *
 * This task performs character-by-character reception with the following features:
 *
 * Startup Sequence:
 * 1. Clear RX buffer to prevent boot-time garbage
 * 2. Delay 100ms for UART stabilization
 * 3. Print welcome message and main menu
 *
 * Main Loop Operations:
 * - Receive one character (blocking, no timeout)
 * - Echo character back to terminal
 * - Handle special characters:
 *   * CR/LF: Process complete command
 *   * Backspace: Delete last character
 *   * Normal: Add to buffer
 *
 * Command Processing Flow:
 * 1. User types command + Enter
 * 2. Command added to queue
 * 3. Command handler task notified
 * 4. Handler prints response and appropriate menu
 *
 * @note Character echo is NOT mutex-protected because it's a single-byte
 *       transmission within the same task - no risk of corruption
 */
void uart_task_handler(void *parameters)
{
    uint8_t received_char;
    uint8_t dummy;

    // Clear RX buffer to eliminate boot-time garbage characters
    // This prevents "Buffer overflow!" message on first power-up
    memset(rx_buffer, 0, UART_RX_BUFFER_SIZE);
    rx_index = 0;

    // Wait for UART peripheral to stabilize after initialization
    vTaskDelay(pdMS_TO_TICKS(100));

    // Flush any garbage from UART RX buffer (from power-on glitches)
    // Reading DR also clears error flags (ORE, NE, FE, PE) on STM32F4
    while (__HAL_UART_GET_FLAG(&huart2, UART_FLAG_RXNE)) {
        dummy = (uint8_t)(huart2.Instance->DR & 0xFF);
        (void)dummy;  // Suppress unused variable warning
    }

    // Print initial UI
    print_welcome_message();
    print_main_menu();

    while (1) {
        /*
         * Main Reception Loop
         * ------------------
         * This loop processes characters one-by-one with immediate echo.
         * The blocking receive ensures the task sleeps when no data available.
         *
         * Flow:
         * 1. Block waiting for character (UART RX interrupt wakes us)
         * 2. Echo character to terminal (user feedback)
         * 3. Process character:
         *    - CR/LF: Complete command -> send to queue -> notify handler
         *    - Backspace: Remove last character from buffer
         *    - Normal: Add to buffer (with overflow check)
         */

        // Receive one character (blocks until available)
        if (HAL_UART_Receive(&huart2, &received_char, 1, portMAX_DELAY) == HAL_OK) {
            /*
             * Case 1: Command Complete (CR or LF)
             * User pressed Enter - process the complete command
             */
            if (received_char == '\n' || received_char == '\r') {
                if (rx_index > 0) {
                    // Null-terminate the string for safe string operations
                    rx_buffer[rx_index] = '\0';

                    // Send command to queue (100ms timeout to prevent deadlock)
                    // Queue depth is 5, so this should rarely block
                    if (xQueueSend(command_queue, rx_buffer, pdMS_TO_TICKS(100)) == pdPASS) {
                        // Wake up command handler task to process the command
                        // Handler will print response and redisplay appropriate menu
                        xTaskNotifyGive(command_handler_task_handle);
                    } else {
                        // Queue full - unlikely but handle gracefully
                        print_message("\r\nError: Command queue full!\r\n");
                    }

                    // Reset buffer for next command
                    rx_index = 0;
                    memset(rx_buffer, 0, UART_RX_BUFFER_SIZE);

                    // Note: We DON'T print "Enter command:" here
                    // The command handler will print the appropriate menu after processing
                }
            }
            /*
             * Case 2: Backspace Character
             * User pressed backspace - remove last character from buffer
             */
            else if (received_char == '\b' || received_char == 127) {
                if (rx_index > 0) {
                    // Remove last character from buffer
                    rx_index--;
                    rx_buffer[rx_index] = '\0';

                    // Visual feedback: backspace sequence erases character on terminal
                    // Sequence: \b (move cursor left) + space (overwrite char) + \b (move back)
                    print_message("\b \b");
                }
            }
            /*
             * Case 3: Normal Character
             * Add to buffer if space available, otherwise report overflow
             */
            else {
                // Echo character through print task for exclusive UART TX ownership
                // Print task has priority 3 (higher) so processes immediately
                print_char(received_char);

                // Check buffer space (reserve 1 byte for null terminator)
                if (rx_index < (UART_RX_BUFFER_SIZE - 1)) {
                    // Add character to buffer
                    rx_buffer[rx_index++] = received_char;
                } else {
                    // Buffer full - discard character and reset buffer
                    // This can happen if user types long string without pressing Enter
                    print_message("\r\nError: Buffer overflow!\r\n");
                    // Reset buffer - user must retype command
                    rx_index = 0;
                    memset(rx_buffer, 0, UART_RX_BUFFER_SIZE);
                }
            }
        }
    }
}
