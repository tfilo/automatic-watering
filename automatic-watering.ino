#include <Wire.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <SSD1306Ascii.h>
#include <SSD1306AsciiAvrI2c.h>
#include <Sodaq_DS3231.h>
#include <avr/sleep.h>
#include <avr/power.h>
#include <EEPROM.h>

#define F_CPU 8000000L
#define I2C_OLED_ADDRESS 0x3C
#define ONE_WIRE_BUS_PIN 4

#define MAIN_SCREEN 0
#define MENU_SCREEN 1
#define TIME_SETUP_SCREEN 2
#define SLEEP_SETUP_SCREEN 3

#define POSITION_DEFAULT 0
#define MENU_TIME_POSITION POSITION_DEFAULT
#define MENU_SLEEP_POSITION 1
#define MENU_EXIT_POSITION 2

#define TIME_YEAR_POSITION POSITION_DEFAULT
#define TIME_MONTH_POSITION 1
#define TIME_DAY_POSITION 2
#define TIME_HOUR_POSITION 3
#define TIME_MINUTE_POSITION 4

#define BTN_INTERRUPT_PIN 2
#define BTN_SET 7
#define BTN_UP 8
#define BTN_DOWN 9
#define NO_BTN 0

#define ANALOG_SENSOR_CONTROL 10

#define SLEEP_EEPROM_ADDR 0

const char daysInMonth[] = {31,28,31,30,31,30,31,31,30,31,30,31};
unsigned long lastBtnPress = 0; // time in ms
unsigned char lastButton = 0; // number of button

unsigned char actualScreen = 0; // actual screen where we are
unsigned char screenPosition = 0; // position in actual screen

unsigned int  setYear = 2010;
unsigned char setMonth = 1;
unsigned char setDay = 1;
unsigned char setHour = 0;
unsigned char setMinute = 0;

bool sleeping = true;
unsigned int sleepAfter;

DateTime now;
OneWire oneWire(ONE_WIRE_BUS_PIN);
DallasTemperature tempSensors(&oneWire);
SSD1306AsciiAvrI2c oled;

void setup(void)
{
  wakeUp();
  setupAsInterrupt();
  attachInterrupt(digitalPinToInterrupt(BTN_INTERRUPT_PIN), pressInterrupt, FALLING);
  byte sleepByte = EEPROM.read(SLEEP_EEPROM_ADDR);
  switch (sleepByte) {
    case 1:
      sleepAfter = 15000;
      break;
    case 2:
      sleepAfter = 30000;
      break;
    case 3:
      sleepAfter = 45000;
      break;
    case 4:
      sleepAfter = 60000;
      break;
    default:
      sleepAfter = 30000;
      break;
  }
}

void loop(void)
{
  unsigned char btn = lastButton;
  lastButton = NO_BTN;

  switch (btn) {
    case BTN_SET:
      handleSetButton();
      break;
    case BTN_UP:
      handleUpButton();
      break;
    case BTN_DOWN:
      handleDownButton();
      break;
  }

  switch (actualScreen) {
    case MAIN_SCREEN:
      mainScreen();
      break;
    case MENU_SCREEN:
      menuScreen();
      break;
    case TIME_SETUP_SCREEN:
      timeScreen();
      break;
    case SLEEP_SETUP_SCREEN:
      sleepScreen();
  }

  if (millis() - lastBtnPress > sleepAfter) {
    putToSleep();
  } else {
    delay(100);
  }
}

/*******************
 * PROCESS BUTTONS *
 *******************/

void handleSetButton() {
  oled.clear();

  if (actualScreen == MAIN_SCREEN) {
    actualScreen = MENU_SCREEN;
    screenPosition = POSITION_DEFAULT;
    return;
  }

  if (actualScreen == MENU_SCREEN) {
    switch (screenPosition) {
      case MENU_TIME_POSITION:
        actualScreen = TIME_SETUP_SCREEN;
        screenPosition = POSITION_DEFAULT;
        now = rtc.now();
        setYear = now.year();
        setMonth = now.month();
        setDay = now.date();
        setHour = now.hour();
        setMinute = now.minute();
        break;
      case MENU_SLEEP_POSITION:
        actualScreen = SLEEP_SETUP_SCREEN;
        screenPosition = POSITION_DEFAULT;
        break;
      case MENU_EXIT_POSITION:
        actualScreen = MAIN_SCREEN;
        screenPosition = POSITION_DEFAULT;
        break;
    }
    return;
  }

  if (actualScreen == TIME_SETUP_SCREEN) {
    switch (screenPosition) {
      case TIME_YEAR_POSITION:
      case TIME_MONTH_POSITION:
      case TIME_DAY_POSITION:
      case TIME_HOUR_POSITION:
        screenPosition++;
        break;
      case TIME_MINUTE_POSITION:
        DateTime dt(setYear, setMonth, setDay, setHour, setMinute, 00, 1); // TODO can I ignore last param WeekDay and set it to 1 every time ?
        rtc.setDateTime(dt);
        actualScreen = MENU_SCREEN;
        screenPosition = POSITION_DEFAULT;
        break;
    }
    return;
  }

  if (actualScreen == SLEEP_SETUP_SCREEN) {
    byte sleepByte;
    switch (sleepAfter) {
      case 15000:
        sleepByte = 1;
        break;
      case 30000:
        sleepByte = 2;
        break;
      case 45000:
        sleepByte = 3;
        break;
      case 60000:
        sleepByte = 4;
        break;
      default:
        sleepByte = 2;
        break;
    }
    EEPROM.write(SLEEP_EEPROM_ADDR, sleepByte);
    actualScreen = MENU_SCREEN;
    screenPosition = POSITION_DEFAULT;
    return;
  }
}

