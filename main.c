#include <stm32f407xx.h>

// MCP2515 Register Definitions
#define MCP2515_CMD_RESET          0xC0
#define MCP2515_CMD_READ           0x03
#define MCP2515_CMD_WRITE          0x02
#define MCP2515_CMD_RTS_TXB0       0x81
#define MCP2515_CMD_RTS_TXB1       0x82
#define MCP2515_CMD_RTS_TXB2       0x84
#define MCP2515_CMD_READ_RX_BUFFER 0x90

// MCP2515 Control Registers
#define MCP2515_REG_CANCTRL        0x0F
#define MCP2515_REG_CANINTE        0x2B
#define MCP2515_REG_CANINTF        0x2C

// MCP2515 Configuration Registers
#define MCP2515_REG_CNF1           0x2A
#define MCP2515_REG_CNF2           0x29
#define MCP2515_REG_CNF3           0x28

// MCP2515 Receive Buffer Control Registers
#define MCP2515_REG_RXB0CTRL       0x60
#define MCP2515_REG_RXB1CTRL       0x70

// MCP2515 Transmit Buffer Registers
#define MCP2515_REG_TXB0CTRL       0x30
#define MCP2515_REG_TXB0SIDH       0x31
#define MCP2515_REG_TXB0SIDL       0x32
#define MCP2515_REG_TXB0DLC        0x35
#define MCP2515_REG_TXB0D0         0x36

// MCP2515 Mode Definitions
#define MCP2515_MODE_CONFIG        0x80
#define MCP2515_MODE_NORMAL        0x00

// MCP2515 Interrupt Flags
#define MCP2515_INTF_RX0IF         0x01
#define MCP2515_INTF_RX1IF         0x02
#define MCP2515_INTF_TX0IF         0x04
#define MCP2515_INTF_TX1IF         0x08
#define MCP2515_INTF_TX2IF         0x10
#define MCP2515_INTF_ERRIF         0x20
#define MCP2515_INTF_WAKIF         0x40
#define MCP2515_INTF_MERRF         0x80

// MCP2515 Interrupt Enable Flags
#define MCP2515_INTE_RX0IE         0x01
#define MCP2515_INTE_RX1IE         0x02
#define MCP2515_INTE_TX0IE         0x04
#define MCP2515_INTE_TX1IE         0x08
#define MCP2515_INTE_TX2IE         0x10
#define MCP2515_INTE_ERRIE         0x20
#define MCP2515_INTE_WAKIE         0x40
#define MCP2515_INTE_MERRE         0x80

// MCP2515 Buffer Control Settings
#define MCP2515_RXBCTRL_RXMODE_ALL 0x60

// LED Definitions
#define LED_TX_COMPLETE    (1 << 12)  // PD12 - Green
#define LED_RX_MESSAGE     (1 << 13)  // PD13 - Blue
#define LED_ERROR          (1 << 14)  // PD14 - Red
#define LED_STATUS         (1 << 15)  // PD15 - Yellow

// Global variables
volatile uint8_t can_rx_buffer[8];
volatile uint8_t can_rx_length = 0;
volatile uint8_t can_tx_in_progress = 0;
volatile uint32_t interrupt_count = 0;
volatile uint8_t last_interrupt_type = 0;

// Function Prototypes
void GPIO_Init(void);
void SPI_Init(void);

void SPI_Write(uint8_t data);
uint8_t SPI_Read(void);
void SS_LOW(void);
void SS_HIGH(void);

void MCP2515_Init(void);
void MCP2515_Write(uint8_t address, uint8_t data);
uint8_t MCP2515_Read(uint8_t address);
void MCP2515_Send(uint8_t *data, uint8_t length);
void MCP2515_Interrupt_Init(void);

void EXTI0_IRQHandler(void);

void Blink_LED(uint32_t led, uint8_t times, uint16_t delay_ms);

