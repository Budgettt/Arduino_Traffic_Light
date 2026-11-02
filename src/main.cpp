#include <avr/io.h>
#include <avr/interrupt.h>
#include "util/delay.h"
#include <stdlib.h>

// Bitwise Operations
#define bitSet(reg, n) (reg |= 1 << n)
#define bitClear(reg, n) (reg &= ~(1 <<n))
#define bitCheck(reg, n) (reg & 1 << n)
#define bitInverse(reg, n) (reg ^= (1 << n))

// LEDs
#define led_green PB0
#define led_yellow PB1
#define led_red PB2
#define led_port PORTB
#define led_DDR DDRB
#define LED_DURATION 400
#define GREEN 0
#define YELLOW 1
#define RED 2
int Tcycle_pwmled = 10;
float duty_cycle = 0.01;
int led_colour = 0;

// Photocell
#define photocell_pin MUX1
#define ADC_LOW 50
#define ADC_HIGH 1000
#define DC_LOW 0.1
#define DC_HIGH 0.9
int adc = 0;

// Sonar
#define trigger_pin PC0
#define echo_pin PC1
#define sonar_DDR DDRC
#define sonar_port PORTC
#define sonar_pin PINC
#define VEL_SOUND 343
#define DETECT_RANGE_CLOSE 10
#define DETECT_RANGE_FAR 80
float Dmm = 0;
uint16_t cnt = 0;
uint16_t timeout = 30000;

// Buzzer
#define buzzer_pin PC3
#define buzzer_DDR DDRC
#define buzzer_port PORTC

// Buttons
#define button_mode_pin PB5
#define button_mode_DDR DDRB
#define button_mode_port PORTB
bool button_mode_status;
bool button_mode_status_old = 1;
#define button_colour_pin PD7
#define button_colour_DDR DDRD
#define button_colour_port PORTD
bool button_colour_status;
bool button_colour_status_old = 1;
#define button_emergency_pin PD3
#define button_emergency_DDR DDRD
#define button_emergency_port PORTD
bool button_emergency_status;
bool button_emergency_status_old = 1;

// Mode
#define AUTOMODE 0
#define MANUALMODE 1
#define EMERGENCYMODE 2
int mode = 0;
int debug = 0;

// Function Declarations
void usart_init(float baud);
void usart_send_byte(unsigned char data);
void usart_send_string(char *pStr);
void usart_send_num(float num, char num_int, char num_decimal);
void usart_flush(void);
unsigned char usart_receive_string(char *ptr);
void process_input(void);

float linear_map(float x, float x1, float x2, float y1, float y2);
void green_led(int duration);
void yellow_led(int duration);
void red_led(int duration);
void red_led_alert(void);
void auto_green(int duration);
void auto_yellow(int duration);
void auto_red(int duration);

void check_dist(void);
void play_sound_ms(float F, float x, int playTime);
void delay_any_ms(char x);
void dist_telemetry(void);
void adc_telemetry(void);

void automode(void);
void manualmode(void);
void emergencymode(void);

bool alert_flag = 0;

char usart_buffer[32] = {0};
char *pBuffer = usart_buffer;

// photocell
ISR(ADC_vect) {
  adc = ADC;
}

// pin change interrupt service routine using pushbutton to change modes
// for the traffic light, cycle between automatic and manual mode
ISR(PCINT0_vect) {
  button_mode_status = bitCheck(PINB, button_mode_pin);
  if (button_mode_status != button_mode_status_old) {
    _delay_ms(20);
    button_mode_status = bitCheck(PINB, button_mode_pin);
    if (button_mode_status != button_mode_status_old) {
      button_mode_status_old = button_mode_status;
      if (button_mode_status_old == 1) {
        if (mode > 0) {
          mode = 0;
        } else {
          mode++;
        }
      }
    }
  }
}

// pin change interrupt service routine using pushbutton to change light
// colour when traffic light is in manual mode
ISR(PCINT2_vect) {
  if (mode != MANUALMODE) {
    return;
  }
  button_colour_status = bitCheck(PIND, button_colour_pin);
  if (button_colour_status != button_colour_status_old) {
    _delay_ms(20);
    button_colour_status = bitCheck(PIND, button_colour_pin);
    if (button_colour_status != button_colour_status_old) {
      button_colour_status_old = button_colour_status;
      if (button_colour_status_old == 1) {
        if (led_colour > 1) {
          led_colour = 0;
        } else {
          led_colour++;
        }
      }
    }
  }
}

