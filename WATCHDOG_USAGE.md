# Watchdog Usage Guide

Simple task watchdog implementation for detecting hung/deadlocked tasks in FreeRTOS.

---

## What Was Added

### Files Created:
1. **`Core/Inc/watchdog.h`** - Watchdog API
2. **`Core/Src/watchdog.c`** - Watchdog implementation

### Projects Updated:
- ✅ **014LedPatternsUartInput**
- ✅ **015LedControlWifiServer**

### Changes Made:
1. Added `#include "watchdog.h"` to `main.c`
2. Added `watchdog_init()` before `vTaskStartScheduler()`
3. Watchdog task created (Priority 4, highest in system)

---

## How It Works

### Architecture

```
┌────────────────────────────────────────────────────────────┐
│  Task A                     Task B                Task C    │
│    │                          │                     │       │
│    │ watchdog_feed(id_a)      │ watchdog_feed(id_b) │       │
│    ↓                          ↓                     ↓       │
│  ┌──────────────────────────────────────────────────┐      │
│  │          Watchdog Monitor Task                   │      │
│  │  - Wakes every 1 second                          │      │
│  │  - Checks all registered tasks                   │      │
│  │  - Alerts if task hasn't fed within timeout      │      │
│  └──────────────────────────────────────────────────┘      │
│                                                             │
│  If Task C stops feeding → WATCHDOG ALERT!                 │
└────────────────────────────────────────────────────────────┘
```

---

## How to Use

### Step 1: Register Your Task

In your task function, register with the watchdog:

```c
void my_task_handler(void *parameters)
{
    // Register with watchdog (timeout = 5 seconds)
    watchdog_id_t my_watchdog_id = watchdog_register("MyTask", 5000);

    if (my_watchdog_id == WATCHDOG_INVALID_ID) {
        // Registration failed!
        while(1);  // Or handle error
    }

    // Task loop...
    while(1) {
        // Do your work

        // Feed watchdog BEFORE timeout expires
        watchdog_feed(my_watchdog_id);

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
```

---

### Step 2: Choose Appropriate Timeout

**Rule of thumb:** Timeout should be **2-3× your longest expected delay**

Examples:

| Task | Max Delay | Recommended Timeout |
|------|-----------|---------------------|
| UART Task | Waits for data indefinitely | Don't register* |
| Command Handler | Waits for queue | Don't register* |
| Periodic Task (1s loop) | 1000 ms | 3000 ms |
| LED Control (100ms loop) | 100 ms | 300 ms |
| Slow Processing (5s) | 5000 ms | 15000 ms |

**\*Note:** Tasks that legitimately block indefinitely (waiting on queues, semaphores)
should NOT register with watchdog, as they'll cause false alarms.

---

### Step 3: Feed the Watchdog

**Golden rules:**
1. Feed **BEFORE** timeout expires
2. Feed in your main loop (not in init code)
3. Feed regularly and frequently
4. Don't feed if task is actually hung!

**GOOD Example:**
```c
void periodic_task(void *parameters)
{
    watchdog_id_t wd_id = watchdog_register("Periodic", 3000);

    while(1) {
        // Do work that takes ~1 second
        process_data();

        // Feed watchdog (we're alive!)
        watchdog_feed(wd_id);

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
```

**BAD Example (will cause false alarms):**
```c
void task_with_long_delay(void *parameters)
{
    watchdog_id_t wd_id = watchdog_register("SlowTask", 1000);  // Too short!

    while(1) {
        watchdog_feed(wd_id);

        vTaskDelay(pdMS_TO_TICKS(5000));  // Delay longer than timeout!
        // ^^^ WATCHDOG ALERT! (but task is actually fine)
    }
}
```

---

## Example Implementations

### Example 1: UART Task

```c
void uart_task_handler(void *parameters)
{
    uint8_t received_char;

    // Register with 3 second timeout
    // (UART is fast, shouldn't take more than 3s to process a byte)
    watchdog_id_t wd_id = watchdog_register("UART_ESP", 3000);

    // Send startup message
    const char *startup = "\r\nSTM32 LED Controller Ready (Stream Buffer Mode)\r\n";
    HAL_UART_Transmit(&huart2, (uint8_t*)startup, strlen(startup), 1000);

    while (1) {
        // Read one byte from stream buffer (TRUE BLOCKING)
        size_t received = xStreamBufferReceive(uart_stream_buffer,
                                               &received_char,
                                               1,
                                               portMAX_DELAY);

        if (received > 0) {
            // Process character...

            // Feed watchdog after processing
            watchdog_feed(wd_id);
        }
    }
}
```

---

### Example 2: Command Handler Task

```c
void command_handler_task(void *parameters)
{
    char command[COMMAND_MAX_LENGTH];

    // DON'T register this task!
    // It legitimately blocks waiting for commands.
    // False alarms would occur when no user input.

    while (1) {
        // Wait for notification (blocks indefinitely - this is OK!)
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        // Receive command from queue
        if (xQueueReceive(command_queue, command, 0) == pdPASS) {
            // Process command...
            process_command(command);
        }
    }
}
```

---

### Example 3: Periodic LED Task

```c
void led_blink_task(void *parameters)
{
    // Register with 500ms timeout (blink every 100ms)
    watchdog_id_t wd_id = watchdog_register("LED_Blink", 500);

    while(1) {
        // Toggle LED
        HAL_GPIO_TogglePin(LED_GPIO_Port, LED_Pin);

        // Feed watchdog
        watchdog_feed(wd_id);

        // Delay 100ms
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}
```

