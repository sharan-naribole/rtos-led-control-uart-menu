# LED Pattern Control - FreeRTOS Application

A professional FreeRTOS-based embedded application demonstrating advanced task architecture, thread-safe UART communication, and LED pattern control on STM32F407VG.

> **📚 For comprehensive technical documentation, detailed architecture analysis, timing diagrams, and design decisions, see [Architecture.md](Architecture.md)**

## 🎯 Project Overview

This project showcases a **production-ready FreeRTOS application** with an interactive UART menu system for controlling LED patterns. The standout feature is a **dedicated print task architecture** that eliminates the need for mutex protection by providing exclusive UART TX ownership.

**Key Innovation:** Instead of using mutexes to protect concurrent UART access (common approach), this implementation uses a dedicated print task with a message queue. This results in cleaner code, better performance, and guaranteed thread safety.

## ✨ Features

- 🎨 **4 LED Patterns**: Static ON, Different frequency blinking, Synchronized blinking, OFF
- 💬 **Interactive UART Menu**: Hierarchical menu system with command processing
- 🔒 **Thread-Safe Design**: Queue-based architecture eliminates race conditions
- ⚡ **Non-Blocking I/O**: Print operations return immediately, no task blocking
- ⚙️ **Efficient UART RX**: Stream Buffer mode with TRUE task blocking (zero CPU waste)
- 🐕 **Watchdog System**: Detects hung or deadlocked tasks automatically
- 🔋 **Power Efficient**: ~98% CPU idle time, WFI sleep mode in idle hook
- 📊 **Well Architected**: Clean separation of concerns (RX, TX, commands, LEDs)
- 📚 **Comprehensive Documentation**: Detailed architecture documentation included

## 🏗️ Architecture Highlights

### Task Structure

```
Priority 4: Watchdog Task   → Monitors task health, detects deadlocks
Priority 3: Print Task      → Exclusive UART TX owner
Priority 2: UART Task       → Character reception & buffering
Priority 2: Command Handler → Menu state machine & LED control
Priority 2: Timer Service   → Software timer callbacks for LED patterns
Priority 0: Idle Task       → Power save (WFI instruction)
```

### Print Task Design

**Problem with Traditional Mutex Approach:**
```c
// Multiple tasks competing for UART
xSemaphoreTake(uart_mutex, portMAX_DELAY);
HAL_UART_Transmit(&huart2, data, len, timeout);  // Task blocks here
xSemaphoreGive(uart_mutex);
```
Issues: Priority inversion, blocking delays, complex error handling

**Our Solution - Dedicated Print Task:**
```c
// Any task, anywhere
print_message("Hello World\r\n");  // Returns immediately!
print_char('A');                    // Non-blocking echo
```

Benefits:
- ✅ Non-blocking (enqueue ~20-50μs, return immediately)
- ✅ No priority inversion (queue-based synchronization)
- ✅ Cleaner code (no mutex boilerplate in 10+ locations)
- ✅ FIFO ordering guaranteed
- ✅ Single point of control (easy to extend)

### Memory Usage

| Component | Size | Utilization |
|-----------|------|-------------|
| Total Heap | 75 KB | 17% used |
| Print Queue | ~5.1 KB | 10 messages × 512 bytes |
| Task Stacks | ~6.7 KB | 5 tasks |
| Free Memory | ~62 KB | Available for expansion |

### UART RX Architecture (Stream Buffer Mode)

**Efficient interrupt-driven reception:**

```
UART RX (PA3)
     ↓
 RX Interrupt (ISR)
     ↓
Stream Buffer ──> UART Task (BLOCKED)
 (Lock-free)      Wakes instantly!
```

**Benefits:**
- ✅ **TRUE blocking** - Task enters BLOCKED state, yields CPU
- ✅ **Zero CPU waste** - No polling loop
- ✅ **Instant wake-up** - ISR immediately unblocks task
- ✅ **Thread-safe** - Lock-free ISR-to-Task communication

### Watchdog System (Deadlock Detection)

**All tasks actively monitored:**

```
UART_task, CMD_Handler, Print_Task
         ↓ Register & feed every 2 seconds
  ┌─────────────────┐
  │ Watchdog Task   │  Checks every 1 second
  │  (Priority 4)   │  → If task hasn't fed in 5s → ALERT!
  └─────────────────┘
```

