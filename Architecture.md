# Architecture Documentation
## 014 LED Patterns UART Input - Stream Buffer + Multi-Task Design

---

## System Overview

This project demonstrates a **FreeRTOS architecture** featuring:
1. **Stream Buffer UART RX** - Efficient interrupt-driven reception with TRUE task blocking
2. **Dedicated Print Task** - Exclusive UART TX ownership (no mutex needed)
3. **Multi-Task Command Processing** - Separation of reception, processing, and output
4. **Interactive Menu System** - Hierarchical command interface

### High-Level Architecture

```
┌──────────────────────────────────────────────────────────────────┐
│                         User Terminal                             │
└───────────┬─────────────────────────────────────────┬────────────┘
            │ UART RX (PA3)                  UART TX (PA2) │
            ↓                                               ↑
     ┌─────────────┐                               ┌───────────────┐
     │  RX ISR     │                               │  Print Task   │
     └──────┬──────┘                               └───────↑───────┘
            │                                              │
            ↓                                              │
     ┌─────────────┐                               ┌───────────────┐
     │ Stream      │                               │ Print Queue   │
     │ Buffer      │                               │ (10 messages) │
     └──────┬──────┘                               └───────↑───────┘
            │                                              │
            ↓                                              │
     ┌─────────────┐        ┌──────────────┐             │
     │  UART Task  │ ────> │ Command Queue│ ──┐          │
     │  (BLOCKED)  │        └──────────────┘   │          │
     └─────────────┘                            ↓          │
                                         ┌──────────────┐  │
                                         │   Command    │──┘
                                         │   Handler    │
                                         └──────┬───────┘
                                                │
                                                ↓
                                         ┌──────────────┐
                                         │ LED Effects  │
                                         │  (Timers)    │
                                         └──────────────┘
```

---

## Stream Buffer Architecture (NEW!)

### The Problem

**HAL_UART_Receive() is NOT truly blocking:**
```c
// What you think happens:
HAL_UART_Receive(&huart2, &ch, 1, portMAX_DELAY);  // Task blocks, yields CPU?

// What actually happens:
while (!UART_DATA_READY) {
    // Task stays RUNNING, polls flag in tight loop!
    // CPU wasted checking flag repeatedly
}
```

**Issues:**
- ❌ Task remains in RUNNING state (not BLOCKED)
- ❌ CPU cycles wasted polling
- ❌ Less efficient power consumption

---

### The Solution: Stream Buffer + Interrupt

```
┌──────────────┐     ┌──────────────┐     ┌──────────────┐
│  UART RX ISR │ ──> │Stream Buffer │ ──> │  UART Task   │
│  (~2μs)      │     │ (Lock-free)  │     │  (BLOCKED)   │
└──────────────┘     └──────────────┘     └──────────────┘
   Instant wake           Thread-safe        Yields CPU!
```

**How it works:**

1. **Initialization:**
```c
uart_stream_buffer = xStreamBufferCreate(128, 1);
HAL_UART_Receive_IT(&huart2, &uart_rx_byte, 1);
```

2. **ISR deposits byte:**
```c
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    xStreamBufferSendFromISR(uart_stream_buffer, &uart_rx_byte, 1, &woken);
    HAL_UART_Receive_IT(&huart2, &uart_rx_byte, 1);
    portYIELD_FROM_ISR(woken);
}
```

3. **Task reads (with finite timeout for watchdog):**
```c
void uart_task_handler(void *parameters)
{
    watchdog_id_t wd_id = watchdog_register("UART_task", 5000);

    while(1) {
        // Task enters BLOCKED state (yields CPU) for up to 2 seconds
        xStreamBufferReceive(uart_stream_buffer, &ch, 1, pdMS_TO_TICKS(2000));

        // Feed watchdog every iteration (timeout or data received)
        watchdog_feed(wd_id);

        // Wakes here when ISR deposits byte OR on 2s timeout
        if (received > 0) {
            process_character(ch);
        }
    }
}
```

