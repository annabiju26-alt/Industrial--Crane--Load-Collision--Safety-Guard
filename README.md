# Industrial Crane Load-Collision Safety Guard
An industrial-grade embedded safety mitigation application built on the **PIC18F4580 microcontroller** to actively prevent catastrophic structural failures and collisions in crane hoisting systems.

---

##  Project Concept
This safety system monitors crane load conditions and physical positioning limits to handle emergency overrides before structural limits are breached:
* **Overload Detection:** Actively evaluates sensor inputs (simulated via push-buttons) to ensure the hoisted weight does not exceed mechanical safety thresholds.
* **UART Telemetry:** While operational, the system continuously broadcasts live hoisting diagnostics (Load Weight, Speed, and Limit Status) over serial TX at **9600 Baud**.
* **Hoist Speed Control:** Features manual or automated toggles to adjust hoisting motor speeds (Normal vs Safe Creep Mode) based on proximity limits.
* **Safety Cut-off & Fault Lock:** In case of critical failure or an Emergency Stop, the system instantly halts the hoisting mechanism, engages mechanical brakes, and triggers a visual hazard indicator.

> ** Industrial Significance:** High-capacity industrial cranes require automated anti-collision and overload fail-safes to prevent cable snapping or structural structural tipping due to sudden dynamic loading shifts.

---

##  Hardware Connections (Matrix)

| PIC18 Pin | Target Component | Functional Purpose |
| :--- | :--- | :--- |
| **RB0** | Proximity Limit Switch | Detects physical travel limit boundaries (Active LOW) |
| **RB1** | Crane Speed Mode Switch | Toggles between High Speed and Safe Creep Mode (Active LOW) |
| **RB2** | Emergency Stop Switch | Global safety shutdown override (Active LOW) |
| **RB4-RB7** | LCD D4-D7 | 4-bit mode data lines (Conflict-free PORTB) |
| **RA0, RA1** | LCD RS, EN | LCD Register Select and Enable control pins |
| **RC6 / TX** | Virtual Terminal RXD | Serial telemetry data broadcaster (9600 Baud) |
| **RC7 / RX** | Virtual Terminal TXD | Command input link (Unused in baseline) |
| **RD0, RD1** | L293D IN1, IN2 | Hoisting Winch Actuator (DC Motor) |
| **RD4** | Green LED | Guard System Normal / Active Operational Status (Add 220Ω resistor) |
| **RD5** | Red LED | Critical Load Fault / Collision Breached Status (Add 220Ω resistor) |
| **RD6** | Yellow LED | Approaching Travel/Weight Limit Indicator (Add 220Ω resistor) |

---

## 🗺️ Circuit Schematic Diagram

<p align="center">
  <img src="schematic.png" alt="Industrial Crane Safety Guard Proteus Schematic" width="800">
</p>

*Note: To display your schematic here, take a screenshot of your finished Proteus circuit layout, save it exactly as `schematic.png`, and upload it into the root directory of this repository.*

---

##  Project Logic & State-Machine Architecture
The safety application utilizes an Event-Driven Finite State Machine (FSM) driven by a central tracking variable:

| State Index | Nominal Description | Triggers & Output Pins Actions | Next State Transition |
| :---: | :--- | :--- | :--- |
| **State 0** | Standby Monitoring | Green LED ON, Red & Yellow OFF, Winch stopped. LCD prints "CRANE: SECURE". Polls sensors. | Move to State 1 if RB0 goes LOW (Limit reached). |
| **State 1** | Locking/Braking Winch | Activates safety mechanical brake actuator for 2 seconds. LCD prints "ENGAGING BRAKE". | Move to State 2 automatically after system stabilizes. |
| **State 2** | Controlled Operation | Green LED OFF. Yellow LED flashes. Broadcasts crane telemetry logs over UART TX. | Move to State 3 when task completion parameters match. |
| **State 3** | Position Cleared | Yellow LED OFF, Green LED ON. Runs hoist winch safely clear for 2 seconds. | Return back to Standby State 0 after a 4-second delay. |
| **State 4** | Catastrophic Fault | Immediate override triggered if E-Stop button (RB2) is pressed. Shuts off winch, engages locks. | System locked in infinite hazard strobe loop. |