// external interrupt service routine using pushbutton to change traffic
// light into emergency mode
ISR(INT1_vect) {
  _delay_ms(20);
  if (bitCheck(PIND, button_emergency_pin)) {
    mode = EMERGENCYMODE;
  }
}

int main(void) {

  usart_init(9600); // initialise usart

  _delay_ms(500);
  usart_flush();

  // UCSRnB: USART Control and Status Register B
  bitSet(UCSR0B, TXEN0);  // Transmitter Enable
  bitSet(UCSR0B, RXEN0);  // Receiver Enable
  // UCSRnB: USART Control and Status Register C
  UCSR0C |= 3 << UCSZ00;  // 8-bit Setting

  // LED
  bitSet(led_DDR, led_green);
  bitSet(led_DDR, led_yellow);
  bitSet(led_DDR, led_red);

  // Button
  bitClear(button_mode_DDR, button_mode_pin);
  bitSet(button_mode_port, button_mode_pin);
  bitClear(button_colour_DDR, button_colour_pin);
  bitSet(button_colour_port, button_colour_pin);
  bitClear(button_emergency_DDR, button_emergency_pin);
  bitSet(button_emergency_port, button_emergency_pin);

  // Photocell
  bitSet(ADMUX, REFS0); // Reference
  bitSet(ADMUX, photocell_pin);  // Input
  ADCSRA |= 0b111 < 0;  // Pre-scaling
  bitSet(ADCSRA, ADIE); // Interrupt enable
  bitSet(ADCSRA, ADEN); // adc enable

  // Sonar
  bitSet(sonar_DDR, trigger_pin); // output pulse
  bitClear(sonar_DDR, echo_pin);  // catch pulse
  
  // Buzzer
  bitSet(buzzer_DDR, buzzer_pin);

  // Interrupt
  PCICR |= 1 << PCIE0;
  PCMSK0 |= 1 << PCINT5;
  PCICR |= 1 << PCIE2;
  PCMSK2 |= 1 << PCINT23;
  EIMSK |= 1 << INT1;
  EICRA |= 3 << ISC10;  // rising edge

  sei();  // Global interrupt enable

  bitSet(ADCSRA, ADSC); // start photocell conv

  _delay_ms(10);

  while (1) {
    alert_flag = 0;

    usart_receive_string(pBuffer);
    process_input();

    bitSet(ADCSRA, ADSC);
    duty_cycle = linear_map(adc, ADC_LOW, ADC_HIGH, DC_HIGH, DC_LOW);
    if (debug == 2 || debug == 3) {
      adc_telemetry();
    }

    check_dist();
    if (debug == 1 || debug == 3) {
      dist_telemetry();
    }

    // traffic light mode selector
    switch (mode) {
      case AUTOMODE: 
        automode();
        break;
      case MANUALMODE:
        manualmode();
        break;
      case EMERGENCYMODE:
        emergencymode();
        break;
      default:
      mode = AUTOMODE;
        break;
    }
  }
}

// Emergency Mode: the traffic light will blink green
void emergencymode(void) {
  green_led(LED_DURATION);
  led_colour = 0;
}

// Manual Mode: the traffic light will remain the selected colour
// until changed using a interrupt service routine and pushbutton
void manualmode(void) {
  switch (led_colour) {
    case GREEN:
      green_led(LED_DURATION);
      break;
    case YELLOW:
      yellow_led(LED_DURATION);
      break;
    case RED:
      red_led(LED_DURATION);
      break;
    default:
      break;
  }
}

// Automatic Mode: the traffic light will operate as normal, cycling
// through the 3 colours. 
void automode(void) {
  switch (led_colour) {
    case GREEN:
      led_colour = YELLOW;
      auto_green(LED_DURATION);
      break;
    case YELLOW:
      led_colour = RED;
      auto_yellow(LED_DURATION);
      break;
    case RED:
      led_colour = GREEN;
      auto_red(LED_DURATION);
      break;
    default:
      led_colour = GREEN;
      break;
  }
}

// manual mode green led
void green_led(int duration) {
  int i = 0;
  while (i < (duration / Tcycle_pwmled)) {
    bitSet(ADCSRA, ADSC);
    duty_cycle = linear_map(adc, ADC_LOW, ADC_HIGH, DC_HIGH, DC_LOW);
    char temp = Tcycle_pwmled * duty_cycle;
    bitSet(led_port, led_green);
    delay_any_ms(temp);
    bitClear(led_port, led_green);
    delay_any_ms(Tcycle_pwmled - temp);
    i++;
  }
}

