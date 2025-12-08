//CPE 301 Final Project by Adam Beltran

#define RDA 0x80
#define TBE 0x20
#define WaterLevelThreshHold 360
#define TemperatureThreshold 18

#include <LiquidCrystal.h>
#include <Stepper.h>
#include "DHT.h"

//Stepper motor
Stepper StepperMotor(2048, 30, 32, 31, 33);
int previousStepperValue = 0;
int currentStepperValue;

enum LED {YELLOW = 0, GREEN = 2, RED = 4, BLUE = 6};
enum STATES {DISABLED, IDLE, ERROR, RUNNING}; //0, 1, 2, 3
int state;
int previousState = -1;
//LED's register.
volatile unsigned char* pin_a = (unsigned char*) 0x20; //LED PIN ADDRESS
volatile unsigned char* ddr_a = (unsigned char*) 0x21; //LED DATA DIRECTION REGISTER
volatile unsigned char* port_a = (unsigned char*) 0x22; //LED DATA REGISTER

//Button register (start).
volatile unsigned char* pin_e = (unsigned char*) 0x2C; //Start button PIN ADDRESS
volatile unsigned char* ddr_e = (unsigned char*) 0x2D; //Start button DATA DIRECTION REGISTER
volatile unsigned char* port_e = (unsigned char*) 0x2E; //Start button DATA REGISTER

//Motor Register
volatile unsigned char* pin_b = (unsigned char*) 0x23; //MOTOR PIN ADDRESS
volatile unsigned char* ddr_b = (unsigned char*) 0x24; //MOTOR DATA DIRECTION REGISTER
volatile unsigned char* port_b = (unsigned char*) 0x25; //MOTOR DATA REGISTER

//Stop & restart Button Register:
volatile unsigned char* pin_c = (unsigned char*) 0x26; //S&R BUTTON PIN ADDRESS
volatile unsigned char* ddr_c = (unsigned char*) 0x27; //S&R BUTTON DATA DIRECTION REGISTER
volatile unsigned char* port_c = (unsigned char*) 0x28; //S&R BUTTON DATA REGISTER

// UART Pointers
volatile unsigned char *myUCSR0A = (unsigned char *)0x00C0;
volatile unsigned char *myUCSR0B = (unsigned char *)0x00C1;
volatile unsigned char *myUCSR0C = (unsigned char *)0x00C2;
volatile unsigned int *myUBRR0 = (unsigned int *)   0x00C4;
volatile unsigned char *myUDR0 = (unsigned char *)  0x00C6;

volatile unsigned char* my_ADMUX = (unsigned char*) 0x7C;
volatile unsigned char* my_ADCSRB = (unsigned char*) 0x7B;
volatile unsigned char* my_ADCSRA = (unsigned char*) 0x7A;
volatile unsigned int* my_ADC_DATA = (unsigned int*) 0x78;

volatile bool start_button = false;
const int RS = 40, EN = 41, D4 = 42, D5 = 43, D6 = 44, D7 = 45;
LiquidCrystal lcd(RS, EN, D4, D5, D6, D7);

DHT dht(7, DHT11);


unsigned long LcdUpdate = 0;
const unsigned long Minute = 60000; //every minute.
unsigned long HumidityLevel;
unsigned long TemperatureLevel;
void setup() {
	U0init(9600);
	adc_init();
	dht.begin();

	lcd.begin(16, 2);
	lcd.setCursor(0,0);

	StepperMotor.setSpeed(20);

	*ddr_a |= 0x55; //Pins 22, 24, 26, 28 on Arduino are used for LEDS, Yellow = 22, Green = 24, Red = 26, Blue = 28
	*port_a &= ~0x55;

//Start Button
	*ddr_e &= ~0x10;
	*port_e |= 0x10;

//Motor setup
	*ddr_b |= 0x40; //pin 12
	*port_b &= ~0x40;

//Stop/Restart Button+
	*ddr_c |= 0x0C; //pins 34, 35. 34 = stop. 35 = restart.
	*port_c &= ~0x0C;

	attachInterrupt(digitalPinToInterrupt(2), Start, FALLING);
	HumidityLevel = dht.readHumidity();
	TemperatureLevel = dht.readTemperature();
}