**Implementation:**
```c
// All three tasks use finite timeouts for watchdog monitoring
void uart_task_handler(void *parameters) {
    watchdog_id_t wd_id = watchdog_register("UART_task", 5000);

    while(1) {
        // Finite 2s timeout (instead of portMAX_DELAY)
        xStreamBufferReceive(buffer, &ch, 1, pdMS_TO_TICKS(2000));

        watchdog_feed(wd_id);  // Prove I'm alive every 2s
    }
}
```

**Active Monitoring:**
- ✅ **UART_task** - Feeds every 2s (5s timeout)
- ✅ **CMD_Handler** - Feeds every 2s (5s timeout)
- ✅ **Print_Task** - Feeds every 2s (5s timeout)

**Detects:**
- 🔍 Hung tasks (stuck in infinite loop)
- 🔍 Deadlocked tasks (waiting on mutex forever)
- 🔍 Crashed tasks (hard fault before feeding)
- 🔍 Starved tasks (priority inversion)

**See:** `WATCHDOG_USAGE.md` for complete guide

### Communication Flow

```
User Terminal
     ↓
  UART RX (PA3) → RX ISR → Stream Buffer → UART Task → Command Queue → Command Handler
                            (Instant)       (BLOCKED)                           ↓
                                                                          LED Effects
                                                                                ↓
  UART TX (PA2) ←─────────── Print Task ← Print Queue ←────────── Response Messages
     ↓
User Terminal
```

## 🛠️ Hardware Requirements

- **Board:** STM32F407VG Discovery Board
- **Debugger:** ST-LINK/V2 (integrated on Discovery board)
- **USB-UART Adapter:** FTDI FT232RL or similar (3.3V logic level)
- **LEDs:** On-board LEDs (PD12-Green, PD13-Orange)

### Hardware Setup Diagram

```

    ┌──────────────────┐
    │                  │
    │    PC / Laptop   │
    │                  │
    └────────┬─────────┘
             │
      ┌──────┴────────┐
      │               │
   USB-A          USB Mini-B
      │               │
      │               │
      │        ┌──────▼───────────────────────────────────┐
      │        │  STM32F407VG Discovery Board            │
      │        │  ┌────────────────────────────────┐    │
      │        │  │                                 │    │
      │        │  │      STM32F407VG MCU           │    │
      │        │  │    (ARM Cortex-M4F)            │    │
      │        │  │                                 │    │
      │        │  │  PA2 (UART2 TX) ────────┐      │    │
      │        │  │  PA3 (UART2 RX) ────────┼──┐   │    │
      │        │  │  GND ────────────────────┼──┼─┐ │    │
      │        │  │                          │  │ │ │    │
      │        │  │  PD12 → [LED] Green ●   │  │ │ │    │
      │        │  │  PD13 → [LED] Orange ●  │  │ │ │    │
      │        │  │                          │  │ │ │    │
      │        │  └──────────────────────────┘  │ │ │    │
      │        │         ▲                       │ │ │    │
      │        │         │ SWD (Debug/Flash)    │ │ │    │
      │        │  ┌──────┴──────┐               │ │ │    │
      │        │  │  ST-LINK/V2 │               │ │ │    │
      │        │  │  (On-board) │               │ │ │    │
      │        │  └──────▲──────┘               │ │ │    │
      │        │         │                       │ │ │    │
      │        └─────────┼───────────────────────┼─┼─┼────┘
      │                  │                       │ │ │
      └──────────────────┘                       │ │ │
             USB Mini-B (Debug/Flash)            │ │ │
                                                 │ │ │
      ┌──────────────────────────────────────────┘ │ │
      │  ┌───────────────────────────────────────────┘
      │  │  ┌──────────────────────────────────────┐
      │  │  │                                       │
   ┌──▼──▼──▼──────┐                                │
   │  FTDI FT232RL │                                │
   │  USB-to-UART  │                                │
   │               │                                │
   │  RX ────────────────> PA2 (TX)                 │
   │  TX ────────────────> PA3 (RX)                 │
   │  GND ──────────────────────────────────────────┘
   │               │
   └───────▲───────┘
           │
       USB-A to PC
       (Serial Terminal)


Connections Summary:
╔═══════════════════╦══════════════════════════════════════════╗
║ Connection Type   ║ Details                                  ║
╠═══════════════════╬══════════════════════════════════════════╣
║ Debug & Flash     ║ PC USB → STM32 Discovery USB (ST-LINK)  ║
║ Serial Terminal   ║ PC USB → FTDI adapter → STM32 UART2     ║
║ UART Wiring       ║ FTDI RX ← PA2 (TX)                      ║
║                   ║ FTDI TX → PA3 (RX)                      ║
║                   ║ FTDI GND → STM32 GND                    ║
║ LEDs              ║ PD12 (Green), PD13 (Orange) - On-board  ║
╚═══════════════════╩══════════════════════════════════════════╝

Power: STM32 powered via USB (ST-LINK connection)
```

