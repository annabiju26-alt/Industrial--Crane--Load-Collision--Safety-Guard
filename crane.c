#include <xc.h>
#include <stdio.h>
#include <stdint.h>

// --- CONFIGURATION BITS ---
#pragma config OSC = IRCIO67    // Internal oscillator block
#pragma config WDT = OFF        // Watchdog Timer Disabled
#pragma config PBADEN = OFF     // PORTB<4:0> digital I/O on Reset
#pragma config LVP = OFF        // Single-Supply ICSP Disabled

// --- SYSTEM DEFINITIONS ---
#define _XTAL_FREQ 4000000      // 4 MHz Internal Clock

// Pin Definitions
#define LCD_RS      LATAbits.LATA1
#define LCD_EN      LATAbits.LATA2
#define LCD_DATA    LATC

#define MOTOR_EN    LATDbits.LATD0
#define LED_GREEN   LATDbits.LATD1
#define LED_YELLOW  LATDbits.LATD2
#define LED_RED     LATDbits.LATD3
#define BUZZER      LATDbits.LATD4

#define RESET_BTN   PORTBbits.RB1

// Volatile global flag for ISR tracking (Fixed syntax error)
volatile uint8_t collision_flag = 0;

// --- LCD FUNCTIONS ---
void lcd_cmd(unsigned char cmd) {
    LCD_DATA = cmd;
    LCD_RS = 0; // Command Mode
    LCD_EN = 1;
    __delay_ms(2);
    LCD_EN = 0;
}

void lcd_char(unsigned char data) {
    LCD_DATA = data;
    LCD_RS = 1; // Data Mode
    LCD_EN = 1;
    __delay_us(40);
    LCD_EN = 0;
}

void lcd_init(void) {
    __delay_ms(20);
    lcd_cmd(0x38); // 8-bit mode, 2 lines, 5x7 matrix
    lcd_cmd(0x0C); // Display ON, Cursor OFF
    lcd_cmd(0x06); // Auto-increment cursor
    lcd_cmd(0x01); // Clear display
    __delay_ms(2);
}

void lcd_str(const char* str) {
    while(*str) {
        lcd_char(*str++);
    }
}

void lcd_clear(void) {
    lcd_cmd(0x01);
    __delay_ms(2);
}

// --- PERIPHERAL INITIALIZATION ---
void init_system(void) {
    // Port Directions
    TRISA = 0x01; // RA0 is Input (ADC), RA1-RA2 are Outputs (LCD Control)
    TRISB = 0x03; // RB0 (INT0) and RB1 (Reset Button) as inputs
    TRISC = 0x00; // PORTC as LCD Data outputs
    TRISD = 0x00; // PORTD as Control Outputs (Motor, LEDs, Buzzer)
    
    // Clear Outputs initially
    LATC = 0x00;
    LATD = 0x00;
    
    // ADC Setup
    ADCON1 = 0x0E; // AN0 Analog, rest digital channels VREF=VSS/VDD
    ADCON2 = 0x2A; // Left justified, 12 TAD, FOSC/32
    ADCON0 = 0x01; // Channel 0 select, Turn on ADC module
    
    // Interrupt Configuration
    INTCON2bits.INTEDG0 = 0; // Trigger INT0 on Falling Edge (Bumper press)
    INTCONbits.INT0IF = 0;   // Clear INT0 Flag
    INTCONbits.INT0IE = 1;   // Enable INT0 External Interrupt
    INTCONbits.GIE = 1;      // Enable Global Interrupts
}

// --- READ ADC CHANNEL 0 ---
unsigned char read_adc(void) {
    ADCON0bits.GO_DONE = 1; // Start conversion
    while(ADCON0bits.GO_DONE); // Wait for conversion to complete
    return ADRESH; // Return 8-bit MSB result (Left justified format)
}

// --- HIGH PRIORITY INTERRUPT SERVICE ROUTINE (Fixed XC8 Syntax Error) ---
void __interrupt(high_priority) high_isr(void) {
    if(INTCONbits.INT0IF) {
        MOTOR_EN = 0;       // Cut off motor outputs immediately inside ISR
        collision_flag = 1; // Set emergency global state flag
        INTCONbits.INT0IF = 0; // Clear interrupt flag
    }
}

// --- MAIN CONTROL LOOP ---
void main(void) {
    unsigned char adc_val = 0;
    unsigned int load_pct = 0; // Changed to unsigned int for robust scaling math
    unsigned char warning_toggle = 0; 
    char buffer[16];
    
    init_system();
    lcd_init();
    
    while(1) {
        // --- COLLISION STOP STATE (ISR LOCKED RAILS) ---
        if(collision_flag) {
            lcd_cmd(0x80);
            lcd_str("CRITICAL COLLISION");
            lcd_cmd(0xC0);
            lcd_str("SYSTEM HALTED!  ");
            
            // Challenge Requirement: Alternating warning strobe using 0xCC and 0x33 patterns
            LATD = 0xCC; 
            __delay_ms(250);
            LATD = 0x33; 
            __delay_ms(250);
            
            // Continuously scan for dedicated Active-Low Reset Switch (RB1)
            if(RESET_BTN == 0) {
                __delay_ms(50); // Debounce delay
                if(RESET_BTN == 0) {
                    collision_flag = 0; // Drop collision system latch
                    LATD = 0x00;        // Clear strobe lines
                    lcd_clear();
                }
            }
            continue; // Keep system trapped in emergency logic until reset occurs
        }
        
        // --- NORMAL SCANNING WORKING LOOP ---
        adc_val = read_adc();
        load_pct = ((unsigned int)adc_val * 100) / 255; // Explicit type-casting for safe multiplication
        
        // Output Current Load Metrics on Row 1
        sprintf(buffer, "LOAD: %u%%      ", load_pct);
        lcd_cmd(0x80);
        lcd_str(buffer);
        
        lcd_cmd(0xC0); // Relocate Cursor to Row 2
        
        if(load_pct <= 70) {
            // State 1: Normal Load operation (0% to 70%)
            lcd_str("STATUS: SAFE    ");
            LED_GREEN  = 1;
            LED_YELLOW = 0;
            LED_RED    = 0;
            BUZZER     = 0;
            MOTOR_EN   = 1; // Ensure hoist motor can run
            __delay_ms(100);
        } 
        else if(load_pct > 70 && load_pct <= 90) {
            // State 2: Heavy Load Warning (71% to 90%)
            lcd_str("STATUS: WARNING ");
            LED_GREEN  = 0;
            LED_RED    = 0;
            MOTOR_EN   = 1; // Hoist control output remains enabled
            
            // Flashing warning toggle logic using software delay
            warning_toggle = !warning_toggle;
            LED_YELLOW = warning_toggle;
            BUZZER = warning_toggle; // Slow chirping signal
            __delay_ms(300); 
        } 
        else {
            // State 3: Structural Overload (Above 90%)
            lcd_str("OVERLOAD - STOP!");
            LED_GREEN  = 0;
            LED_YELLOW = 0;
            LED_RED    = 1;
            BUZZER     = 1;   // Continuous sounding buzzer
            MOTOR_EN   = 0;   // Hoist motor instantly cut off (forced low)
            __delay_ms(100);
        }
    }
}