// Init
void GPIO_Init(void){
    // Enable GPIO clocks
    RCC->AHB1ENR |= (1 << 0) | (1 << 3); // GPIOA and GPIOD
    
    // PA5=SCK, PA6=MISO, PA7=MOSI (SPI1)
    GPIOA->MODER &= ~((0x3<<10) | (0x3<<12) | (0x3<<14));
    GPIOA->MODER |=  ((0x2<<10) | (0x2<<12) | (0x2<<14));
    GPIOA->OTYPER &= ~((1<<5)|(1<<6)|(1<<7));
    GPIOA->OSPEEDR |= ((0x3<<10) | (0x3<<12) | (0x3<<14));
    GPIOA->PUPDR |= ((0x1<<10) | (0x1<<12) | (0x1<<14)); // Pull-up
    GPIOA->AFR[0] &= ~((0xF<<20) | (0xF<<24) | (0xF<<28));
    GPIOA->AFR[0] |=  ((5<<20) | (5<<24) | (5<<28));

    // PA4 as CS
    GPIOA->MODER &= ~(0x3 << (4*2));
    GPIOA->MODER |=  (0x1 << (4*2)); // Output
    GPIOA->BSRR = (1<<4); // CS high initially
    
    // PA0 as input for MCP2515 interrupt
    GPIOA->MODER &= ~(0x3 << 0);  // Input mode
    GPIOA->PUPDR &= ~(0x3 << 0);
    GPIOA->PUPDR |= (0x1 << 0);   // Pull-up
    
    // GPIOD for LEDs - PD12-15
    // PD12: TX Complete (Green)
    // PD13: RX Message (Blue)
    // PD14: Error/Status (Red)
    // PD15: System Status (Yellow)
    GPIOD->MODER &= ~((0x3 << 24) | (0x3 << 26) | (0x3 << 28) | (0x3 << 30));
    GPIOD->MODER |=  ((0x1 << 24) | (0x1 << 26) | (0x1 << 28) | (0x1 << 30));
    GPIOD->OTYPER &= ~((1<<12) | (1<<13) | (1<<14) | (1<<15));
    
    // All LEDs off initially
    GPIOD->ODR &= ~(LED_TX_COMPLETE | LED_RX_MESSAGE | LED_ERROR | LED_STATUS);
}
void SPI_Init(void){
    RCC->APB2ENR |= (1 << 12); // SPI1 enable
    
    // Disable SPI first
    SPI1->CR1 &= ~SPI_CR1_SPE;
    
    // Clear any pending flags
    volatile uint8_t temp = SPI1->DR;
    temp = SPI1->SR;
    
    // Configure SPI1
    SPI1->CR1 = (0 << 11) |     // 8-bit data
                (0 << 10) |     // Full duplex
                (0 << 7)  |     // MSB first
                (1 << 9)  |     // SSM = 1
                (1 << 8)  |     // SSI = 1
                (0b111 << 3) |  // BR = fPCLK/256
                (1 << 2)  |     // Master mode
                (0 << 1)  |     // CPOL = 0
                (0 << 0);       // CPHA = 0
    
    // Enable SPI
    SPI1->CR1 |= SPI_CR1_SPE;
}



// slave select
void SS_LOW(void) { GPIOA->BSRR = (1 << (4 + 16)); }
void SS_HIGH(void) { GPIOA->BSRR = (1 << 4); }

// MCP2515 
void MCP2515_Init(void) {
    // Reset MCP2515
    SS_LOW();
    SPI_Write(MCP2515_CMD_RESET);
    SS_HIGH();
    
    // Wait for oscillator stabilization 
    for(volatile int i = 0; i < 20000; i++);
    
    // Enter configuration mode
    MCP2515_Write(MCP2515_REG_CANCTRL, MCP2515_MODE_CONFIG);
    
    // Verify we're in config mode
    for(int retry = 0; retry < 10; retry++) {
        uint8_t mode = MCP2515_Read(MCP2515_REG_CANCTRL);
        if((mode & 0xE0) == MCP2515_MODE_CONFIG) {
            break;
        }
        for(volatile int i = 0; i < 1000; i++);
    }
    
    // Configure CAN bus timing (125kbps at 8MHz)
    MCP2515_Write(MCP2515_REG_CNF1, 0x03);
    MCP2515_Write(MCP2515_REG_CNF2, 0x90);
    MCP2515_Write(MCP2515_REG_CNF3, 0x02);
    
    // Configure receive buffers
    MCP2515_Write(MCP2515_REG_RXB0CTRL, 0x60); // Receive all, enable buffer
    MCP2515_Write(MCP2515_REG_RXB1CTRL, 0x60);
    
    // Enable interrupts 
    MCP2515_Write(MCP2515_REG_CANINTE, 
                  MCP2515_INTE_RX0IE | 
                  MCP2515_INTE_TX0IE | 
                  MCP2515_INTE_ERRIE);
    
    // Clear ALL interrupt flags
    MCP2515_Write(MCP2515_REG_CANINTF, 0x00);
    
    // Enter normal mode
    MCP2515_Write(MCP2515_REG_CANCTRL, MCP2515_MODE_NORMAL);
    
    // Verify normal mode
    for(int retry = 0; retry < 10; retry++) {
        uint8_t mode = MCP2515_Read(MCP2515_REG_CANCTRL);
        if((mode & 0xE0) == MCP2515_MODE_NORMAL) {
            Blink_LED(LED_STATUS, 2, 50); // Success blink
            return;
        }
        for(volatile int i = 0; i < 10000; i++);
    }
    
    // Failed to enter normal mode
    while(1) {
        Blink_LED(LED_ERROR, 5, 200);
    }
}

