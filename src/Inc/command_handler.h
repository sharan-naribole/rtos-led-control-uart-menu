/**
 ******************************************************************************
 * @file           : command_handler.h
 * @brief          : Command Processing and Menu State Machine Interface
 ******************************************************************************
 * @description
 * This header defines the interface for the command processing task and the
 * hierarchical menu system. The command handler implements a state machine
 * that navigates between menus and executes user commands.
 *
 * Menu Structure:
 * ┌─────────────┐
 * │  Main Menu  │  (1: LED Patterns, 2: Exit)
 * └─────────────┘
 *        │
 *        ├─ Option 1 ──> ┌────────────────────┐
 *        │                │ LED Patterns Menu  │
 *        │                │ 0: Return to main  │
 *        │                │ 1: All LEDs ON     │
 *        │                │ 2: Diff Freq Blink │
 *        │                │ 3: Same Freq Blink │
 *        │                │ 4: All LEDs OFF    │
 *        │                └────────────────────┘
 *        │
 *        └─ Option 2 ──> Stop LEDs & stay in main menu
 *
 * Command Processing Flow:
 * 1. UART task receives complete command → sends to queue
 * 2. UART task notifies command handler task
 * 3. Command handler wakes up and processes command
 * 4. Handler executes action based on current menu state
 * 5. Handler prints response and appropriate menu
 *
 * State Management:
 * - States: MENU_MAIN, MENU_LED_PATTERNS
 * - State transitions controlled by user commands
 * - Invalid commands keep current state and show error
 *
 * Thread Safety:
 * - Menu state only accessed by command handler task (no protection needed)
 * - All UART transmissions protected by uart_mutex
 ******************************************************************************
 */

#ifndef __COMMAND_HANDLER_H
#define __COMMAND_HANDLER_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"
#include "FreeRTOS.h"
#include "task.h"

/*============================================================================
 * Type Definitions
 *===========================================================================*/

/**
 * @brief  Menu state enumeration for state machine
 *
 * The application uses a hierarchical menu system with the following states:
 *
 * MENU_MAIN:
 *   Top-level menu with application-wide options
 *   Options:
 *     1 - LED Patterns (transition to MENU_LED_PATTERNS)
 *     2 - Exit Application (stop all LEDs, stay in MENU_MAIN)
 *
 * MENU_LED_PATTERNS:
 *   Submenu for LED pattern selection
 *   Options:
 *     0 - Return to main menu (transition to MENU_MAIN)
 *     1 - All LEDs ON (static pattern)
 *     2 - Different Frequency Blinking (green 100ms, orange 1000ms)
 *     3 - Same Frequency Blinking (both 100ms)
 *     4 - All LEDs OFF (pattern stopped)
 *
 * State transitions are triggered by user commands and controlled by
 * process_command() function.
 */
typedef enum {
    MENU_MAIN = 0,          /**< Main menu state */
    MENU_LED_PATTERNS       /**< LED patterns submenu state */
} MenuState_t;

/*============================================================================
 * Public Function Interfaces
 *===========================================================================*/

/**
 * @brief  Command handler task (main task loop)
 * @param  parameters: Task parameters (unused, required by FreeRTOS API)
 * @retval None (task never returns)
 *
 * Task Behavior:
 * 1. Waits for task notification from UART task (blocks when idle)
 * 2. When notified, receives all pending commands from queue
 * 3. Processes each command using process_command()
 * 4. Returns to waiting state when queue empty
 *
 * Synchronization:
 * - Woken by ulTaskNotifyTake() when UART task sends command
 * - Receives commands from command_queue (non-blocking receive)
 * - Uses uart_mutex for all UART transmissions
 *
 * Priority: 2 (same as UART task for balanced scheduling)
 * Stack: 256 words (sufficient for menu printing)
 *
 * @note Task handle stored in uart_task.c for notification from UART task
 */
void command_handler_task(void *parameters);

/**
 * @brief  Process a command based on current menu state
 * @param  command: Null-terminated command string to process
 * @retval None
 *
 * Command Processing Steps:
 * 1. Trim leading/trailing whitespace
 * 2. Convert to lowercase for case-insensitive matching
 * 3. Dispatch to appropriate handler based on current_menu_state
 * 4. Execute action (LED pattern change, menu transition, etc.)
 * 5. Print response message
 * 6. Redisplay appropriate menu
 *
 * Error Handling:
 * - Invalid commands display error message and redisplay current menu
 * - Menu state remains unchanged on invalid input
 * - Buffer overflow already handled by UART task
 *
 * @note All UART transmissions are mutex-protected
 * @note Function is called only from command_handler_task
 */
void process_command(char *command);

/**
 * @brief  Get current menu state
 * @retval Current menu state (MENU_MAIN or MENU_LED_PATTERNS)
 *
 * Returns the current state of the menu state machine.
 * Can be used for debugging or conditional logic in other modules.
 *
 * @note Thread-safe: state only modified by command handler task
 */
MenuState_t get_menu_state(void);

#ifdef __cplusplus
}
#endif

#endif /* __COMMAND_HANDLER_H */
