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
#define ONE_WIRE_BUS_PIN 13

#define BTN_INTERRUPT_PIN 3
#define RTC_INTERRUPT_PIN 2
#define BTN_SET 8
#define BTN_UP 7
#define BTN_DOWN 2
#define NO_BTN 0

#define MOISTURE_SENSOR_1 A0 // group 1
#define MOISTURE_SENSOR_2 A1 // group 1
#define MOISTURE_SENSOR_3 A2 // group 1
#define MOISTURE_SENSOR_4 A0 // group 2
#define MOISTURE_SENSOR_5 A1 // group 2
#define MOISTURE_SENSOR_6 A2 // group 2

#define BATTERY_STATUS A3

#define PUMP_1_PIN 12
#define PUMP_2_PIN 11
#define PUMP_3_PIN 10
#define PUMP_4_PIN 9
#define PUMP_5_PIN 5
#define PUMP_6_PIN 6

#define MOISTURE_GROUP1_CONTROL 4
#define MOISTURE_GROUP2_CONTROL 1

#define MAX_SUPPORTED_POTS 6 // This is max limit for 3 groups of sensors by two sensors in group, there are used all pins for digital pins for pump control too
#define EXIT_SENSOR_CALIBRATION_MENU MAX_SUPPORTED_POTS

#define MAIN_SCREEN 0
#define MENU_SCREEN 1
#define TIME_SETUP_SCREEN 2
#define SLEEP_SETUP_SCREEN 3
#define SENSORS_ENABLE_SCREEN 4
#define MENU_SENSORS_CALIBRATION_SCREEN 5

#define SENSOR_CALIBRATION_SCREEN 10

#define POSITION_DEFAULT 0
#define MENU_TIME_POSITION POSITION_DEFAULT
#define MENU_SLEEP_POSITION 1
#define MENU_SENSORS_ENABLE_POSITION 2
#define MENU_SENSOR_CALIBRATION_POSITION 3
#define MENU_EXIT_POSITION 4

#define TIME_YEAR_POSITION POSITION_DEFAULT
#define TIME_MONTH_POSITION 1
#define TIME_DAY_POSITION 2
#define TIME_HOUR_POSITION 3
#define TIME_MINUTE_POSITION 4

#define SENSOR_ENABLE_POSITION POSITION_DEFAULT

#define SLEEP_EEPROM_ADDR 0
#define ENABLED_SENSORS_EEPROM_ADDR 5
#define MIN_MOISTURE_EEPROM_ADDR 10
#define MAX_MOISTURE_EEPROM_ADDR 30

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

unsigned int sleepAfter;

boolean initialWatering = true;

byte enabledSensors; // up to 6 sensors (0-5), ignore bit 6 and 7 (0 == false)
int moistureMin[] = {0, 0, 0, 0, 0, 0};
int moistureMax[] = {1023, 1023, 1023, 1023, 1023, 1023};

bool markedPot[] = {false, false, false, false, false, false};
 
DateTime now;
OneWire oneWire(ONE_WIRE_BUS_PIN);
DallasTemperature tempSensor(&oneWire);
SSD1306AsciiAvrI2c oled;

#define SLEEP 0
#define WAKED_BY_USER 1
#define WAKED_BY_RTC 2

byte status = WAKED_BY_USER; // default jump to user mode

void setup(void)
{
  analogReference(INTERNAL);
  pinMode(MOISTURE_GROUP1_CONTROL, OUTPUT);
  pinMode(MOISTURE_GROUP2_CONTROL, OUTPUT);
  digitalWrite(MOISTURE_GROUP1_CONTROL, HIGH); // keep sensors turned off
  digitalWrite(MOISTURE_GROUP2_CONTROL, HIGH); // keep sensors turned off
  tempSensor.begin();
  rtc.begin();
  oled.begin(&Adafruit128x64, I2C_OLED_ADDRESS);
  oled.setFont(Adafruit5x7);
  oled.clear();
  
  setupButtons();
  attachInterrupt(digitalPinToInterrupt(BTN_INTERRUPT_PIN), pressInterrupt, FALLING);
  attachInterrupt(digitalPinToInterrupt(RTC_INTERRUPT_PIN), rtcInterrupt, FALLING);
  loadEEPROMvariables();
}