void handleUpButton() {
  if (actualScreen == MENU_SCREEN) {
    if (screenPosition == POSITION_DEFAULT) {
      screenPosition = MENU_EXIT_POSITION;
    } else {
      screenPosition--;
    }
    return;
  }

  if (actualScreen == TIME_SETUP_SCREEN) {
    switch (screenPosition) {
      case TIME_YEAR_POSITION:
        if (setYear >= 2099) {
          setYear = 2010;
        } else {
          setYear++;
        }
        break;
      case TIME_MONTH_POSITION:
        if (setMonth >= 12) {
          setMonth = 1;
        } else {
          setMonth++;
        }
        break;
      case TIME_DAY_POSITION:
        if (setDay >= daysInMonth[setMonth - 1]) {
          setDay = 1;
        } else {
          setDay++;
        }
        break;
      case TIME_HOUR_POSITION:
        if (setHour >= 23) {
          setHour = 0;
        } else {
          setHour++;
        }
        break;
      case TIME_MINUTE_POSITION:
        if (setMinute >= 59) {
          setMinute = 0;
        } else {
          setMinute++;
        }
        break;
    }
    return;
  }

  if (actualScreen == SLEEP_SETUP_SCREEN) {
    if (sleepAfter >= 60000) {
      sleepAfter = 15000;
    } else {
      sleepAfter+=15000;
    }
    return;
  }
}

void handleDownButton() {
  if (actualScreen == MENU_SCREEN) {
    if (screenPosition == MENU_EXIT_POSITION) {
      screenPosition = POSITION_DEFAULT;
    } else {
      screenPosition++;
    }
    return;
  }

  if (actualScreen == TIME_SETUP_SCREEN) {
    switch (screenPosition) {
      case TIME_YEAR_POSITION:
        if (setYear <= 2010) {
          setYear = 2099;
        } else {
          setYear--;
        }
        break;
      case TIME_MONTH_POSITION:
        if (setMonth <= 1) {
          setMonth = 12;
        } else {
          setMonth--;
        }
        break;
      case TIME_DAY_POSITION:
        if (setDay <= 1) {
          setDay = daysInMonth[setMonth - 1];
        } else {
          setDay--;
        }
        break;
      case TIME_HOUR_POSITION:
        if (setHour <= 0) {
          setHour = 23;
        } else {
          setHour--;
        }
        break;
      case TIME_MINUTE_POSITION:
        if (setMinute <= 0) {
          setMinute = 59;
        } else {
          setMinute--;
        }
        break;
    }
    return;
  }

  if (actualScreen == SLEEP_SETUP_SCREEN) {
    if (sleepAfter >= 15000) {
      sleepAfter = 60000;
    } else {
      sleepAfter-=15000;
    }
    return;
  }
}

/***********
 * SCREENS *
 ***********/

void timeScreen() {
  oled.setRow(1);
  oled.setCol(1);
  oled.print("Nastav cas a datum");
  oled.clearToEOL ();

  oled.setRow(2);
  oled.setCol(1);

  switch(screenPosition) {
    case TIME_YEAR_POSITION:
      oled.print("Rok: ");
      oled.print(setYear);
      break;
    case TIME_MONTH_POSITION:
      oled.print("Mesiac: ");
      oled.print(setMonth);
      break;
    case TIME_DAY_POSITION:
      oled.print("Den: ");
      oled.print(setDay);
      break;
    case TIME_HOUR_POSITION:
      oled.print("Hodina: ");
      oled.print(setHour);
      break;
    case TIME_MINUTE_POSITION:
      oled.print("Minuta: ");
      oled.print(setMinute);
      break;
  }
  oled.clearToEOL ();
}