// manual mode yellow led
void yellow_led(int duration) {
  int i = 0;
  while (i < (duration / Tcycle_pwmled)) {
    bitSet(ADCSRA, ADSC);
    duty_cycle = linear_map(adc, ADC_LOW, ADC_HIGH, DC_HIGH, DC_LOW);
    char temp = Tcycle_pwmled * duty_cycle;
    bitSet(led_port, led_yellow);
    delay_any_ms(temp);
    bitClear(led_port, led_yellow);
    delay_any_ms(Tcycle_pwmled - temp);
    i++;
  }
}

// manual mode red led
void red_led(int duration) {
  int i = 0;
  while (i < (duration / Tcycle_pwmled)) {
    bitSet(ADCSRA, ADSC);
    duty_cycle = linear_map(adc, ADC_LOW, ADC_HIGH, DC_HIGH, DC_LOW);
    char temp = Tcycle_pwmled * duty_cycle;
    bitSet(led_port, led_red);
    delay_any_ms(temp);
    bitClear(led_port, led_red);
    delay_any_ms(Tcycle_pwmled - temp);
    i++;
  }
}

// causes the red led to blink and buzzer to alarm rapidly relative to 
// the distance between the traffic light and obstruction detected using the sonar
void red_led_alert(void) {
  while (Dmm < DETECT_RANGE_FAR) {
    if (debug == 2 || debug == 3) {
      adc_telemetry();
    }
    if (debug == 1 || debug == 3) {
      dist_telemetry();
    }
    play_sound_ms(523.25, 0.05, 10);
    red_led(Dmm * 5);
    check_dist();
  }
  alert_flag = 1;
}

// automatic mode green led
void auto_green(int duration) {
  int i = 0;
  while (i < (duration / Tcycle_pwmled)
          && Dmm > DETECT_RANGE_FAR) {
    bitSet(ADCSRA, ADSC);
    duty_cycle = linear_map(adc, ADC_LOW, ADC_HIGH, DC_HIGH, DC_LOW);
    char temp = Tcycle_pwmled * duty_cycle;
    bitSet(led_port, led_green);
    delay_any_ms(temp);
    bitClear(led_port, led_green);
    delay_any_ms(Tcycle_pwmled - temp);
    check_dist();
    i++;
  }
  if (Dmm < DETECT_RANGE_FAR) {
    red_led_alert();
  }
  if (alert_flag == 1) {
    led_colour = GREEN;
  }
}

// automatic mode yellow led
void auto_yellow(int duration) {
  int i = 0;
  while (i < (duration / Tcycle_pwmled)
          && Dmm > DETECT_RANGE_FAR) {
    bitSet(ADCSRA, ADSC);
    duty_cycle = linear_map(adc, ADC_LOW, ADC_HIGH, DC_HIGH, DC_LOW);
    char temp = Tcycle_pwmled * duty_cycle;
    bitSet(led_port, led_yellow);
    delay_any_ms(temp);
    bitClear(led_port, led_yellow);
    delay_any_ms(Tcycle_pwmled - temp);
    check_dist();
    i++;
  }
  if (Dmm < DETECT_RANGE_FAR) {
    red_led_alert();
  }
  if (alert_flag == 1) {
    led_colour = YELLOW;
  }
}

// automatic mode red led
void auto_red(int duration) {
  int i = 0;
  while (i < (duration / Tcycle_pwmled)) {
    bitSet(ADCSRA, ADSC);
    duty_cycle = linear_map(adc, ADC_LOW, ADC_HIGH, DC_HIGH, DC_LOW);
    char temp = Tcycle_pwmled * duty_cycle;
    bitSet(led_port, led_red);
    delay_any_ms(temp);
    bitClear(led_port, led_red);
    delay_any_ms(Tcycle_pwmled - temp);
    check_dist();
    i++;
  }
  if (Dmm < DETECT_RANGE_FAR) {
    led_colour = RED;
  }
}

