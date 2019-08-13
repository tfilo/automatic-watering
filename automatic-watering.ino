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

#define MOISTURE_SENSOR_1 A0 // group 1
#define MOISTURE_SENSOR_2 A1 // group 1
#define MOISTURE_SENSOR_3 A2 // group 1
#define MOISTURE_SENSOR_4 A0 // group 2
#define MOISTURE_SENSOR_5 A1 // group 2
#define MOISTURE_SENSOR_6 A2 // group 2

#define MOISTURE_GROUP1_CONTROL 10
#define MOISTURE_GROUP2_CONTROL 11 // ?? TODO overit tento PIN ci ho uz nepouzivam !!!!!!!!!!!!!!!!!!!!!!!!!!

#define MOISTURE_SENSORS_MAX_COUNT 6

#define MAIN_SCREEN 0
#define MENU_SCREEN 1
#define TIME_SETUP_SCREEN 2
#define SLEEP_SETUP_SCREEN 3
#define MOISTURE_ENABLE_SCREEN 4
#define MENU_SENSOR_CALIBRATION_SCREEN() 5

#define SENSOR_CALIBRATION_SCREEN() 10

#define POSITION_DEFAULT 0
#define MENU_TIME_POSITION POSITION_DEFAULT
#define MENU_SLEEP_POSITION 1
#define MENU_MOISTURE_POSITION 2
#define MENU_CALIBRATION_POSITION 3
#define MENU_EXIT_POSITION 4

#define TIME_YEAR_POSITION POSITION_DEFAULT
#define TIME_MONTH_POSITION 1
#define TIME_DAY_POSITION 2
#define TIME_HOUR_POSITION 3
#define TIME_MINUTE_POSITION 4

#define MOISTURE_ENABLE_POSITION POSITION_DEFAULT

#define BTN_INTERRUPT_PIN 2
#define BTN_SET 7
#define BTN_UP 8
#define BTN_DOWN 9
#define NO_BTN 0

#define SLEEP_EEPROM_ADDR 0
#define ENABLE_MOISTURE_EEPROM_ADDR 5

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

byte enabledMoistureSensors; // up to 6 sensors (0-5), ignore bit 6 and 7 (0 == false)
int moistureMin[] = {0, 0, 0, 0, 0, 0};
int moistureMax[] = {1023, 1023, 1023, 1023, 1023, 1023};
 
DateTime now;
OneWire oneWire(ONE_WIRE_BUS_PIN);
DallasTemperature tempSensors(&oneWire);
SSD1306AsciiAvrI2c oled;

