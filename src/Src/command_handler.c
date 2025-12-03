/**
 ******************************************************************************
 * @file           : command_handler.c
 * @brief          : Command Processing and Menu State Machine
 ******************************************************************************
 * @description
 * This module implements a hierarchical menu system with command processing.
 * It uses a state machine to navigate between menus and execute user commands.
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
 * Command Processing:
 * - All commands are trimmed and converted to lowercase
 * - Invalid commands display error and redisplay current menu
 * - Valid commands execute action and print confirmation
 * - Menu redisplays after every command (except return to main)
 *
 * Thread Safety:
 * - All UART transmissions use print_message() via print task
 * - Menu state is only modified by command handler task (no protection needed)
 ******************************************************************************
 */

#include "command_handler.h"
#include "uart_task.h"
#include "led_effects.h"
#include "print_task.h"
#include <string.h>
#include <stdio.h>
#include <ctype.h>

/* Current menu state (state machine variable) */
static MenuState_t current_menu_state = MENU_MAIN;

static void to_lowercase(char *str)
{
    for (int i = 0; str[i]; i++) {
        str[i] = tolower((unsigned char)str[i]);
    }
}

static void trim_whitespace(char *str)
{
    char *end;

    // Trim leading space
    while (isspace((unsigned char)*str)) str++;

    if (*str == 0) return;

    // Trim trailing space
    end = str + strlen(str) - 1;
    while (end > str && isspace((unsigned char)*end)) end--;

    // Write new null terminator
    *(end + 1) = '\0';
}

MenuState_t get_menu_state(void)
{
    return current_menu_state;
}

static void print_led_patterns_menu(void)
{
    const char *menu =
        "\r\n========================================\r\n"
        "        LED Pattern Selection\r\n"
        "========================================\r\n"
        "  0 - Return to main menu\r\n"
        "  1 - All LEDs ON\r\n"
        "  2 - Different Frequency Blinking\r\n"
        "  3 - Same Frequency Blinking\r\n"
        "  4 - All LEDs OFF\r\n"
        "========================================\r\n"
        "Enter selection: ";

    print_message(menu);
}

static void process_main_menu_command(char *command)
{
    char response[128];

    if (strcmp(command, "1") == 0) {
        // Enter LED patterns menu
        current_menu_state = MENU_LED_PATTERNS;
        print_led_patterns_menu();
    }
    else if (strcmp(command, "2") == 0) {
        // Exit application - stop all LED patterns
        led_effects_set_pattern(LED_PATTERN_NONE);
        snprintf(response, sizeof(response), "\r\nApplication exited. All LEDs turned OFF.\r\n");
        print_message(response);
        print_main_menu();
    }
    else {
        snprintf(response, sizeof(response), "\r\nInvalid option. Please try again.\r\n");
        print_message(response);
        print_main_menu();
    }
}

static void process_led_patterns_menu_command(char *command)
{
    char response[128];

    if (strcmp(command, "0") == 0) {
        // Return to main menu
        current_menu_state = MENU_MAIN;
        print_main_menu();
    }
    else if (strcmp(command, "1") == 0) {
        led_effects_set_pattern(LED_PATTERN_1);
        snprintf(response, sizeof(response), "\r\nNow playing LED Pattern 1\r\n");
        print_message(response);
        print_led_patterns_menu();
    }
    else if (strcmp(command, "2") == 0) {
        led_effects_set_pattern(LED_PATTERN_2);
        snprintf(response, sizeof(response), "\r\nNow playing LED Pattern 2\r\n");
        print_message(response);
        print_led_patterns_menu();
    }
    else if (strcmp(command, "3") == 0) {
        led_effects_set_pattern(LED_PATTERN_3);
        snprintf(response, sizeof(response), "\r\nNow playing LED Pattern 3\r\n");
        print_message(response);
        print_led_patterns_menu();
    }
    else if (strcmp(command, "4") == 0) {
        led_effects_set_pattern(LED_PATTERN_NONE);
        snprintf(response, sizeof(response), "\r\nAll LEDs turned OFF\r\n");
        print_message(response);
        print_led_patterns_menu();
    }
    else {
        snprintf(response, sizeof(response), "\r\nInvalid option. Please try again.\r\n");
        print_message(response);
        print_led_patterns_menu();
    }
}

void process_command(char *command)
{
    // Trim and convert to lowercase
    trim_whitespace(command);
    to_lowercase(command);

    // Process based on current menu state
    switch (current_menu_state) {
        case MENU_MAIN:
            process_main_menu_command(command);
            break;

        case MENU_LED_PATTERNS:
            process_led_patterns_menu_command(command);
            break;

        default:
            current_menu_state = MENU_MAIN;
            print_main_menu();
            break;
    }
}

void command_handler_task(void *parameters)
{
    char received_command[COMMAND_MAX_LENGTH];

    while (1) {
        // Wait for notification from UART task
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        // Try to receive command from queue
        while (xQueueReceive(command_queue, received_command, 0) == pdPASS) {
            // Process the command
            process_command(received_command);
        }
    }
}