// Function using the sonar to check the distance between the sensor 
// and any obstructions
void check_dist(void) {
  cnt = 0;
  timeout = 30000;
  bitClear(sonar_port, trigger_pin);
  _delay_us(2);
  bitSet(sonar_port, trigger_pin);
  _delay_us(11);
  bitClear(sonar_port, trigger_pin);

  while(!bitCheck(sonar_pin, echo_pin)) {
    ; // wait for pulse
  }
  while (bitCheck(sonar_pin, echo_pin) && timeout--) {
    cnt++;  // count pulse
    _delay_us(1);
  }
  // dist calc
  Dmm = (float)cnt / 1.0e6 * VEL_SOUND / 2. * 1000.;
}

// output the sonar distance telemetry on the usart interface
void dist_telemetry(void) {
  usart_send_string(">Dmm:");
  usart_send_num(Dmm, 4, 0);
  usart_send_byte('\n');
}

// output the photocell telemetry on the usart interface
void adc_telemetry(void) {
  usart_send_string(">adc:");
  usart_send_num(adc, 4, 0);
  usart_send_byte('\n');
}

// produce a sound with the buzzer with specificed frequency, volume, and length
void play_sound_ms(float F, float x, int playTime) {
  bitSet(led_port, led_red);
  float T = 1e6 / F;
  int Ton = x * T;
  int Toff = T - Ton;
  int cycles = playTime * 1e3 / T;

  for (int i = 0; i < cycles; i++) {
    buzzer_port = buzzer_port | (1 << buzzer_pin);
    // bitSet(buzzer_port, buzzer_pin);
    for (int i = 0; i < Ton; i++) {
      _delay_us(1);
    }

    buzzer_port = buzzer_port & ~(1 << buzzer_pin);
    // bitClear(buzzer_port, buzzer_pin);
    for (int i = 0; i < Toff; i++) {
      _delay_us(1);
    }
  }
}

// util func to return corresponding y value, given x value and linear gradient
float linear_map(float x, float x1, float x2, float y1, float y2) {
  float y;

  if (x < x1) {
    x = x1;
  }
  if (x > x2) {
    x = x2;
  }
  if (y2 > y1) {
    y = y1 + (y2 - y1) / (x2 - x1) * x;
  } else {
    y = y1 - (y1 - y2) / (x2 - x1) * x;
  }

  return y;
}

// util func to delay x milliseconds
void delay_any_ms(char x) {
  for (int i = 0; i < x; i++) {
    _delay_ms(1);
  }
}

// initialising usart
void usart_init(float baud) {
  float ubrr0 = 16 * 1.0e6 / 16 / baud - 1;
  int ubrr0a = (int)ubrr0;

  if (ubrr0 - ubrr0a >= 0.5) {
    ubrr0a = ubrr0a + 1;
  }

  UBRR0 = ubrr0a;
}

// usart func to send individual bytes into the data register UDR0
void usart_send_byte(unsigned char data) {
  while (!bitCheck(UCSR0A, UDRE0)) {
    ;
  }
  UDR0 = data;
}

// usart util func to output a string
void usart_send_string(char *pStr) {
  while (*pStr != '\0') {
    usart_send_byte(*pStr);
    pStr++;
  }
}

// usart util func to parse a number into a string, then send it
void usart_send_num(float num, char num_int, char num_decimal) {
  char str[32];
  if (num_decimal == 0) {
    dtostrf(num, num_int, num_decimal, str);
  } else {
    dtostrf(num, num_int + num_decimal + 1, num_decimal, str);
  }
  str[num_int + num_decimal + 1] = '\0';
  usart_send_string(str);
}

// usart util func to flush the input buffer
void usart_flush(void) {
  char c;
  while (bitCheck(UCSR0A, RXC0)) {
    c = UDR0;
  }
}

// usart util func to recieve a string input from user
unsigned char usart_receive_string(char *ptr) {
  char tmp;

  while (1) {
    if (!bitCheck(UCSR0A, RXC0)) {
      return;
    }
    tmp = UDR0; // Read the data
    if (tmp == '\r' || tmp == '\n') {
      // Keep reading through the data
      *ptr = '\0';
      return;
    } else {
      *ptr++ = tmp;
      // Store data in usart_buffer array
    }
  }
}


// processing user input in the usart interface
void process_input(void) {
  int choice = atoi(pBuffer);

  if (choice < 0 || choice >= 4) {
    return;
  }

  switch (choice) {
    case 1: 
      debug = 1;
      break;
    case 2: 
      debug = 2;
      break;
    case 3:
      debug = 3;
      break;
    default:
      break;
  }
  usart_flush();
}