**Benefits:**
- ✅ **TRUE blocking** - Task enters BLOCKED state, yields CPU to other tasks
- ✅ **Zero CPU waste** - No polling loop (wakes on data OR 2s timeout)
- ✅ **Instant wake-up** - ISR immediately unblocks task (<10μs latency)
- ✅ **Thread-safe** - Lock-free ISR-to-Task communication
- ✅ **Watchdog monitored** - Detects deadlocks/hangs automatically
- ✅ **Production-quality** - Proper RTOS design pattern

---

## Dedicated Print Task Architecture

### Why Not Use Mutexes?

**Traditional mutex approach:**
```c
// In every task that prints...
xSemaphoreTake(uart_mutex, portMAX_DELAY);
HAL_UART_Transmit(&huart2, msg, len, timeout);  // Task blocks here!
xSemaphoreGive(uart_mutex);
```

**Problems:**
- Priority inversion issues
- Task blocking during TX
- Mutex boilerplate repeated everywhere
- Complex error handling
- Harder to maintain

---

### Our Solution: Dedicated Print Task

**From ANY task - simple, non-blocking:**
```c
print_message("Hello World\r\n");  // Returns immediately!
print_char('A');                    // ~20-50μs, no blocking
```

**Architecture:**
```
┌────────────┐     ┌─────────────┐     ┌─────────────┐
│  Any Task  │ ──> │ Print Queue │ ──> │ Print Task  │
│            │     │  (FIFO)     │     │ (Priority 3)│
└────────────┘     └─────────────┘     └──────┬──────┘
  Enqueue ~20μs       Thread-safe              │
  Returns!                                     ↓
                                        UART TX Exclusive
                                         Owner (PA2)
```

**Benefits:**
- ✅ Non-blocking (enqueue ~20-50μs, return immediately)
- ✅ No priority inversion (queue-based synchronization)
- ✅ Clean API (no mutex boilerplate in 10+ locations)
- ✅ FIFO ordering guaranteed
- ✅ Single point of control (easy to extend)
- ✅ Better testability

---

## Complete Data Flow

### User Types "1" → LED Pattern Activates

```
┌─────────────────────────────────────────────────────────────┐
│ Step 1: User Types '1'                                       │
└──────────────────────────────┬──────────────────────────────┘
                               ↓
                        UART RX (PA3)
                               ↓
┌──────────────────────────────────────────────────────────────┐
│ Step 2: ISR Executes                                          │
│  HAL_UART_RxCpltCallback() → xStreamBufferSendFromISR()      │
│  Time: ~2μs                                                   │
└──────────────────────────────┬───────────────────────────────┘
                               ↓
                        Stream Buffer
                               ↓
┌──────────────────────────────────────────────────────────────┐
│ Step 3: UART Task Wakes                                       │
│  xStreamBufferReceive() returns → print_char('1') → buffer   │
│  Time: ~10μs wake + ~20μs enqueue                            │
└──────────────────────────────┬───────────────────────────────┘
                               ↓
                          Print Queue
                               ↓
┌──────────────────────────────────────────────────────────────┐
│ Step 4: Print Task Executes                                   │
│  HAL_UART_Transmit() → Echo '1' to terminal                  │
│  Time: ~87μs @ 115200 baud                                   │
└───────────────────────────────────────────────────────────────┘
                               ↓
┌──────────────────────────────────────────────────────────────┐
│ Step 5: User Presses Enter                                    │
│  ISR → Stream Buffer → UART Task                              │
└──────────────────────────────┬───────────────────────────────┘
                               ↓
                        Command Complete!
                               ↓
┌──────────────────────────────────────────────────────────────┐
│ Step 6: UART Task Sends to Command Queue                      │
│  xQueueSend(command_queue, "1") → xTaskNotifyGive()          │
└──────────────────────────────┬───────────────────────────────┘
                               ↓
                         Command Queue
                               ↓
┌──────────────────────────────────────────────────────────────┐
│ Step 7: Command Handler Wakes                                 │
│  Processes "1" → Changes LED pattern → Prints response        │
└──────────────────────────────┬───────────────────────────────┘
                               ↓
                          Print Queue
                               ↓
┌──────────────────────────────────────────────────────────────┐
│ Step 8: Response Printed                                      │
│  "Pattern 1 activated\r\n" → User sees feedback               │
└───────────────────────────────────────────────────────────────┘
```