void sleepSceen() {
  oled.setRow(1);
  oled.setCol(1);
  oled.print("Usporny rezim");
  oled.clearToEOL ();

  oled.setRow(3);
  oled.setCol(1);
  oled.print(sleepAfter / 1000, DEC);
  oled.print("s");
  oled.clearToEOL ();
}

void menuScreen() {
  oled.setRow(1);
  oled.setCol(1);
  oled.print("MENU:");
  oled.clearToEOL ();

  oled.setRow(2);
  oled.setCol(1);
  if (screenPosition == MENU_TIME_POSITION) {
    oled.print("*");
  } else {
    oled.print(" ");
  }
  oled.print("Cas a datum");
  oled.clearToEOL ();

  oled.setRow(3);
  oled.setCol(1);
  if (screenPosition == MENU_SLEEP_POSITION) {
    oled.print("*");
  } else {
    oled.print(" ");
  }
  oled.print("Usporny rezim");
  oled.clearToEOL ();

  oled.setRow(4);
  oled.setCol(1);
  if (screenPosition == MENU_EXIT_POSITION) {
    oled.print("*");
  } else {
    oled.print(" ");
  }
  oled.print("Exit");
  oled.clearToEOL ();
}

void mainScreen() {
  tempSensors.requestTemperatures(); // Send the command to get temperatures
  float soilTemperature = tempSensors.getTempCByIndex(0);
  int soilHumidity = analogRead(A0);

  now = rtc.now();
  char actual_time[16];
  sprintf(actual_time, "%02d:%02d %02d.%02d.%04d", now.hour(), now.minute(), now.date(), now.month(), now.year());

  oled.setRow(1);
  oled.setCol(3);
  oled.print(actual_time);
  oled.clearToEOL ();

  oled.setRow(4);
  oled.setCol(1);
  oled.print("Teplota: ");
  oled.print(soilTemperature);
  oled.clearToEOL ();

  oled.setRow(5);
  oled.setCol(1);
  oled.print("Vlh. pody: ");
  oled.print(soilHumidity);
  oled.clearToEOL ();
}

/************
 * HARDWARE *
 ************/
void pressInterrupt() {
  if (millis() - lastBtnPress < 300) { // Debounce 300ms
    return;
  }
  wakeUp();

  lastBtnPress = millis();

  setupAsButtons();

  // test for pressed button
  if (!digitalRead(BTN_SET)) {
    lastButton = BTN_SET;
  }
  if (!digitalRead(BTN_UP)) {
    lastButton = BTN_UP;
  }
  if (!digitalRead(BTN_DOWN)) {
    lastButton = BTN_DOWN;
  }

  setupAsInterrupt();
}

void setupAsInterrupt() {
  pinMode(BTN_INTERRUPT_PIN, INPUT_PULLUP);
  pinMode(BTN_SET, OUTPUT);
  digitalWrite(BTN_SET, LOW);
  pinMode(BTN_UP, OUTPUT);
  digitalWrite(BTN_UP, LOW);
  pinMode(BTN_DOWN, OUTPUT);
  digitalWrite(BTN_DOWN, LOW);
}

void setupAsButtons() {
  pinMode(BTN_SET, INPUT_PULLUP);
  pinMode(BTN_UP, INPUT_PULLUP);
  pinMode(BTN_DOWN, INPUT_PULLUP);
  pinMode(BTN_INTERRUPT_PIN, OUTPUT);
  digitalWrite(BTN_INTERRUPT_PIN, LOW);
}

void wakeUp() {
  if (sleeping) {
    noInterrupts();
    sleeping = false;
    sleep_disable();
    power_all_enable();
    ADCSRA |= (1 << ADEN); // wake up ADC
    pinMode(ANALOG_SENSOR_CONTROL, OUTPUT);
    digitalWrite(ANALOG_SENSOR_CONTROL, LOW);
    delay(20);
    tempSensors.begin();
    rtc.begin();
    oled.begin(&Adafruit128x64, I2C_OLED_ADDRESS);
    oled.setFont(Adafruit5x7);
    oled.clear();
    interrupts();
  }
}

void putToSleep() {
  noInterrupts();
  digitalWrite(ANALOG_SENSOR_CONTROL, HIGH); // turn off capacitive soil moisture sensor v1.2 using P-Channel MOSFET BS250, save around 5mA
  oled.ssd1306WriteCmd(0xAE); // turn off oled
  set_sleep_mode(SLEEP_MODE_PWR_DOWN); // set powerdown state for ATmega
  ADCSRA = 0; // put ADC to sleep, save around 0.250mA
  power_all_disable(); // put everything other to sleep
  sleep_enable();
  sleeping = true;
  interrupts();
  sleep_cpu(); // When sleeping current goes down to 0.035mA
  lastButton = NO_BTN; // after waking up by any button don't do action assignet to button
}