void loop() {
	int waterLevel = adc_read(0);
	currentStepperValue = adc_read(7);
	int speed = map(currentStepperValue, 0, 1023, 0, 12);
	StepperMotor.setSpeed(speed);
	bool stopNow    = !(*pin_c & 0x08);
	bool restartNow = !(*pin_c & 0x04);
	static bool prevStop = false;
	static bool prevRestart = false;
	bool stop_button    = stopNow    && !prevStop;
	bool restart_button = restartNow && !prevRestart;
	prevStop = stopNow;
	prevRestart = restartNow;

	if (stop_button) Stop_button();
	if(restart_button) Restart_button();
	if (speed > 0) {
		StepperMotor.step(1);
	}
	if (state != previousState) {
		DisplayLCD();
		previousState = state;
	}
	if (millis() - LcdUpdate >= Minute) {
		LcdUpdate = millis();
		DisplayLCD();
	}

	switch (state) {
	case DISABLED: //yellow
		if(*port_b & 0x40) {
			//if fan is on
			FanOFF();
		}
		LedON(YELLOW);
		LedOFF(RED);
		LedOFF(GREEN);
		LedOFF(BLUE);

		if(start_button) {
			Serial.println("SYSTEM STARTED \n");
			start_button = false;
			state = RUNNING;
		}
		break;
	case IDLE:
		FanON();
		LedON(GREEN);
		LedOFF(RED);
		LedOFF(YELLOW);
		LedOFF(BLUE);
		if(stop_button) {
			start_button = false;
			state = DISABLED;
		}
		if (waterLevel < WaterLevelThreshHold) {
			start_button = false;
			state = ERROR;
		}
		break;

	case RUNNING:
		start_button = false;
		FanON();
		LedON(BLUE);
		LedOFF(RED);
		LedOFF(YELLOW);
		LedOFF(GREEN);

		if(*port_b & 0x40) {
			//fan is already on.
		} else {
			FanON();
		}
		if(stop_button) {
			start_button = false;
			state = DISABLED;
		}
		if (waterLevel < WaterLevelThreshHold) {
			start_button = false;
			state = ERROR;
		}
		else if (TemperatureLevel < TemperatureThreshold) {
			start_button = false;
			state = IDLE;
		}
		break;

	case ERROR:
		LedON(RED);
		LedOFF(GREEN);
		LedOFF(YELLOW);
		LedOFF(BLUE);
		if(*port_b & 0x40) {
			FanOFF();
		} else {
			//fan is already off
		}
		if(waterLevel > WaterLevelThreshHold) {
			if(restart_button) {
				state = IDLE;
			}
		}
		break;

	default:
		state = DISABLED;
		break;
	}
}


void U0init(int U0baud) {
	unsigned long FCPU = 16000000;
	unsigned int tbaud;
	tbaud = (FCPU / 16 / U0baud - 1);
	// Same as (FCPU / (16 * U0baud)) - 1;
	*myUCSR0A = 0x20;
	*myUCSR0B = 0x18;
	*myUCSR0C = 0x06;
	*myUBRR0 = tbaud;
}
unsigned char U0kbhit() {
	if (*myUCSR0A & RDA) {
		return true;
	} else {
		return false;
	}
}
unsigned char U0getchar() {
	return *myUDR0;
}
void U0putchar(unsigned char U0pdata) {
	while ((*myUCSR0A & TBE) == 0);
	*myUDR0 = U0pdata;
}
void adc_init()
{
	// setup the A register
// set bit 7 to 1 to enable the ADC
	*my_ADCSRA |= 0b10000000;
// clear bit 5 to 0 to disable the ADC trigger mode
	*my_ADCSRA &= 0b11011111;
// clear bit 3 to 0 to disable the ADC interrupt
	*my_ADCSRA &= 0b11110111;
// clear bit 0-2 to 0 to set prescaler selection to slow reading
	*my_ADCSRA &= 0b11111000;
	// setup the B register
// clear bit 3 to 0 to reset the channel and gain bits
	*my_ADCSRB &= 0b11110111;
// clear bit 2-0 to 0 to set free running mode
	*my_ADCSRB &= 0b11111000;
	// setup the MUX Register
// clear bit 7 to 0 for AVCC analog reference
	*my_ADMUX &= 0b11011111;
// set bit 6 to 1 for AVCC analog reference
	*my_ADMUX |= 0b01000000;
	// clear bit 5 to 0 for right adjust result
	*my_ADMUX &= 0b11011111;
// clear bit 4-0 to 0 to reset the channel and gain bits
	*my_ADMUX &= 0b11100000;
}
unsigned int adc_read(unsigned char adc_channel_num)
{
	// Clear old channel selection (keep REFS bits)
	*my_ADMUX  &= 0xE0;        // 1110 0000
	*my_ADCSRB &= ~(1 << 3);  // clear MUX5

	if (adc_channel_num == 0) {
		// ADC0 (A0)
		// MUX bits already 0
	}
	else if (adc_channel_num == 7) {
		// ADC7 (A7)
		*my_ADMUX |= 0x07;
	}
	else {
	//default is 0
  }
	// Start conversion
	*my_ADCSRA |= (1 << 6);   // ADSC

	// Wait until conversion completes
	while (*my_ADCSRA & (1 << 6));

	// Read 10-bit result
	return (*my_ADC_DATA & 0x03FF);
}

void LedON(unsigned char bit) {
	*port_a |= (1 << bit);
}
void LedOFF(unsigned char bit) {
	*port_a &= ~(1 << bit);
}
void FanON() {
	*port_b |= 0x40;
}
void FanOFF() {
	*port_b &= ~0x40;
}
void Stop_button() {
	//*port_c &= ~0x0C;
	*port_c |= 0x04;
	Serial.println("Stop button pressed \n");
}
void Restart_button() {
	//*port_c &= ~0x0C;
	*port_c |= 0x08;
	Serial.println("restart button pressed \n");
}
void DisplayLCD() {
	lcd.clear();
	lcd.setCursor(0, 0);
	if(state == ERROR) {
		lcd.print("ERROR");
		return;
	}
	if(state == DISABLED) {
		return;
	}
	lcd.print("Temp: ");
	lcd.print(TemperatureLevel);
	lcd.setCursor(0, 1);
	lcd.print("Humidity: ");
	lcd.print(HumidityLevel );
	return;
}
//Functionallity for the start button
void Start() {
	start_button = true;
}