void MCP2515_Interrupt_Init(void) {
    // Enable SYSCFG clock
    RCC->APB2ENR |= (1 << 14);
    
    // Connect PA0 to EXTI0
    SYSCFG->EXTICR[0] &= ~(0xF << 0);
    SYSCFG->EXTICR[0] |= (0x0 << 0); // PA0
    
    // Configure EXTI0 for falling edge
    EXTI->FTSR |= (1 << 0);
    EXTI->RTSR &= ~(1 << 0); // Disable rising edge
    
    // Enable EXTI0 interrupt
    EXTI->IMR |= (1 << 0);
    
    // Clear any pending interrupt
    EXTI->PR = (1 << 0);
    
    // Configure NVIC for EXTI0 (highest priority)
    NVIC_SetPriority(EXTI0_IRQn, 0x00);
    NVIC_EnableIRQ(EXTI0_IRQn);
}


void MCP2515_Write(uint8_t address, uint8_t data) {
    SS_LOW();
    SPI_Write(MCP2515_CMD_WRITE);
    SPI_Write(address);
    SPI_Write(data);
    SS_HIGH();
}

uint8_t MCP2515_Read(uint8_t address) {
    uint8_t data;
    SS_LOW();
    SPI_Write(MCP2515_CMD_READ);
    SPI_Write(address);
    data = SPI_Read();
    SS_HIGH();
    return data;
}

void MCP2515_Send(uint8_t *data, uint8_t length) {
    // Turn on TX LED (PD12 - Green) when starting transmission
    GPIOD->ODR |= LED_TX_COMPLETE;
    can_tx_in_progress = 1;
    
    // Load message into transmit buffer
    MCP2515_Write(MCP2515_REG_TXB0SIDH, 0x00);
    MCP2515_Write(MCP2515_REG_TXB0SIDL, 0x00);
    MCP2515_Write(MCP2515_REG_TXB0DLC, length & 0x0F);
    
    for(int i = 0; i < length && i < 8; i++) {
        MCP2515_Write(MCP2515_REG_TXB0D0 + i, data[i]);
    }
    
    // Request to send
    SS_LOW();
    SPI_Write(MCP2515_CMD_RTS_TXB0);
    SS_HIGH();
}


void Blink_LED(uint32_t led, uint8_t times, uint16_t delay_ms) {
    for(uint8_t i = 0; i < times; i++) {
        GPIOD->ODR |= led;
        for(volatile int j = 0; j < delay_ms * 1000; j++);
        GPIOD->ODR &= ~led;
        for(volatile int j = 0; j < delay_ms * 1000; j++);
    }
}



// SPI
uint8_t SPI_Read(void) {
    // Wait until TX buffer is empty
    while(!(SPI1->SR & SPI_SR_TXE));
    
    // Send dummy byte to generate clock
    SPI1->DR = 0;
    
    // Wait until data is received
    while(!(SPI1->SR & SPI_SR_RXNE));
    
    return SPI1->DR;
}


void SPI_Write(uint8_t data) {
    // Wait until TX buffer is empty
    while(!(SPI1->SR & SPI_SR_TXE));
    
    // Send data
    SPI1->DR = data;
    
    // Wait until transmission completes
    while(SPI1->SR & SPI_SR_BSY);
    
    // Clear RX buffer (vol prev comp opt)
		volatile uint8_t temp = SPI1->DR;
}