**Total Time:** ~200μs from keystroke to echo + LED activation

---

## Task Priority & Scheduling

| Task | Priority | Stack | Function | Blocking Point | Watchdog |
|------|----------|-------|----------|----------------|----------|
| **Watchdog Task** | 4 (highest) | 256 words | Deadlock detection | `vTaskDelayUntil` (1s period) | N/A (monitor) |
| **Print Task** | 3 | 512 words | UART TX | `xQueueReceive` (2s timeout) | ✅ 5s timeout |
| **UART Task** | 2 | 1024 words | UART RX via stream buffer | `xStreamBufferReceive` (2s timeout) | ✅ 5s timeout |
| **Command Handler** | 2 | 1024 words | Command processing | `ulTaskNotifyTake` (2s timeout) | ✅ 5s timeout |
| **Timer Service** | 2 | 512 words | LED timer callbacks | Event-driven | ❌ Not registered |
| **Idle Task** | 0 (lowest) | Auto | Power save (WFI) | Runs when all blocked | ❌ Not registered |

**Why Watchdog has highest priority:**
- Must run even if other tasks are hung
- Critical for system reliability
- Very short execution time (~1ms per check)
- Runs only every 1 second

**Why Print Task has 2nd highest priority:**
- Ensures responsive user feedback
- Prevents print queue overflow
- Short execution time (just UART TX)
- Higher priority than command processing

---

## Memory Layout

### Heap Usage

| Object | Size | Location |
|--------|------|----------|
| **Stream buffer storage** | 128 bytes | Heap |
| **Stream buffer control** | ~24 bytes | Heap |
| **Command queue** (5 × 32 bytes) | 160 bytes | Heap |
| **Print queue** (10 × 512 bytes) | 5120 bytes | Heap |
| **UART task stack** | 1024 bytes | Heap |
| **Command handler stack** | 1024 bytes | Heap |
| **Print task stack** | 512 bytes | Heap |
| **Timer service stack** | 512 bytes | Heap |
| **LED timer objects** | ~200 bytes | Heap |
| **Total** | **~8.7 KB** | **17% of 75KB heap** |

**Remaining:** ~62 KB (83%) available for expansion

---

## Performance Analysis

### UART RX Latency

**From byte arrival to task processing:**
1. UART hardware interrupt: ~1-2 μs
2. ISR execution (stream buffer send): ~1-2 μs
3. Context switch to UART task: ~5-10 μs
4. Task resumes execution: immediate

**Total: ~10-15 μs** 🚀

**vs. Polling mode:** Up to 1 ms (FreeRTOS tick period) worst-case delay

---

### CPU Utilization

**Idle State (no user input):**
```
UART Task:        BLOCKED (0% CPU)
Command Handler:  BLOCKED (0% CPU)
Print Task:       BLOCKED (0% CPU)
Timer Service:    SUSPENDED (0% CPU)
Idle Task:        RUNNING (~98% → enters WFI sleep mode)
```

**Active Input (user typing at 60 WPM):**
```
Each keystroke: ~50 μs total processing
  - ISR overhead: ~2 μs
  - Task wake/process: ~48 μs

Average CPU: < 0.1%
Power state: >99% in low-power WFI
```

---

## FreeRTOS Primitives Deep Dive

### 1. Stream Buffer (UART RX)

**Why Stream Buffer vs Queue?**

| Feature | Stream Buffer | Queue |
|---------|---------------|-------|
| **Data type** | Raw byte stream | Fixed-size messages |
| **Use case** | UART, SPI, I2C data | Commands, events |
| **Overhead** | Lower (continuous data) | Higher (per-item metadata) |
| **ISR-safe** | Yes | Yes |
| **Lock-free** | Yes (single producer/consumer) | Yes |
| **Best for** | Variable-length streams | Discrete messages |