void loop(void)
{
  unsigned char btn = lastButton;
  lastButton = NO_BTN;

  if (status==WAKED_BY_USER) {
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
        break;
      case SENSORS_ENABLE_SCREEN:
        sensorsEnableScreen();
        break;
      case MENU_SENSORS_CALIBRATION_SCREEN:
        sensorsCalibrationMenuScreen();
        break;
      case SENSOR_CALIBRATION_SCREEN:
        sensorCalibrationScreen();
        break;
    }

    if (millis() - lastBtnPress > sleepAfter) {
      putToSleep();
    } else {
      delay(100);
    }
  } else if (status==WAKED_BY_RTC) {
    wateringScreen();

    if (btn!=0) { // STOP with any button
      for (int i=0; i<MAX_SUPPORTED_POTS; i++) {
        markedPot[i] = false;
      }
      putToSleep();
    }
    
    if (initialWatering) {
      initialWatering = false;
      // TODO Check every pot and if value (%) is lower than specified mark pot for watering
    } else {
      // Print to screen that system is checking pots. Check every marked pot if value is higher than specified, if true is watered and unmark this pot
      // Water every marked pot for 5 seconds, ONE by ONE !!!
      // if no pot marked anymore, than putToSleep() else delay(30000L);
      delay(30000L);
    }
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
      case MENU_SENSORS_ENABLE_POSITION:
        actualScreen = SENSORS_ENABLE_SCREEN;
        screenPosition = POSITION_DEFAULT;
        break;
      case MENU_SENSOR_CALIBRATION_POSITION:
        actualScreen = MENU_SENSORS_CALIBRATION_SCREEN;
        screenPosition = POSITION_DEFAULT;
        while (screenPosition != EXIT_SENSOR_CALIBRATION_MENU && bitRead(enabledSensors, screenPosition)==0) { // if disabled, jump to next enabled in same direction
          screenPosition++;
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

  if (actualScreen == SENSORS_ENABLE_SCREEN) {
    if (screenPosition >= MAX_SUPPORTED_POTS - 1) {
      EEPROM.write(ENABLED_SENSORS_EEPROM_ADDR, enabledSensors);
      EEPROM.write(ENABLED_SENSORS_EEPROM_ADDR + 1, 255 ^ enabledSensors);
      actualScreen = MENU_SCREEN;
      screenPosition = POSITION_DEFAULT;
    } else {
      screenPosition++;  
    } 
    return;
  }

  if (actualScreen == MENU_SENSORS_CALIBRATION_SCREEN) {
    if (screenPosition == EXIT_SENSOR_CALIBRATION_MENU) {
      actualScreen = MENU_SCREEN;
      screenPosition = POSITION_DEFAULT;
    } else {
      actualScreen = SENSOR_CALIBRATION_SCREEN;
      // don't set screenPosition because it will be used as sensor index
    }
    return;
  }

  if (actualScreen == SENSOR_CALIBRATION_SCREEN) {
    actualScreen = MENU_SENSORS_CALIBRATION_SCREEN;

    EEPROM.put(MIN_MOISTURE_EEPROM_ADDR + (screenPosition * 2), moistureMin[screenPosition]);
    EEPROM.put(MAX_MOISTURE_EEPROM_ADDR + (screenPosition * 2), moistureMax[screenPosition]);
    int sumMin = 0;
    int sumMax = 0;
    for (byte i = 0; i < sizeof(moistureMin) / sizeof(int); i++) {
      sumMin+=moistureMin[i];
      sumMax+=moistureMax[i]; //MUST be same length to moistureMin !!!!
    }
    EEPROM.put(MIN_MOISTURE_EEPROM_ADDR + 12, sumMin);
    EEPROM.put(MAX_MOISTURE_EEPROM_ADDR + 12, sumMax);
    
    // don't set screenPosition because it will be used as sensor index in menu
    return;
  }
}

void handleUpButton() {
  if (actualScreen == MENU_SCREEN) {
    if (screenPosition == POSITION_DEFAULT) {
      screenPosition = MENU_EXIT_POSITION;
    } else {
      screenPosition--;
      if (screenPosition == MENU_SENSOR_CALIBRATION_POSITION && enabledSensors == 192) { // 192 means that all sensors are turned off (defaul value 11000000)
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

  if (actualScreen == SENSORS_ENABLE_SCREEN) {
    enabledSensors = (enabledSensors ^ (1 << screenPosition)); // reverse bit on specified possition
    return;
  }

  if (actualScreen == MENU_SENSORS_CALIBRATION_SCREEN) {
    if (screenPosition == POSITION_DEFAULT) {
      screenPosition = EXIT_SENSOR_CALIBRATION_MENU;
    } else {
      screenPosition--;
    }
    while (screenPosition != EXIT_SENSOR_CALIBRATION_MENU && bitRead(enabledSensors, screenPosition)==0) { // if disabled, jump to next enabled in same direction, don't check for exit to menu idx
      screenPosition--;
      if (screenPosition < POSITION_DEFAULT) {
        screenPosition = EXIT_SENSOR_CALIBRATION_MENU;
      }
    }
    return;
  }

  if (actualScreen == SENSOR_CALIBRATION_SCREEN) {
    moistureMax[screenPosition] = measureRaw(screenPosition);
    return;
  }
}

void handleDownButton() {
  if (actualScreen == MENU_SCREEN) {
    if (screenPosition == MENU_EXIT_POSITION) {
      screenPosition = POSITION_DEFAULT;
    } else {
      screenPosition++;
      if (screenPosition == MENU_SENSOR_CALIBRATION_POSITION && enabledSensors == 192) { // 192 means that all sensors are turned off (defaul value 11000000)
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

  if (actualScreen == SENSORS_ENABLE_SCREEN) {
    enabledSensors = (enabledSensors ^ (1 << screenPosition)); // reverse bit on specified possition
    return;
  }

  if (actualScreen == MENU_SENSORS_CALIBRATION_SCREEN) {
    if (screenPosition == EXIT_SENSOR_CALIBRATION_MENU) {
      screenPosition = POSITION_DEFAULT;
    } else {
      screenPosition++;
    }
    while (screenPosition != EXIT_SENSOR_CALIBRATION_MENU && bitRead(enabledSensors, screenPosition)==0) { // if disabled, jump to next enabled in same direction, don't check for exit to menu idx
      screenPosition++;
      if (screenPosition > EXIT_SENSOR_CALIBRATION_MENU) {
        screenPosition = POSITION_DEFAULT;
      }
    }
    return;
  }

  if (actualScreen == SENSOR_CALIBRATION_SCREEN) {
    moistureMin[screenPosition] = measureRaw(screenPosition);
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
  if (screenPosition == MENU_SENSORS_ENABLE_POSITION) {
    oled.print("*");
  } else {
    oled.print(" ");
  }
  oled.print("Zapnut senzory");
  oled.clearToEOL ();

  oled.setRow(row++);
  oled.setCol(1);
  if (screenPosition == MENU_SENSOR_CALIBRATION_POSITION) {
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

  float batteryInput = analogRead(BATTERY_STATUS) * (1.1 / 1023.0);
  float batteryVoltage = (batteryInput * 1220.0) / 220.0;
  
  tempSensor.requestTemperatures(); // Send the command to get temperatures
  now = rtc.now();
  char line[21];
  sprintf(line, "%02d:%02d %02d.%02d.%02d B%.1fV", now.hour(), now.minute(), now.date(), now.month(), (now.year() % 100), batteryVoltage);

  oled.setRow(row++);
  oled.setCol(1);
  oled.print(line);
  oled.clearToEOL ();

  row++;
  
  oled.setRow(row++);
  oled.setCol(1);
  oled.print("Teplota: ");
  oled.print(tempSensor.getTempCByIndex(0), 2);
  oled.print("C");
  oled.clearToEOL ();

  row++;
  
  char moisture[6][7];
  for (byte i = 0; i<MAX_SUPPORTED_POTS; i++) {
    if (bitRead(enabledSensors, i)) {
      byte value = measure(i);
      sprintf(moisture[i], "V%1d:%3d", i + 1, measure(i));
    } else {
      sprintf(moisture[i], "V%1d:off", i + 1);
    }
  }

  oled.setRow(row++);
  oled.setCol(1);
  sprintf(line, "%s %s %s", moisture[0], moisture[1] ,moisture[2]);
  oled.print(line);
  oled.clearToEOL ();

  oled.setRow(row++);
  oled.setCol(1);
  sprintf(line, "%s %s %s", moisture[3], moisture[4] ,moisture[5]);
  oled.print(line);
  oled.clearToEOL ();
  delay(500);
}

void sensorsEnableScreen() {
  oled.setRow(1);
  oled.setCol(1);
  oled.print("Zapnut senzory");
  oled.clearToEOL();      
  oled.setRow(3);
  oled.setCol(1);
  oled.print(" 1  2  3  4  5  6");
  oled.clearToEOL();
  oled.setRow(4);
  oled.setCol(1);
  for (byte i = 0; i < MAX_SUPPORTED_POTS; i++) {
    if (screenPosition == i) {
      oled.print(">");
    } else {
      oled.print(" ");
    }
    if (bitRead(enabledSensors, i)) {
      oled.print("A");
    } else {
      oled.print("N");
    }
      oled.print(" ");
    }
  oled.clearToEOL();
}

void sensorsCalibrationMenuScreen() {
  oled.setRow(1);
  oled.setCol(1);
  if (screenPosition == EXIT_SENSOR_CALIBRATION_MENU) {
    oled.print("<");
  } else {
    oled.print(" ");
  }
  oled.print("Kalibracia senzorov");
  oled.clearToEOL();

  for (byte i = 0; i < MAX_SUPPORTED_POTS; i++) {
    oled.setRow(i + 2);
    oled.setCol(1);
    if (screenPosition == i) {
      oled.print("*");
    } else {
      oled.print(" ");
    }
    if (bitRead(enabledSensors, i)) {
      char line[21];
      sprintf(line, "S%1d - od %3d do %3d", i+1, moistureMin[i], moistureMax[i]);
      oled.print(line);
      oled.clearToEOL();
    } else {
      oled.print("S");
      oled.print(i+1);
      oled.print(" off");
      oled.clearToEOL();
    }
  }
}

void sensorCalibrationScreen() {
  oled.setRow(1);
  oled.setCol(1);
  oled.print("Kalibracia senzora ");
  oled.print(screenPosition + 1);
  oled.clearToEOL();

  oled.setRow(3);
  oled.setCol(1);
  oled.print("Raw hodnota: ");
  oled.print(measureRaw(screenPosition));
  oled.clearToEOL();

  oled.setRow(5);
  oled.setCol(1);
  oled.print("Ulozena (min): ");
  oled.print(moistureMin[screenPosition]);
  oled.clearToEOL();

  oled.setRow(6);
  oled.setCol(1);
  oled.print("Ulozena (max): ");
  oled.print(moistureMax[screenPosition]);
  oled.clearToEOL();
}

void wateringScreen() {
  float batteryInput = analogRead(BATTERY_STATUS) * (1.1 / 1023.0);
  float batteryVoltage = (batteryInput * 1220.0) / 220.0;
  tempSensor.requestTemperatures(); // Send the command to get temperatures
  now = rtc.now();
  char line[21];
  sprintf(line, "%02d:%02d %02d.%02d.%02d B%.1fV", now.hour(), now.minute(), now.date(), now.month(), (now.year() % 100), batteryVoltage);
  oled.println(line);
  oled.clearToEOL ();
  oled.set2X();
  oled.println(" Polievam ");
  oled.set1X();
  oled.clearToEOL ();
  oled.println("Prerusit lubovolnym");
  oled.println("tlacitkom");
}

/************
 * HARDWARE *
 ************/

void setNextRTCInterrupt() {
  DateTime now = rtc.now();
  /* FOR production
  if (now.hour() < 10) { // if before 10:00 set next Interrupt to 10:00
    rtc.enableInterrupts(10,00,0);
  } else {
    rtc.enableInterrupts(20,00,0); // if after 10:00 set next Interrupt to 20:00 
  }*/
  // FOR TESTING
  if (now.hour() < 9) {
    rtc.enableInterrupts(9,00,0);
  } else if (now.hour() < 10) {
    rtc.enableInterrupts(10,00,0);
  } else if (now.hour() < 11) {
    rtc.enableInterrupts(11,00,0);
  } else if (now.hour() < 12) {
    rtc.enableInterrupts(12,00,0);
  } else if (now.hour() < 13) {
    rtc.enableInterrupts(13,00,0);
  } else if (now.hour() < 14) {
    rtc.enableInterrupts(14,00,0);
  } else if (now.hour() < 15) {
    rtc.enableInterrupts(15,00,0);
  } else {
    rtc.enableInterrupts(8,00,0);
  }
}

void postponeRTCInterrupt() {
  DateTime now = rtc.now();

  uint8_t hour = now.hour();
  uint8_t minute = now.minute();
  if (minute < 55) {
    minute += 5;  
  } else {
    minute = 0;
    if (hour < 23) {
      hour++;  
    } else {
      hour = 0;  
    }
  }
  
  rtc.enableInterrupts(hour,minute,0);
}

int measureRaw(byte idx) {
  int soilHumidity = 0; // return lowest possible value, if error in code it will help to not watering all time  
  if (bitRead(enabledSensors, idx)) {
    digitalWrite(MOISTURE_GROUP1_CONTROL, HIGH);
    digitalWrite(MOISTURE_GROUP2_CONTROL, HIGH);

    if (idx >= 0 && idx < 3) {
      digitalWrite(MOISTURE_GROUP1_CONTROL, LOW);
      delay(150);
    } else if (idx >= 3 && idx <= 5) {
      digitalWrite(MOISTURE_GROUP2_CONTROL, LOW);
      delay(150);
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
  if (bitRead(enabledSensors, idx)) {
    int raw = measureRaw(idx);
    raw = max(raw, moistureMin[idx]); // at least min calibrated value;
    raw = min(raw, moistureMax[idx]); // no more than max calibrated value; 
    soilHumidity = map(raw, moistureMin[idx], moistureMax[idx], 100, 0); // change moisture analog value to 0 - 100
  } 
  return soilHumidity;
}

void rtcInterrupt() {
  if (status==WAKED_BY_USER) {
    // if user is working in menu, try after 5 minute
    postponeRTCInterrupt();
  } else {
    status = WAKED_BY_RTC;
    initialWatering = true;
    setNextRTCInterrupt();
  }
}
 
void pressInterrupt() {
  if (status==WAKED_BY_RTC) {
    // If waked by RTC than don't change status, but track buttons, any button will stop/prevent watering
  } else {
    status = WAKED_BY_USER;
  }
  if (millis() - lastBtnPress < 300) { // Debounce 300ms
    return;
  }

  lastBtnPress = millis();

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
}

void setupButtons() {
  pinMode(BTN_SET, INPUT_PULLUP);
  pinMode(BTN_UP, INPUT_PULLUP);
  pinMode(BTN_DOWN, INPUT_PULLUP);
  pinMode(BTN_INTERRUPT_PIN, INPUT_PULLUP);
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

  byte enabledSensorsByte = EEPROM.read(ENABLED_SENSORS_EEPROM_ADDR);
  byte enabledSensorsByteXor = EEPROM.read(ENABLED_SENSORS_EEPROM_ADDR + 1);

  if ((255 ^ enabledSensorsByte) != enabledSensorsByteXor) { // if checksum (xor) value is not valid, set default
    enabledSensorsByte = 0b11000000; // default turn off all sensors
  }
  enabledSensors = enabledSensorsByte;

  int savedMinSum = 0;
  int savedMaxSum = 0;
  EEPROM.get(MIN_MOISTURE_EEPROM_ADDR, moistureMin);
  EEPROM.get(MAX_MOISTURE_EEPROM_ADDR, moistureMax);
  EEPROM.get(MIN_MOISTURE_EEPROM_ADDR + 12, savedMinSum);
  EEPROM.get(MAX_MOISTURE_EEPROM_ADDR + 12, savedMaxSum);
  int sumMin = 0;
  int sumMax = 0;
  for (byte i = 0; i < sizeof(moistureMin) / sizeof(int); i++) {
    sumMin+=moistureMin[i];
    sumMax+=moistureMax[i]; //MUST be same length to moistureMin !!!!
  }
  if (savedMinSum!=sumMin || savedMaxSum!=sumMax) {
     // Error in stored data, set all to default and save it
     sumMin = 0;
     sumMax = 0;
     for (byte i = 0; i < sizeof(moistureMin) / sizeof(int); i++) {
       moistureMin[i]=0;
       moistureMax[i]=1023;
       sumMin+=moistureMin[i];
       sumMax+=moistureMax[i];
     }
     EEPROM.put(MIN_MOISTURE_EEPROM_ADDR, moistureMin);
     EEPROM.put(MAX_MOISTURE_EEPROM_ADDR, moistureMax);
     EEPROM.put(MIN_MOISTURE_EEPROM_ADDR + 12, sumMin);
     EEPROM.put(MAX_MOISTURE_EEPROM_ADDR + 12, sumMax);
  }
}

void wakeUp() {
  lastButton = NO_BTN; // after waking up by any button don't do action assignet to button
  noInterrupts();
  sleep_disable();
  power_all_enable();
  ADCSRA |= (1 << ADEN); // wake up ADC
  interrupts();
  // analogReference(INTERNAL); // TODO only if makes higher consuption during sleep
  oled.begin(&Adafruit128x64, I2C_OLED_ADDRESS);
  oled.setFont(Adafruit5x7);
  oled.clear();
}

void putToSleep() {
  noInterrupts();
  digitalWrite(MOISTURE_GROUP1_CONTROL, HIGH); // turn off group1 of capacitive soil moisture sensors v1.2 using PNP S8550, save around 15mA
  digitalWrite(MOISTURE_GROUP2_CONTROL, HIGH); // turn off group2 of capacitive soil moisture sensors v1.2 using PNP S8550, save around 15mA
  oled.ssd1306WriteCmd(0xAE); // turn off oled
  // analogReference(EXTERNAL); // TODO only if makes higher consuption during sleep
  set_sleep_mode(SLEEP_MODE_PWR_DOWN); // set powerdown state for ATmega
  ADCSRA = 0; // put ADC to sleep, save around 0.250mA
  power_all_disable(); // put everything other to sleep
  sleep_enable();
  interrupts();
  status = SLEEP;
  sleep_cpu(); // When sleeping current goes down to 0.035mA
  wakeUp();
}