void EXTI0_IRQHandler(void) {
    if(EXTI->PR & (1 << 0)) {
        interrupt_count++;
        
        // Clear EXTI flag FIRST
        EXTI->PR = (1 << 0);
        
        // Read interrupt flags
        uint8_t intf = MCP2515_Read(MCP2515_REG_CANINTF);
        
        // Handle RX interrupt first
        if(intf & MCP2515_INTF_RX0IF) {
            // Turn on RX LED (PD13 - Blue) for receipt indication
            GPIOD->ODR |= LED_RX_MESSAGE;
            
            // Read message from RX buffer 0
            SS_LOW();
            SPI_Write(MCP2515_CMD_READ_RX_BUFFER);
            
            // Skip identifier (4 bytes)
            SPI_Read(); SPI_Read(); SPI_Read(); SPI_Read();
            
            // Read DLC
            can_rx_length = SPI_Read() & 0x0F;
            
            // Read data
            for(uint8_t i = 0; i < can_rx_length && i < 8; i++) {
                can_rx_buffer[i] = SPI_Read();
            }
            SS_HIGH();
            
            // Clear RX interrupt flag immediately
            MCP2515_Write(MCP2515_REG_CANINTF, intf & ~MCP2515_INTF_RX0IF);
            
            // Check message and send response
            if(can_rx_length >= 5) {
                uint8_t is_hello = 1;
                uint8_t is_what = 1;
                
                // Check for "hello"
                if(can_rx_buffer[0] != 'h') is_hello = 0;
                if(can_rx_buffer[1] != 'e') is_hello = 0;
                if(can_rx_buffer[2] != 'l') is_hello = 0;
                if(can_rx_buffer[3] != 'l') is_hello = 0;
                if(can_rx_buffer[4] != 'o') is_hello = 0;
                
                // Check for "what?"
                if(can_rx_buffer[0] != 'w') is_what = 0;
                if(can_rx_buffer[1] != 'h') is_what = 0;
                if(can_rx_buffer[2] != 'a') is_what = 0;
                if(can_rx_buffer[3] != 't') is_what = 0;
                if(can_rx_buffer[4] != '?') is_what = 0;
                
                // BIGGER delay for RX LED on (1000ms = 1 second)
                for(volatile uint32_t i = 0; i < 900000; i++);
                
                // Turn off RX LED
                GPIOD->ODR &= ~LED_RX_MESSAGE;
                
                // Send response based on message
                if(is_hello) {
                    uint8_t response[] = "what?";
                    MCP2515_Send(response, 5);
                }
                else if(is_what) {
                    uint8_t response[] = "hello";
                    MCP2515_Send(response, 5);
                }
            } else {
                // Not our message, just turn off RX LED after short delay
                for(volatile uint32_t i = 0; i < 50000; i++);
                GPIOD->ODR &= ~LED_RX_MESSAGE;
            }
        }
        
        // Handle TX interrupt
        if(intf & MCP2515_INTF_TX0IF) {
            // MEDIUM TX completion indication (300ms)
            for(volatile uint32_t i = 0; i < 500000; i++);
            
            // Turn off TX LED
            GPIOD->ODR &= ~LED_TX_COMPLETE;
            can_tx_in_progress = 0;
            
            // Clear TX interrupt flag
            MCP2515_Write(MCP2515_REG_CANINTF, intf & ~MCP2515_INTF_TX0IF);
        }
    }
}


int main(void) {
    GPIO_Init();
    SPI_Init();
    
    // Startup LED sequence
    Blink_LED(LED_STATUS | LED_TX_COMPLETE | LED_RX_MESSAGE, 3, 150);
    
    // Initialize MCP2515
    MCP2515_Init();
    
    // Initialize interrupts
    MCP2515_Interrupt_Init();
    
    // Verify communication
    uint8_t test_read = MCP2515_Read(MCP2515_REG_CANCTRL);
    if((test_read & 0xE0) != MCP2515_MODE_NORMAL) {
        // Error - flash error LED
        while(1) {
            Blink_LED(LED_ERROR, 5, 200);
        }
    }
    
    // delay let other node initialize
    for(volatile int i = 0; i < 1000000; i++);
    
    // Send initial message
    uint8_t init_msg[] = "hello";
    MCP2515_Send(init_msg, 5);
    while(1) {}
}