**Implementation:**
```c
// Creation
StreamBufferHandle_t buffer = xStreamBufferCreate(
    128,  // Buffer size in bytes
    1     // Trigger level (wake on 1+ bytes)
);

// ISR write (producer)
size_t sent = xStreamBufferSendFromISR(buffer, &data, 1, &woken);

// Task read (consumer)
size_t received = xStreamBufferReceive(buffer, &data, 1, portMAX_DELAY);
```

**Key Point:** Stream buffers are **perfect for UART** because:
- Data is a continuous byte stream (not discrete messages)
- Lower RAM overhead than queues
- Simpler API for byte-by-byte reception
- Lock-free with single producer (ISR) and single consumer (task)

---

### 2. Queue (Commands & Print Messages)

**Command Queue:**
```c
// Small fixed-size commands
QueueHandle_t command_queue = xQueueCreate(
    5,   // Queue depth
    32   // Item size (bytes)
);
```

**Print Queue:**
```c
// Larger variable-length messages
typedef struct {
    msg_type_t msg_type;
    char message[512];
} print_msg_t;

QueueHandle_t print_queue = xQueueCreate(
    10,                // Queue depth
    sizeof(print_msg_t)  // Item size
);
```

---

### 3. Task Notification (Command Handler Wake-up)

```c
TaskHandle_t command_handler_task_handle;

// UART Task notifies (sender)
xTaskNotifyGive(command_handler_task_handle);

// Command Handler waits (receiver)
uint32_t notification_value = ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
```