---

## What Happens When Timeout Occurs?

### Default Behavior (No Callback Set)

When a task fails to feed within its timeout, the watchdog prints to UART:

```
*** WATCHDOG ALERT ***
Task: UART_ESP (ID=0)
Last feed: 5234 ms ago
Timeout: 3000 ms
Status: HUNG or DEADLOCKED!
```

---

### Custom Callback (Optional)

You can set a custom handler for watchdog alerts:

```c
void my_watchdog_handler(watchdog_id_t id, const char *task_name, uint32_t last_feed_ms)
{
    // Log to flash
    log_to_flash("WATCHDOG: %s hung for %lu ms", task_name, last_feed_ms);

    // Take action based on task
    if (strcmp(task_name, "UART_ESP") == 0) {
        // Restart UART subsystem
        restart_uart();
    } else {
        // Last resort: system reset
        NVIC_SystemReset();
    }
}

// In main(), after watchdog_init():
watchdog_set_callback(my_watchdog_handler);
```

---

## Testing the Watchdog

### Test 1: Verify Watchdog is Running

After flashing:
```
[WATCHDOG] Initialized
[WATCHDOG] Monitor task started
```

You should see this on UART during boot.

---

### Test 2: Register a Task

Add watchdog registration to any task and check:
```
[WATCHDOG] Registered 'UART_ESP' (ID=0, timeout=3000ms)
```

---

### Test 3: Trigger a Timeout (Intentional Deadlock)

Create a test task that stops feeding:

```c
void test_deadlock_task(void *parameters)
{
    watchdog_id_t wd_id = watchdog_register("TestTask", 2000);

    // Feed a few times
    for (int i = 0; i < 5; i++) {
        watchdog_feed(wd_id);
        vTaskDelay(pdMS_TO_TICKS(500));
    }

    // STOP FEEDING - simulate deadlock!
    print_message("TestTask: Simulating deadlock...\r\n");
    while(1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        // NOT feeding watchdog!
    }
}
```

**Expected output after ~2 seconds:**
```
*** WATCHDOG ALERT ***
Task: TestTask (ID=1)
Last feed: 2143 ms ago
Timeout: 2000 ms
Status: HUNG or DEADLOCKED!
```

---

## Limitations

### What Watchdog CAN Detect:
✅ Task stuck in infinite loop
✅ Task blocked on mutex/semaphore (deadlock)
✅ Task crashed (hard fault before reaching feed)
✅ Task priority inversion (starved of CPU)

### What Watchdog CANNOT Detect:
❌ Logic errors (task runs but does wrong thing)
❌ Memory corruption (unless it causes hang)
❌ Livelock (task runs but makes no progress)
❌ Tasks that legitimately block indefinitely

---

## Configuration Options

In `watchdog.h`, you can adjust:

```c
#define WATCHDOG_MAX_TASKS  8       // Max tasks to monitor
#define WATCHDOG_TASK_PRIORITY  4   // Watchdog task priority (should be highest!)
#define WATCHDOG_TASK_STACK_SIZE  256  // Stack in words
#define WATCHDOG_CHECK_PERIOD_MS  1000  // How often to check (ms)
```

---

## Best Practices

### 1. Don't Over-Use Watchdog

**Register:**
- Tasks with periodic behavior (timers, polling loops)
- Critical tasks that must never hang

**Don't Register:**
- Tasks that legitimately block on queues/semaphores
- Low-priority background tasks
- Tasks that have variable/unpredictable timing

---

### 2. Choose Realistic Timeouts

**Too short:** False alarms (task is fine but slow)
**Too long:** Late detection (system already failed)

**Sweet spot:** 2-3× your normal loop time

---

### 3. Feed Only When Healthy

Don't blindly feed at start of loop:

```c
// BAD - feeds even if stuck in processing!
while(1) {
    watchdog_feed(wd_id);  // Feed BEFORE work

    process_data();  // If this hangs, watchdog thinks we're OK!
}

// GOOD - only feed if we complete loop
while(1) {
    process_data();  // Do work first

    watchdog_feed(wd_id);  // Feed after successful completion
}
```

---

### 4. Test Thoroughly

Before deploying, test:
- Normal operation (no false alarms)
- Simulated deadlock (watchdog detects it)
- Recovery mechanism (if implemented)

---

## Next Steps

1. **Instrument your tasks** - Add registration and feeding
2. **Test normal operation** - Verify no false alarms
3. **Test deadlock detection** - Create intentional hang
4. **Implement recovery** - Add callback to handle timeouts
5. **Deploy** - Monitor in production

---

## Summary

| Component | Status |
|-----------|--------|
| **Watchdog Module** | ✅ Created (`watchdog.h`, `watchdog.c`) |
| **014 Project** | ✅ `watchdog_init()` added to main.c |
| **015 Project** | ✅ `watchdog_init()` added to main.c |
| **Task Instrumentation** | ⏳ **YOU DO THIS** (examples provided above) |
| **Testing** | ⏳ **TODO** (test deadlock detection) |

---

**Ready to use!** The watchdog infrastructure is in place. Now you just need to:
1. Pick which tasks to monitor
2. Add `watchdog_register()` and `watchdog_feed()` calls
3. Test!

Good luck! 🐕
