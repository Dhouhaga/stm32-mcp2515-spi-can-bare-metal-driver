# stm32-mcp2515-spi-can-bare-metal-driver
STM32F407 bare-metal CAN bus driver for MCP2515 controller via SPI. Features direct register manipulation, external interrupt handling, NVIC configuration, and real-time message processing. No HAL, no middleware. Demonstrates CAN 2.0A protocol and SPI master implementation from scratch.

## Demo

**Note:** We can also access the registers' content via Keil uVision's debugging tool to verify what's happening bit by bit (SPI1->SR, SPI1->DR, GPIOD->ODR, etc.)

https://github.com/user-attachments/assets/bec745b5-f486-4ed3-8638-c300642418a5


- Left board sends "hello" → TX LED blinks  
- Right board receives → RX LED flashes, sends "what?" → TX LED flashes
- Left board receives → RXLED flashes, sends "hello" again ...  
## Technical Stack

- MCU: STM32F407 (ARM Cortex-M4, 168MHz)
- CAN Controller: Microchip MCP2515
- Interface: SPI (Mode 0,0, Master, 8-bit)
- Bit Rate: 125 kbps
- Frame Format: CAN 2.0A (11-bit identifier)

## Hardware Pin Mapping

| STM32F407 | MCP2515 | Function |
|-----------|---------|----------|
| PA4 | CS | Chip Select |
| PA5 | SCK | SPI Clock |
| PA6 | MISO | SPI Data In |
| PA7 | MOSI | SPI Data Out |
| PA0 | INT | External Interrupt |
| PD12 | - | TX LED (Green) |
| PD13 | - | RX LED (Blue) |
| PD14 | - | Error LED (Red) |
| PD15 | - | Status LED (Yellow) |

## CAN Bit Timing (125kbps @ 8MHz)

- CNF1: 0x03 (BRP=4, SJW=1)
- CNF2: 0x90 (PS1=4, Prop=1, PHSEG1=4)
- CNF3: 0x02 (PHSEG2=3)
- Sample Point: 68%

## MCP2515 Commands Implemented
0xC0 - Reset
0x03 - Read
0x02 - Write
0x81 - Request to Send (TXB0)
0x90 - Read RX Buffer

## Register Configuration Summary

- CANCTRL: Configuration mode (0x80) / Normal mode (0x00)
- CANINTE: RX0IE | TX0IE | ERRIE
- RXB0CTRL: 0x60 (Receive all messages, enable buffer)
- TXB0: Standard identifier 0x00, DLC, 8-byte payload

## SPI Master Configuration

- Clock Polarity (CPOL): 0
- Clock Phase (CPHA): 0
- Baud Rate: fPCLK / 256
- Master mode enabled
- Software slave management (SSM=1, SSI=1)

## Interrupt Architecture
PA0 (INT) -> EXTI0 (falling edge) -> NVIC (priority 0) -> ISR

Interrupts handled in ISR:
- RX0IF: Read message from RX buffer, parse content, trigger response
- TX0IF: Clear transmission flag, turn off TX LED
- ERRIF: Error condition (reserved for fault handling)

## Message Protocol

| Received | Transmitted |
|----------|-------------|
| "hello" (5 bytes) | "what?" (5 bytes) |
| "what?" (5 bytes) | "hello" (5 bytes) |

## Bare-Metal Implementation Details

No HAL functions used. All peripherals configured via register writes:

- RCC->AHB1ENR: GPIO clock enable
- RCC->APB2ENR: SPI1 and SYSCFG clock enable
- GPIOx->MODER, OTYPER, OSPEEDR, PUPDR, AFR: Pin muxing
- SPI1->CR1, CR2: SPI timing and mode
- EXTI->FTSR, RTSR, IMR, PR: External interrupt
- NVIC_SetPriority, NVIC_EnableIRQ: Interrupt controller

## LED State Machine

| Condition | LED Action |
|-----------|-------------|
| Boot success | Status blinks twice |
| Boot failure | Error blinks 5 times |
| Transmission start | TX LED ON |
| Transmission complete | TX LED OFF |
| Message received | RX LED ON for 1 second |
| Response sent | RX LED OFF after processing |

## Build Dependencies

- ARM GCC toolchain (arm-none-eabi)
- STM32F407xx.h (CMSIS header)
- MCP2515 external hardware

## Test Setup

Two STM32F407 nodes required:
1. Node A: Sends "hello"
2. Node B: Receives, sends "what?"
3. Node A: Receives "what?", sends "hello"

## Skills Demonstrated

- CAN 2.0A protocol implementation
- SPI master driver from scratch
- External interrupt handling with NVIC
- Real-time message processing in ISR context
- Register-level STM32 programming
- Embedded debugging with GPIO indicators
- CAN bit timing calculation and configuration

## Target Applications

- Automotive ECU communication
- Industrial automation
- Robotics inter-processor bus
- Telematics and data logging
- Battery management systems (BMS)