**Why Task Notification instead of Semaphore?**
- ✅ ~45% faster than binary semaphore
- ✅ Lower RAM overhead (uses task's notification value)
- ✅ Simpler API
- ✅ Built into task control block (no separate object)

---

## Thread Safety Analysis

### No Race Conditions

**UART RX:**
```
ISR (producer) → Stream Buffer → UART Task (consumer)
```
- ✅ Single producer (ISR only)
- ✅ Single consumer (UART Task only)
- ✅ Lock-free stream buffer guarantees atomicity

**UART TX:**
```
Multiple Tasks → Print Queue → Print Task (exclusive TX owner)
```
- ✅ Queue provides thread-safe enqueue
- ✅ Only Print Task calls HAL_UART_Transmit()
- ✅ No mutex needed!

**Command Processing:**
```
UART Task → Command Queue → Command Handler
```
- ✅ Single producer (UART Task)
- ✅ Single consumer (Command Handler)
- ✅ Queue ensures FIFO ordering

---

## Watchdog System (Deadlock Detection)

### Architecture

**All three application tasks actively monitored:**

```
┌──────────────┐   Register   ┌──────────────┐
│  UART_task   │──────────────>│              │
└──────┬───────┘               │              │
       │ Feed every 2s         │  Watchdog    │  Checks every 1s
┌──────▼────────┐              │   Monitor    │  → Alerts on timeout
│ CMD_Handler   │──────────────>│   Task       │     (5s timeout)
└──────┬────────┘              │ (Priority 4) │
       │ Feed every 2s         │              │
┌──────▼────────┐              │              │
│  Print_Task   │──────────────>│              │
└───────────────┘   Register   └──────────────┘
                                      │
                               Timeout detected!
                                      ↓
                               UART Alert / Callback
```

**Registered Tasks:**
- ✅ `UART_task` - Timeout: 5000ms, Feed interval: 2000ms
- ✅ `CMD_Handler` - Timeout: 5000ms, Feed interval: 2000ms
- ✅ `Print_Task` - Timeout: 5000ms, Feed interval: 2000ms

---

### How It Works

**1. Registration (at task startup):**
```c
void uart_task_handler(void *parameters)
{
    // Register with 5 second timeout (2.5× the 2s blocking period)
    watchdog_id_t wd_id = watchdog_register("UART_task", 5000);

    // Task loop...
}
```

**2. Finite Timeout Pattern (for watchdog compatibility):**
```c
while(1) {
    // Use finite timeout instead of portMAX_DELAY
    // Allows periodic watchdog feeding even when idle
    xStreamBufferReceive(buffer, &ch, 1, pdMS_TO_TICKS(2000));

    // Feed watchdog on every iteration (whether data received or timeout)
    watchdog_feed(wd_id);
}
```

**3. Monitoring (watchdog task runs every 1 second):**
```c
void watchdog_task(void)
{
    while(1) {
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(1000));

        for (each registered task) {
            if (time_since_last_feed > timeout) {
                // ALERT! Task is hung or deadlocked
                print_alert(task_name, time_since_last_feed);
            }
        }
    }
}
```

---

### What Watchdog Detects

| Problem | Detection | Example |
|---------|-----------|---------|
| **Infinite Loop** | ✅ Yes | Task stuck in `while(1) {}` without feeding |
| **Deadlock** | ✅ Yes | Task waiting on mutex forever |
| **Priority Inversion** | ✅ Yes | High priority task starved of CPU |
| **Hard Fault** | ✅ Yes | Task crashed before feeding |
| **Slow Task** | ⚠️ Maybe | If timeout too short → false alarm |
| **Logical Error** | ❌ No | Task runs but does wrong thing |

---

### Configuration

In `watchdog.h`:
```c
#define WATCHDOG_MAX_TASKS  8         // Max tasks to monitor
#define WATCHDOG_TASK_PRIORITY  4     // Highest priority
#define WATCHDOG_TASK_STACK_SIZE  256 // Stack (words)
#define WATCHDOG_CHECK_PERIOD_MS  1000 // Check interval
```

---

### Actual Implementation

**UART Task:**
```c
void uart_task_handler(void *parameters)
{
    // Register with 5 second timeout
    watchdog_id_t wd_id = watchdog_register("UART_task", 5000);

    while (1) {
        // Finite 2s timeout (not portMAX_DELAY)
        xStreamBufferReceive(buffer, &ch, 1, pdMS_TO_TICKS(2000));

        // Feed watchdog every iteration
        watchdog_feed(wd_id);

        if (received > 0) {
            handle_character(ch);
        }
    }
}
```

**Command Handler Task:**
```c
void command_handler_task(void *parameters)
{
    // Register with 5 second timeout
    watchdog_id_t wd_id = watchdog_register("CMD_Handler", 5000);

    while (1) {
        // Finite 2s timeout (not portMAX_DELAY)
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(2000));

        // Feed watchdog every iteration
        watchdog_feed(wd_id);

        while (xQueueReceive(command_queue, cmd, 0) == pdPASS) {
            process_command(cmd);
        }
    }
}
```

**Print Task:**
```c
void print_task_handler(void *parameters)
{
    // Register with 5 second timeout
    watchdog_id_t wd_id = watchdog_register("Print_Task", 5000);

    while (1) {
        // Finite 2s timeout (not portMAX_DELAY)
        xQueueReceive(print_queue, msg, pdMS_TO_TICKS(2000));

        // Feed watchdog every iteration
        watchdog_feed(wd_id);

        if (received) {
            HAL_UART_Transmit(&huart2, msg, len, HAL_MAX_DELAY);
        }
    }
}
```

**When timeout occurs:**
```
*** WATCHDOG ALERT ***
Task: UART_task (ID=0)
Last feed: 5234 ms ago
Timeout: 5000 ms
Status: HUNG or DEADLOCKED!
```

---

### Best Practices

**DO:**
- ✅ Use finite blocking timeouts (e.g., 2 seconds) instead of `portMAX_DELAY`
- ✅ Feed watchdog on every loop iteration (whether work done or timeout)
- ✅ Set watchdog timeout = 2-3× blocking period (5s for 2s loop)
- ✅ Register task after initialization, before main loop
- ✅ Test with intentional deadlock to verify detection

**DON'T:**
- ❌ Use `portMAX_DELAY` if task is registered with watchdog
- ❌ Feed only when work is done (feed on timeout too!)
- ❌ Use timeout shorter than normal execution time
- ❌ Feed blindly without checking watchdog registration succeeded
- ❌ Forget to test!

**Key Pattern:**
```c
// GOOD: Finite timeout + feed on every iteration
while(1) {
    xQueueReceive(queue, &item, pdMS_TO_TICKS(2000));  // Finite timeout
    watchdog_feed(wd_id);  // Feed regardless of receive status
}

// BAD: Infinite blocking prevents watchdog feeding
while(1) {
    xQueueReceive(queue, &item, portMAX_DELAY);  // Never times out!
    watchdog_feed(wd_id);  // Only fed when item received
}
```

---

### Memory Overhead

| Component | Size |
|-----------|------|
| Watchdog task stack | 256 words (1024 bytes) |
| Task tracking array (8 tasks) | ~192 bytes |
| Total | ~1.2 KB |

---

### CPU Overhead

- **Idle:** 0% (task blocked)
- **Active:** ~0.1% (1ms check every 1 second)
- **On timeout:** Brief spike for alert message

---

## Comparison: Before vs After Stream Buffer

### Before (Polling Mode)

```c
void uart_task_handler(void *parameters)
{
    while(1) {
        // Task stays RUNNING, polls UART flag
        HAL_UART_Receive(&huart2, &ch, 1, portMAX_DELAY);

        // Under the hood: tight polling loop
        // while (!UART_RXNE) { /* CPU waste */ }

        print_char(ch);
        // ...
    }
}
```

**Characteristics:**
- ❌ Task remains in RUNNING state (not truly blocked)
- ❌ CPU cycles wasted polling UART RXNE flag
- ❌ Task only yields when preempted by tick interrupt
- ❌ Higher power consumption
- ✅ Simple code (fewer lines)
- ✅ Good for learning

---

### After (Stream Buffer Mode)

```c
void uart_task_init(void)
{
    uart_stream_buffer = xStreamBufferCreate(128, 1);
    HAL_UART_Receive_IT(&huart2, &uart_rx_byte, 1);
}

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    xStreamBufferSendFromISR(uart_stream_buffer, &uart_rx_byte, 1, &woken);
    HAL_UART_Receive_IT(&huart2, &uart_rx_byte, 1);
    portYIELD_FROM_ISR(woken);
}

void uart_task_handler(void *parameters)
{
    // Register with watchdog
    watchdog_id_t wd_id = watchdog_register("UART_task", 5000);

    while(1) {
        // Finite timeout (not portMAX_DELAY) for watchdog compatibility
        xStreamBufferReceive(uart_stream_buffer, &ch, 1, pdMS_TO_TICKS(2000));

        // Feed watchdog to prove task is alive
        watchdog_feed(wd_id);

        if (received > 0) {
            print_char(ch);
            // ...
        }
    }
}
```

**Characteristics:**
- ✅ Task enters BLOCKED state (true blocking)
- ✅ Zero CPU waste (no polling)
- ✅ Instant wake-up (<10μs from ISR)
- ✅ Lower power consumption
- ✅ Production-quality design
- ⚠️ More code (split ISR/Task)
- ✅ Better for real products

---

## Debugging Tips

### 1. Verify Task States

```c
// In FreeRTOSConfig.h
#define configGENERATE_RUN_TIME_STATS  1
#define configUSE_TRACE_FACILITY       1

// Check task state
eTaskState uart_state = eTaskGetState(uart_task_handle);

// Should be:
// - eBlocked when idle (good!)
// - eRunning only briefly when processing (good!)
// - NOT eRunning constantly (bad - still polling!)
```

---

### 2. Monitor Stream Buffer Usage

```c
size_t available = xStreamBufferBytesAvailable(uart_stream_buffer);
size_t space = xStreamBufferSpacesAvailable(uart_stream_buffer);

// Healthy system:
// - available ≈ 0 (task keeping up with ISR)
// - space ≈ 128 (buffer mostly empty)

// Problem indicators:
// - available growing (task can't keep up)
// - space shrinking (buffer filling up → data loss imminent)
```

---

### 3. Monitor Queue Depths

```c
UBaseType_t cmd_waiting = uxQueueMessagesWaiting(command_queue);
UBaseType_t print_waiting = uxQueueMessagesWaiting(print_queue);

// Should be 0 or low when system is idle
// High values indicate backlog in processing
```

---

### 4. Breakpoint Locations

**To verify ISR is called:**
```c
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    // ← Breakpoint here
    // Should hit on every received byte

    xStreamBufferSendFromISR(...);
    // ...
}
```

**To verify task wakes up:**
```c
size_t received = xStreamBufferReceive(...);
// ← Breakpoint here
// Should hit immediately after ISR completes
```

**To verify print task executes:**
```c
void print_task_handler(void *parameters)
{
    while(1) {
        xQueueReceive(print_queue, &msg, portMAX_DELAY);
        // ← Breakpoint here
        // Should hit when any task calls print_message()
    }
}
```

---

## Best Practices Implemented

### 1. ISR Safety

✅ Always use `FromISR` variants in interrupt context:
```c
xStreamBufferSendFromISR(...)  // NOT xStreamBufferSend()
xQueueSendFromISR(...)         // NOT xQueueSend()
xTaskNotifyFromISR(...)        // NOT xTaskNotify()
portYIELD_FROM_ISR(...)        // NOT portYIELD()
```

✅ Check higher priority task woken:
```c
BaseType_t xHigherPriorityTaskWoken = pdFALSE;
xStreamBufferSendFromISR(..., &xHigherPriorityTaskWoken);
portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
```

---

### 2. Resource Management

✅ Create all FreeRTOS objects before starting scheduler:
```c
uart_stream_buffer = xStreamBufferCreate(...);
configASSERT(uart_stream_buffer != NULL);  // Catch creation failure

command_queue = xQueueCreate(...);
configASSERT(command_queue != NULL);
```

✅ Start first reception before task runs:
```c
HAL_UART_Receive_IT(&huart2, &uart_rx_byte, 1);
```

---

### 3. Separation of Concerns

| Concern | Owner | Benefit |
|---------|-------|---------|
| **UART RX** | UART Task (via ISR + stream buffer) | Clean reception logic |
| **UART TX** | Print Task (via queue) | No mutex needed |
| **Command Processing** | Command Handler | Decoupled from I/O |
| **LED Control** | Software Timers | Non-blocking patterns |

**Result:** Maintainable, testable, scalable architecture

---

### 4. Error Handling

✅ Buffer overflow protection:
```c
if (rx_index >= (UART_RX_BUFFER_SIZE - 1)) {
    rx_index = 0;
    print_message("ERROR: Buffer overflow\r\n");
}
```

✅ Queue full handling:
```c
if (xQueueSend(queue, &item, timeout) != pdPASS) {
    print_message("ERROR: Queue full\r\n");
}
```

---

## Future Enhancements

### 1. DMA Mode (Even Lower CPU)

```c
// Start circular DMA reception
HAL_UART_Receive_DMA(&huart2, dma_buffer, DMA_BUFFER_SIZE);

// Half-complete callback
void HAL_UART_RxHalfCpltCallback(UART_HandleTypeDef *huart)
{
    // Process first half while DMA fills second half
    xStreamBufferSend(uart_stream_buffer, &dma_buffer[0], DMA_BUFFER_SIZE/2, 0);
}

// Complete callback
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    // Process second half while DMA fills first half
    xStreamBufferSend(uart_stream_buffer, &dma_buffer[DMA_BUFFER_SIZE/2], DMA_BUFFER_SIZE/2, 0);
}
```

**Benefits:**
- Zero CPU for byte-by-byte reception
- ISR only called on buffer half-full/full
- Even better power efficiency

---

### 2. Command History

```c
static char cmd_history[CMD_HISTORY_SIZE][COMMAND_MAX_LENGTH];
static uint8_t history_index = 0;

// On up arrow (ESC [ A)
void recall_previous_command(void)
{
    if (history_index > 0) {
        history_index--;
        strcpy(rx_buffer, cmd_history[history_index]);
        print_message(rx_buffer);
    }
}
```

---

### 3. TX Stream Buffer (Non-Blocking Transmit)

```c
StreamBufferHandle_t tx_stream_buffer;

// Any task - fully non-blocking
void print_message_nb(const char *msg)
{
    xStreamBufferSend(tx_stream_buffer, msg, strlen(msg), 0);
}

// DMA TX ISR feeds from stream buffer
void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart)
{
    size_t bytes = xStreamBufferReceiveFromISR(tx_stream_buffer, tx_buffer, TX_BUFFER_SIZE, &woken);
    if (bytes > 0) {
        HAL_UART_Transmit_DMA(&huart2, tx_buffer, bytes);
    }
}
```

---

### 4. Error Statistics

```c
typedef struct {
    uint32_t uart_overrun_errors;
    uint32_t uart_framing_errors;
    uint32_t uart_parity_errors;
    uint32_t stream_buffer_overflows;
    uint32_t queue_full_errors;
} system_stats_t;

// Menu option to display stats
void print_system_stats(void)
{
    print_message("=== System Statistics ===\r\n");
    printf("UART Overrun: %lu\r\n", stats.uart_overrun_errors);
    printf("Buffer Overflow: %lu\r\n", stats.stream_buffer_overflows);
    // ...
}
```

---

## Summary

### Key Takeaways

1. **Stream Buffer UART RX** - TRUE blocking, zero CPU waste, instant wake-up
2. **Dedicated Print Task** - Cleaner than mutexes, guaranteed thread safety
3. **Multi-Task Architecture** - Clean separation of RX, TX, command processing
4. **Production-Ready** - Comprehensive error handling, robust design patterns

### Architectural Strengths

| Aspect | Implementation | Benefit |
|--------|----------------|---------|
| **UART RX** | Stream Buffer + ISR | TRUE blocking, zero CPU waste |
| **UART TX** | Dedicated Print Task | No mutex, no priority inversion |
| **Command Flow** | Queue-based | Decoupled RX from processing |
| **LED Control** | Software Timers | Non-blocking, deterministic |
| **Watchdog** | All 3 tasks monitored (2s feed, 5s timeout) | Deadlock/hang detection |
| **Task Blocking** | Finite timeouts (2s) | Watchdog compatible |
| **Power** | WFI in idle hook | ~98% time in low-power mode |
| **Thread Safety** | Queues + Stream Buffers | Lock-free, no race conditions |

### Performance Metrics

- **RX Latency:** ~10-15 μs (byte arrival to task processing)
- **TX Latency:** ~20-50 μs (print_message() call to enqueue)
- **CPU Utilization:** < 0.1% during normal typing
- **Power State:** >99% in WFI sleep mode
- **Memory Usage:** ~8.7 KB (17% of heap), 62 KB free

---

## References

- [FreeRTOS Stream Buffer Documentation](https://www.freertos.org/RTOS-stream-buffer-example.html)
- [FreeRTOS Queue Documentation](https://www.freertos.org/a00018.html)
- [FreeRTOS Task Notifications](https://www.freertos.org/RTOS-task-notifications.html)
- [STM32 HAL UART Documentation](https://www.st.com/resource/en/user_manual/um1725-description-of-stm32f4-hal-and-lowlayer-drivers-stmicroelectronics.pdf)
- FreeRTOS API Reference: stream_buffer.h, queue.h, task.h
- STM32F4 Reference Manual: USART section (RM0090)

---

**Document Version:** 2.0 (Stream Buffer Update)
**Last Updated:** December 2024
**Project:** 014LedPatternsUartInput
**Architecture:** Multi-task FreeRTOS with Stream Buffer UART RX