void setup(void)
{
  pinMode(MOISTURE_GROUP1_CONTROL, OUTPUT);
  pinMode(MOISTURE_GROUP2_CONTROL, OUTPUT);
  digitalWrite(MOISTURE_GROUP1_CONTROL, HIGH); // keep sensors turned off
  digitalWrite(MOISTURE_GROUP2_CONTROL, HIGH); // keep sensors turned off
  // THIS part doesn't need to be in wakeUp function ???
  tempSensors.begin();
  rtc.begin();
  oled.begin(&Adafruit128x64, I2C_OLED_ADDRESS);
  oled.setFont(Adafruit5x7);
  
  setupAsInterrupt();
  attachInterrupt(digitalPinToInterrupt(BTN_INTERRUPT_PIN), pressInterrupt, FALLING);
  loadEEPROMvariables();
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
    case MOISTURE_ENABLE_SCREEN:
      moistureScreen();
    case MENU_SENSOR_CALIBRATION_SCREEN():
      calibrationMenuScreen();
    case SENSOR_CALIBRATION_SCREEN():
      calibrateSensorScreen();
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
      case MENU_MOISTURE_POSITION:
        actualScreen = MOISTURE_ENABLE_SCREEN;
        screenPosition = POSITION_DEFAULT;
        break;
      case MENU_CALIBRATION_POSITION:
        actualScreen = MENU_SENSOR_CALIBRATION_SCREEN();
        screenPosition = POSITION_DEFAULT;
        byte count = 0;
        while (bitRead(enabledMoistureSensors, screenPosition)==0) { // if disabled, jump to next enabled in same direction
          count++;
          screenPosition++;
          if (screenPosition == MOISTURE_SENSORS_MAX_COUNT - 1) {
            screenPosition = POSITION_DEFAULT;
          }
          if (count > MOISTURE_SENSORS_MAX_COUNT) {
            break; // just to prevent infinite loop if some error  
          }
        }
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
    EEPROM.write(SLEEP_EEPROM_ADDR + 1, 255 ^ sleepByte);
    actualScreen = MENU_SCREEN;
    screenPosition = POSITION_DEFAULT;
    return;
  }

  if (actualScreen == MOISTURE_ENABLE_SCREEN) {
    if (screenPosition >= MOISTURE_SENSORS_MAX_COUNT) {
      EEPROM.write(ENABLE_MOISTURE_EEPROM_ADDR, enabledMoistureSensors);
      EEPROM.write(ENABLE_MOISTURE_EEPROM_ADDR + 1, 255 ^ enabledMoistureSensors);
      actualScreen = MENU_SCREEN;
      screenPosition = POSITION_DEFAULT;
    } else {
      screenPosition++;  
    } 
    return;
  }

  if (actualScreen == MENU_SENSOR_CALIBRATION_SCREEN()) {
    actualScreen = SENSOR_CALIBRATION_SCREEN();
    // don't set screenPosition because it will be used as sensor index
    return;
  }

  if (actualScreen == SENSOR_CALIBRATION_SCREEN()) {
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
      if (screenPosition == MENU_CALIBRATION_POSITION && enabledMoistureSensors == 192) { // 192 means that all sensors are turned off (defaul value 11000000)
        screenPosition--; // skip calibration menu if no sensor enabled
      }
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

  if (actualScreen == MOISTURE_ENABLE_SCREEN) {
    enabledMoistureSensors = (enabledMoistureSensors ^ (1 << screenPosition)); // reverse bit on specified possition
    return;
  }

  if (actualScreen == MENU_SENSOR_CALIBRATION_SCREEN()) {
    if (screenPosition == POSITION_DEFAULT) {
      screenPosition = MOISTURE_SENSORS_MAX_COUNT - 1;
    } else {
      screenPosition--;
    }
    byte count = 0;
    while (bitRead(enabledMoistureSensors, screenPosition)==0) { // if disabled, jump to next enabled in same direction
      screenPosition--;
      if (screenPosition == POSITION_DEFAULT) {
        screenPosition = MOISTURE_SENSORS_MAX_COUNT - 1;
      }
      if (count > MOISTURE_SENSORS_MAX_COUNT) {
        break; // just to prevent infinite loop if some error  
      }
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
      if (screenPosition == MENU_CALIBRATION_POSITION && enabledMoistureSensors == 192) { // 192 means that all sensors are turned off (defaul value 11000000)
        screenPosition++; // skip calibration menu if no sensor enabled
      }
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
    if (sleepAfter <= 15000) {
      sleepAfter = 60000;
    } else {
      sleepAfter-=15000;
    }
    return;
  }

  if (actualScreen == MOISTURE_ENABLE_SCREEN) {
    enabledMoistureSensors = (enabledMoistureSensors ^ (1 << screenPosition)); // reverse bit on specified possition
    return;
  }

  if (actualScreen == MENU_SENSOR_CALIBRATION_SCREEN()) {
    if (screenPosition == MOISTURE_SENSORS_MAX_COUNT - 1) {
      screenPosition = POSITION_DEFAULT;
    } else {
      screenPosition++;
    }
    byte count = 0;
    while (bitRead(enabledMoistureSensors, screenPosition)==0) { // if disabled, jump to next enabled in same direction
      count++;
      screenPosition++;
      if (screenPosition == MOISTURE_SENSORS_MAX_COUNT - 1) {
        screenPosition = POSITION_DEFAULT;
      }
      if (count > MOISTURE_SENSORS_MAX_COUNT) {
        break; // just to prevent infinite loop if some error  
      }
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

void sleepScreen() {
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
  byte row = 1;
  oled.setRow(row++);
  oled.setCol(1);
  oled.print("MENU:");
  oled.clearToEOL ();

  oled.setRow(row++);
  oled.setCol(1);
  if (screenPosition == MENU_TIME_POSITION) {
    oled.print("*");
  } else {
    oled.print(" ");
  }
  oled.print("Cas a datum");
  oled.clearToEOL ();

  oled.setRow(row++);
  oled.setCol(1);
  if (screenPosition == MENU_SLEEP_POSITION) {
    oled.print("*");
  } else {
    oled.print(" ");
  }
  oled.print("Usporny rezim");
  oled.clearToEOL ();

  oled.setRow(row++);
  oled.setCol(1);
  if (screenPosition == MENU_MOISTURE_POSITION) {
    oled.print("*");
  } else {
    oled.print(" ");
  }
  oled.print("Povolenie senzorov");
  oled.clearToEOL ();

  oled.setRow(row++);
  oled.setCol(1);
  if (screenPosition == MENU_CALIBRATION_POSITION) {
    oled.print("*");
  } else {
    oled.print(" ");
  }
  oled.print("Kalibracia senzorov");
  oled.clearToEOL ();

  oled.setRow(row++);
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
  byte row = 1;
  tempSensors.requestTemperatures(); // Send the command to get temperatures
  now = rtc.now();
  char actual_time[20];
  sprintf(actual_time, "%02d:%02d %02d.%02d.%02d B%02d%%", now.hour(), now.minute(), now.date(), now.month(), (now.year() % 100), 99);

  oled.setRow(row++);
  oled.setCol(3);
  oled.print(actual_time);
  oled.clearToEOL ();

  row++;
  
  oled.setRow(row++);
  oled.setCol(1);
  oled.print("Temperature: ");
  oled.print(tempSensors.getTempCByIndex(0), 2);
  oled.print("C");
  oled.clearToEOL ();

  row++;
  
  oled.setRow(row++);
  oled.setCol(1);
  oled.print("H1: ");
  if (bitRead(enabledMoistureSensors, 0)) {
    oled.print(measure(0), DEC);
  } else {
    oled.print("off");
  }

  oled.setCol(8);
  oled.print("H2: ");
  if (bitRead(enabledMoistureSensors, 1)) {
    oled.print(measure(1), DEC);
  } else {
    oled.print("off");
  }

  oled.setCol(16);
  oled.print("H3: ");
  if (bitRead(enabledMoistureSensors, 2)) {
    oled.print(measure(2), DEC);
  } else {
    oled.print("off");
  }
  
  oled.clearToEOL ();

  oled.setRow(row++);
  oled.setCol(1);
  oled.print("H4: ");
  if (bitRead(enabledMoistureSensors, 3)) {
    oled.print(measure(3), DEC);
  } else {
    oled.print("off");
  }

  oled.setCol(8);
  oled.print("H5: ");
  if (bitRead(enabledMoistureSensors, 4)) {
    oled.print(measure(4), DEC);
  } else {
    oled.print("off");
  }

  oled.setCol(16);
  oled.print("H6: ");
  if (bitRead(enabledMoistureSensors, 5)) {
    oled.print(measure(5), DEC);
  } else {
    oled.print("off");
  }
  
  oled.clearToEOL ();
}

void moistureScreen() {
  oled.setRow(1);
  oled.setCol(1);
  oled.print("Povolenie senzorov");
  oled.clearToEOL();      
  oled.setRow(3);
  oled.setCol(1);
  oled.print(" 1  2  3  4  5  6");
  oled.clearToEOL();
  oled.setRow(4);
  oled.setCol(1);
  for (byte i = 0; i < MOISTURE_SENSORS_MAX_COUNT; i++) {
    if (screenPosition == i) {
      oled.print(">");
    } else {
      oled.print(" ");
    }
    if (bitRead(enabledMoistureSensors, i)) {
      oled.print("A");
    } else {
      oled.print("N");
    }
      oled.print(" ");
    }
  oled.clearToEOL();
}

void calibrationMenuScreen() {
  oled.setRow(1);
  oled.setCol(1);
  oled.print("Kalibracia senzorov");
  oled.clearToEOL();

  for (byte i = 0; i < MOISTURE_SENSORS_MAX_COUNT; i++) {
    oled.setRow(i + 2);
    oled.setCol(1);
    if (screenPosition == i) {
      oled.print("*");
    } else {
      oled.print(" ");
    }

    char line[21];
    sprintf(line, "S%1d - od %3d do %3d", i+1, moistureMin[i], moistureMax[i]);
    oled.print(line);
    oled.clearToEOL();
  }
}

void calibrateSensorScreen() {
  oled.setRow(1);
  oled.setCol(1);
  oled.print("Kalibracia senzora ");
  oled.print(screenPosition + 1);
  oled.clearToEOL();

  oled.setRow(3);
  oled.setCol(1);
  oled.print("Aktualna raw: ");
  oled.print(measureRaw(screenPosition));
  oled.clearToEOL();

  oled.setRow(5);
  oled.setCol(1);
  oled.print("Ulozena min: ");
  oled.print(moistureMin[screenPosition]);
  oled.clearToEOL();

  oled.setRow(6);
  oled.setCol(1);
  oled.print("Ulozena max: ");
  oled.print(moistureMax[screenPosition]);
  oled.clearToEOL();
}

/************
 * HARDWARE *
 ************/

int measureRaw(byte idx) {
  int soilHumidity = 0; // return lowest possible value, if error in code it will help to not watering all time  
  if (bitRead(enabledMoistureSensors, idx)) {
    digitalWrite(MOISTURE_GROUP1_CONTROL, HIGH);
    digitalWrite(MOISTURE_GROUP2_CONTROL, HIGH);

    if (idx >= 0 && idx < 3) {
      digitalWrite(MOISTURE_GROUP1_CONTROL, LOW);
      delay(50);
    } else if (idx >= 3 && idx <= 5) {
      digitalWrite(MOISTURE_GROUP2_CONTROL, LOW);
      delay(50);
    }
    switch(idx) {
      case 0:
        soilHumidity = analogRead(MOISTURE_SENSOR_1);
        break;
      case 1:
        soilHumidity = analogRead(MOISTURE_SENSOR_2);
        break;
      case 2:
        soilHumidity = analogRead(MOISTURE_SENSOR_3);
        break;
      case 3:
        soilHumidity = analogRead(MOISTURE_SENSOR_4);
        break;
      case 4:
        soilHumidity = analogRead(MOISTURE_SENSOR_5);
        break;
      case 5:
        soilHumidity = analogRead(MOISTURE_SENSOR_6);
        break; 
    }
    digitalWrite(MOISTURE_GROUP1_CONTROL, HIGH);
    digitalWrite(MOISTURE_GROUP2_CONTROL, HIGH);
  }

  return soilHumidity;
}
 
byte measure(byte idx) {
  byte soilHumidity = 100; // return max humidity, help to prevent watering if mistake in code
  if (bitRead(enabledMoistureSensors, idx)) {
    int raw = measureRaw(idx);
    raw = max(raw, moistureMin[idx]); // at least min calibrated value;
    raw = min(raw, moistureMax[idx]); // no more than max calibrated value; 
    soilHumidity = map(raw, moistureMin[idx], moistureMax[idx], 100, 0); // change moisture analog value to 0 - 100
  } 
  return soilHumidity;
}
 
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

void loadEEPROMvariables() {
  byte sleepByte = EEPROM.read(SLEEP_EEPROM_ADDR);
  byte sleepByteXor = EEPROM.read(SLEEP_EEPROM_ADDR + 1);

  if ((255 ^ sleepByte) != sleepByteXor) { // if checksum (xor) value is not valid, set default
    sleepByte = 2;
  }
  
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

  byte enabledMoistureSensorsByte = EEPROM.read(ENABLE_MOISTURE_EEPROM_ADDR);
  byte enabledMoistureSensorsByteXor = EEPROM.read(ENABLE_MOISTURE_EEPROM_ADDR + 1);

  if ((255 ^ enabledMoistureSensorsByte) != enabledMoistureSensorsByteXor) { // if checksum (xor) value is not valid, set default
    enabledMoistureSensorsByte = 0b11000000; // default turn off all sensors
  }
  enabledMoistureSensors = enabledMoistureSensorsByte;
}

void wakeUp() {
  if (sleeping) {
    noInterrupts();
    sleeping = false;
    sleep_disable();
    power_all_enable();
    ADCSRA |= (1 << ADEN); // wake up ADC
    interrupts();
    oled.clear();
  }
}

void putToSleep() {
  noInterrupts();
  digitalWrite(MOISTURE_GROUP1_CONTROL, HIGH); // turn off group1 of capacitive soil moisture sensors v1.2 using P-Channel MOSFET BS250, save around 15mA
  digitalWrite(MOISTURE_GROUP2_CONTROL, HIGH); // turn off group2 of capacitive soil moisture sensors v1.2 using P-Channel MOSFET BS250, save around 15mA
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