### Pin Connections Detail

```
FTDI FT232RL         STM32F407VG Discovery
┌─────────────┐      ┌──────────────────┐
│             │      │                  │
│  RX (In)    │◄─────┤ PA2 (UART2 TX)   │  (Yellow wire)
│  TX (Out)   │─────►│ PA3 (UART2 RX)   │  (Orange wire)
│  GND        │──────┤ GND              │  (Black wire)
│  VCC (3.3V) │  ✗   │ (Not connected)  │  (Board self-powered)
│             │      │                  │
└─────────────┘      └──────────────────┘
     │
     │ USB
     ▼
    PC (Serial Terminal: 115200 baud, 8N1)
```

## 💻 Software Requirements

- **IDE:** STM32CubeIDE (or command-line ARM GCC)
- **Toolchain:** ARM GCC (arm-none-eabi)
- **RTOS:** FreeRTOS v10.x (included)
- **HAL:** STM32F4 HAL Driver (included)
- **Terminal:** minicom, screen, PuTTY, or similar (115200 baud, 8N1)

## 🚀 Getting Started

### 1. Build the Project

**Using STM32CubeIDE:**
```bash
# Import project into STM32CubeIDE
File → Import → Existing Projects into Workspace
# Select Debug or Release configuration
# Build: Ctrl+B or Project → Build All
```

**Using Command Line:**
```bash
cd Debug/
make clean
make -j4
```

### 2. Flash to STM32

```bash
# Using ST-LINK utility or STM32CubeIDE
# Flash the generated .elf or .bin file
```

### 3. Connect Terminal

```bash
# Linux/macOS
screen /dev/ttyUSB0 115200
# or
screen /dev/tty.usbserial-XXXXX 115200

# Windows
# Use PuTTY: COM port, 115200 baud, 8N1
```

### 4. Interact with Menu

```
****************************************
*                                      *
*   LED Pattern Control Application   *
*        FreeRTOS UART Interface       *
*                                      *
****************************************

========================================
              MAIN MENU
========================================
  1 - LED Patterns
  2 - Exit Application
========================================
Enter selection: 1

========================================
        LED Pattern Selection
========================================
  0 - Return to main menu
  1 - All LEDs ON
  2 - Different Frequency Blinking
  3 - Same Frequency Blinking
  4 - All LEDs OFF
========================================
Enter selection:
```

## 📁 Project Structure

```
rtos-led-control-uart-menu/
├── includes/
│   ├── main.h
│   ├── uart_task.h
│   ├── print_task.h           ← Print task API
│   ├── command_handler.h
│   ├── led_effects.h
│   └── watchdog.h             ← Watchdog API
├── src/
│   ├── main.c                  ← Initialization & task creation
│   ├── uart_task.c             ← Character RX & buffering
│   ├── print_task.c            ← Print task implementation
│   ├── command_handler.c       ← Menu state machine
│   ├── led_effects.c           ← LED pattern control
│   ├── watchdog.c              ← Watchdog implementation
│   └── stm32f4xx_it.c          ← Interrupt handlers
│   └── Startup/
│       └── startup_stm32f407vgtx.s
├── Drivers/                         ← STM32 HAL & CMSIS
├── common/ThirdParty/
│   ├── FreeRTOS/                   ← FreeRTOS kernel
│   └── SEGGER/                     ← SEGGER SystemView (optional)
├── Debug/                          ← Build output
├── Architecture.md                 ← Detailed architecture docs
├── README.md                       ← This file
└── STM32F407VGTX_FLASH.ld         ← Linker script
```

## 🔧 Configuration

### Key Parameters (print_task.h)

```c
#define PRINT_MESSAGE_MAX_SIZE 512      // Max message length
#define PRINT_QUEUE_DEPTH 10            // Number of queued messages
#define PRINT_TASK_PRIORITY 3           // Highest app priority
#define PRINT_TASK_STACK_SIZE 512       // Stack in words (2048 bytes)
```

### FreeRTOS Config (FreeRTOSConfig.h)