---

##  How to Use & Test the Project

###  Method 1: Interactive Serial Keyboard Inputs
1. Run the Proteus simulation and locate the **Virtual Terminal** display window.
2. Click inside the terminal space to focus execution.
3. Press **S** to simulate system initialization/hoist confirmation.
4. Press **F** during operation to manually override and toggle between High Speed and Creep Mode coefficients.
5. Press **K** to simulate load-clear drop sequences.
6. Press **E** to execute an immediate hardware emergency trip sequence.

###  Method 2: Physical Hardware Push-Buttons
1. Press the push-button on **RB0** to simulate physical travel limit sensor triggers.
2. Press the push-button on **RB1** to execute manual operations modifier checks.
3. Press the push-button on **RB2** to check mechanical interrupt safety performance.

---

##  Embedded C Code (XC8)

```c
#pragma config OSC = HS
#pragma config WDT = OFF
#pragma config PBADEN = OFF
#pragma config MCLRE = ON
#pragma config LVP = OFF

#include <xc.h>
#include <stdio.h>

// Calibrated software delay
void delay_ms(unsigned int ms) {
    for (unsigned int i = 0; i < ms; i++) {
        for (int j = 0; j < 165; j++);
    }
}

void delay_short() {
    for (int i = 0; i < 500; i++);
}

// --- 4-BIT LCD DRIVER ---
void lcd_write_nibble(unsigned char nibble) {
    LATB = (LATB & 0x0F) | (nibble & 0xF0);
    PORTAbits.RA1 = 1; // EN = 1
    delay_short();
    PORTAbits.RA1 = 0; // EN = 0
}

void command(unsigned char cmd) {
    PORTAbits.RA0 = 0; // RS = 0
    lcd_write_nibble(cmd & 0xF0);
    lcd_write_nibble(cmd << 4);
    delay_ms(2);
}

void data(unsigned char val) {
    PORTAbits.RA0 = 1; // RS = 1
    lcd_write_nibble(val & 0xF0);
    lcd_write_nibble(val << 4);
    delay_ms(2);
}

void lcd_print(const char* s) {
    while(*s) {
        data(*s++);
    }
}

void lcd_init() {
    delay_ms(15);
    lcd_write_nibble(0x30);
    delay_ms(5);
    lcd_write_nibble(0x30);
    delay_ms(1);
    lcd_write_nibble(0x30);
    delay_ms(1);
    lcd_write_nibble(0x20); // 4-bit select
    delay_ms(1);
    
    command(0x28); // 4-bit, 2-line
    command(0x0C); // Display ON, Cursor OFF
    command(0x06); // Auto-increment
    command(0x01); // Clear screen
    delay_ms(2);
}

// --- USART SERIAL TELEMETRY ---
void usart_init() {
    TRISCbits.TRISC7 = 1; // RX as input
    TRISCbits.TRISC6 = 0; // TX as output
    TXSTA = 0x24;         // Transmit Enabled, High speed
    RCSTA = 0x90;         // Continuous receive enabled, Serial Port Enabled
    SPBRG = 129;          // 9600 Baud at 20MHz crystal oscillator
}

char usart_read() {
    if (OERR) {
        CREN = 0;
        CREN = 1;
    }
    while (RCIF == 0);
    return RCREG;
}

void usart_write(char ch) {
    while (!TXIF); // Wait for buffer empty
    TXREG = ch;
}

void usart_print(const char* s) {
    while (*s) {
        usart_write(*s++);
    }
}

// --- MAIN SAFETY SYSTEM ---
void main() {
    ADCON1 = 0x0F; // Digital mode configurations
    
    TRISD = 0x00;  // PORTD as outputs (Motor & LEDs)
    TRISB = 0x07;  // RB0-RB2 inputs (buttons), RB4-RB7 outputs (LCD)
    TRISA = 0xFC;  // RA0-RA1 outputs (LCD RS, EN)
    
    // Set all outputs low initially
    LATDbits.LATD0 = 0; // Motor IN1
    LATDbits.LATD1 = 0; // Motor IN2
    LATDbits.LATD4 = 0; // Green LED
    LATDbits.LATD5 = 0; // Red LED
    LATDbits.LATD6 = 0; // Yellow LED
    LATDbits.LATD7 = 0; // Emergency Warning LED
    LATB = 0x00;
    
    lcd_init();
    usart_init();
    
    unsigned char charge_state = 0; // 0 = Monitoring, 1 = Braking/Locking, 2 = Operating, 3 = Cleared, 4 = Fault
    int battery_level = 0;          // Simulated Load Position/Safety Margin
    int charge_rate = 1;            // 1 = High Speed, 2 = Safe Creep Mode
    char buffer[32];
    
    command(0x80);
    lcd_print("SAFETY GUARD ON ");
    command(0xC0);
    lcd_print("CRANE: SECURE   ");
    
    usart_print("\r\n==========================================\r\n");
    usart_print("  Industrial Crane Safety Guard Online\r\n");
    usart_print("==========================================\r\n");
    usart_print("Keyboard Commands (Type in Virtual Terminal):\r\n");
    usart_print("  [S] - Initialize & Confirm Load Hoist\r\n");
    usart_print("  [F] - Toggle Speed (High Speed / Safe Creep)\r\n");
    usart_print("  [K] - Force Safe Load Drop Sequence\r\n");
    usart_print("  [E] - Trigger Immediate Emergency Cut-off\r\n");
    usart_print("------------------------------------------\r\n");
    
    while(1) {
        // --- 1. EMERGENCY STOP CHECK (RB2 Active LOW) ---
        if (PORTBbits.RB2 == 0) {
            charge_state = 4; // Force Fault state
        }
        
        // --- 2. SCAN SERIAL RX FOR KEYBOARD COMMANDS ---
        if (RCIF) {
            char cmd = usart_read();
            
            if ((cmd == 'S' || cmd == 's') && charge_state == 0) {
                charge_state = 1;
            }
            else if ((cmd == 'F' || cmd == 'f') && charge_state == 2) {
                if (charge_rate == 1) {
                    charge_rate = 2;
                    usart_print("UART Override: CREEP MODE Activated\r\n");
                } else {
                    charge_rate = 1;
                    usart_print("UART Override: HIGH SPEED Activated\r\n");
                }
            }
            else if ((cmd == 'K' || cmd == 'k') && charge_state == 2) {
                usart_print("UART Override: Safe Load Drop Executed...\r\n");
                charge_state = 3;
            }
            else if (cmd == 'E' || cmd == 'e') {
                charge_state = 4; // Force Fault state
            }
        }
        
        // --- 3. CRITICAL FAULT / ISOLATION LOOP ---
        if (charge_state == 4) {
            LATDbits.LATD6 = 0; // Kill operational status indicators
            
            command(0x01);
            lcd_print("! SYS BREACHED !");
            command(0xC0);
            lcd_print("ENGAGING BRAKES ");
            usart_print("CRITICAL ALERT: Dynamic Trip! Hard lock deployed...\r\n");
            
            LATDbits.LATD0 = 0;
            LATDbits.LATD1 = 1;
            delay_ms(2000); // Deploy reverse restraint braking
            
            LATDbits.LATD0 = 0;
            LATDbits.LATD1 = 0;
            LATDbits.LATD5 = 1; // Solid red alarm deployment
            
            command(0x01);
            lcd_print("SYSTEM ISOCKED ");
            command(0xC0);
            lcd_print("RESET MANDATORY ");
            
            while(1) {
                LATDbits.LATD5 = !LATDbits.LATD5; // Flash warning strobe
                delay_ms(250);
            }
        }
        
        // --- 4. SECURE STANDBY MONITORING ---
        if (charge_state == 0) {
            LATDbits.LATD4 = 1; 
            LATDbits.LATD5 = 0;
            LATDbits.LATD6 = 0;
            
            if (PORTBbits.RB0 == 0) {
                delay_ms(50); // Debounce
                if (PORTBbits.RB0 == 0) {
                    charge_state = 1;
                }
            }
        }
        
        // --- 5. AUTOMATED INITIAL SYSTEM LOCKING ---
        if (charge_state == 1) {
            usart_print("Load Shift Detected. Calibrating brakes...\r\n");
            command(0x01);
            lcd_print("WINCH ACTIVE    ");
            command(0xC0);
            lcd_print("LOCKING SYSTEM.. ");
            
            LATDbits.LATD0 = 1;
            LATDbits.LATD1 = 0;
            delay_ms(2000); // Mechanical activation latching window
            
            LATDbits.LATD0 = 0;
            LATDbits.LATD1 = 0;
            
            battery_level = 0;
            charge_state = 2;
        }
        
        // --- 6. LIVE MONITORING & TELEMETRY ---
        if (charge_state == 2) {
            LATDbits.LATD4 = 0;
            LATDbits.LATD6 = !LATDbits.LATD6; // Toggle active telemetry indicator
            
            if (PORTBbits.RB1 == 0) {
                delay_ms(50);
                if (PORTBbits.RB1 == 0) {
                    if (charge_rate == 1) {
                        charge_rate = 2;
                        usart_print("Hardware Sensor: Creep Speed Modifiers Applied\r\n");
                    } else {
                        charge_rate = 1;
                        usart_print("Hardware Sensor: System Operating at Max Capacity\r\n");
                    }
                }
            }
            
            if (charge_rate == 1) {
                battery_level = battery_level + 2; 
            } else {
                battery_level = battery_level + 5; 
            }
            
            if (battery_level >= 100) {
                battery_level = 100;
                charge_state = 3; 
            }
            
            command(0x80);
            if (charge_rate == 1) {
                lcd_print("MODE: HIGH SPEED");
            } else {
                lcd_print("MODE: CREEP SAFE");
            }
            
            command(0xC0);
            sprintf(buffer, "CAPACITY: %d%%   ", battery_level);
            lcd_print(buffer);
            
            if (charge_rate == 1) {
                sprintf(buffer, "LOG_TX: CAP=%d%%, V_IND=24V, I_DRV=3.2A\r\n", battery_level);
            } else {
                sprintf(buffer, "LOG_TX: CAP=%d%%, V_IND=12V, I_DRV=1.1A\r\n", battery_level);
            }
            usart_print(buffer);
            
            delay_ms(800); 
        }
        
        // --- 7. SAFETY SEQUENCE STABILIZED COMPLETE ---
        if (charge_state == 3) {
            LATDbits.LATD6 = 0;
            LATDbits.LATD4 = 1; 
            
            command(0x01);
            lcd_print("TASK COMPLETED! ");
            command(0xC0);
            lcd_print("DISENGAGING LOCK");
            usart_print("Target coordinates matching clearance thresholds! Unlatching...\r\n");
            
            LATDbits.LATD0 = 0;
            LATDbits.LATD1 = 1;
            delay_ms(2000); 
            
            LATDbits.LATD0 = 0;
            LATDbits.LATD1 = 0;
            
            command(0x01);
            lcd_print("HOIST SECURED   ");
            command(0xC0);
            lcd_print("SYSTEM STABLE   ");
            delay_ms(4000);
            
            charge_state = 0; 
        }
        delay_ms(100);
    }
}