```c
#define configTOTAL_HEAP_SIZE       (75 * 1024)  // 75 KB heap
#define configTICK_RATE_HZ          1000         // 1ms tick
#define configMAX_PRIORITIES        5            // Priority levels 0-4
#define configUSE_PREEMPTION        1            // Preemptive scheduling
#define configUSE_IDLE_HOOK         1            // Enable WFI sleep
#define configUSE_TIMERS            1            // Software timers
#define configTIMER_TASK_PRIORITY   2            // Timer service priority
```

## 🧪 Testing & Verification

### Tested Scenarios

✅ **Character Echo** - Immediate feedback, no lag
✅ **Menu Navigation** - All menus display completely, no truncation
✅ **LED Patterns** - All 4 patterns work correctly
✅ **Thread Safety** - No text corruption under rapid input
✅ **Error Handling** - Buffer overflow and queue full handled gracefully
✅ **Backspace** - Visual feedback works correctly
✅ **Power Efficiency** - CPU ~98% idle, WFI sleep active

### Performance Metrics

| Metric | Value |
|--------|-------|
| Character Echo Latency | ~50-100μs |
| Command Processing | <6ms |
| CPU Load (Active) | ~2% |
| CPU Load (Idle) | ~98% |
| Average Power | ~22 mA @ 3.3V |
| Heap Utilization | 17% (62 KB free) |

## 🎓 Learning Outcomes

This project demonstrates:

1. **FreeRTOS Task Design** - Multiple cooperating tasks with proper priorities
2. **Queue-Based Communication** - Producer-consumer patterns
3. **Resource Management** - Exclusive ownership vs mutex protection
4. **State Machines** - Menu navigation and command processing
5. **Software Timers** - LED pattern control without blocking
6. **Power Management** - WFI sleep mode in idle task
7. **Professional Practices** - Clean code, documentation, error handling

## 📖 Documentation

### **📚 [Architecture.md](Architecture.md) - Complete Technical Reference**

**1000+ lines of comprehensive documentation including:**

- **Task Architecture** - Detailed analysis of all 5 tasks with priorities, stack sizes, responsibilities
- **FreeRTOS Objects** - Queues, timers, synchronization primitives
- **Data Flow Diagrams** - Complete system communication paths
- **Timing Analysis** - Worst-case response times, latency measurements
- **Memory Maps** - Heap allocation, stack usage, resource utilization
- **Interrupt Configuration** - UART RX, SysTick, priority levels
- **Critical Section Analysis** - Thread safety implementation details
- **Power Management** - WFI sleep mode, power consumption estimates
- **Troubleshooting Guide** - Common issues and solutions
- **Design Decisions** - Evolution from mutex to print task architecture
- **Testing Verification** - Complete test results and validation
- **Performance Metrics** - CPU load, memory usage, response times

**This README provides a quick start guide. For deep technical understanding, refer to Architecture.md.**

## 🚧 Future Enhancements

### Recommended Next Steps

1. **SEGGER RTT Logging**
   - Add dedicated debug logging via SWD (no UART pins needed)
   - SystemView integration for visual task analysis
   - Log task states, queue depths, heap usage

2. **Advanced Features**
   - User-configurable LED patterns stored in flash
   - Command history with up/down arrow support
   - Runtime statistics menu (CPU usage, stack watermarks)

3. **Production Hardening**
   - Watchdog timer integration
   - Stack overflow detection hooks
   - Comprehensive fault handlers with diagnostics

## 🐛 Troubleshooting

### Common Issues

**No output on terminal:**
- Check UART connections (TX ↔ RX, RX ↔ TX)
- Verify baud rate: 115200, 8N1
- Press STM32 RESET button after connecting terminal

**Text corruption:**
- Ensure you flashed the latest firmware
- Print task priority must be 3 (highest)
- Only print task should call HAL_UART_Transmit()

**LEDs not blinking:**
- Verify `led_effects_init()` called before scheduler starts
- Check timer priorities (should be 2)
- Ensure patterns 2 or 3 selected (1 is static ON)

## 📄 License

This project is for educational and personal use. Feel free to learn from, modify, and extend it.

## 🙏 Acknowledgments

- **STMicroelectronics** - STM32 HAL libraries and CMSIS
- **FreeRTOS** - Real-time operating system kernel
- **SEGGER** - SystemView debugging tools

## 📧 Contact

For questions or discussions about this project, please refer to the detailed [Architecture.md](Architecture.md) documentation.

---
