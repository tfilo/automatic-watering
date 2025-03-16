# Automatic watering
This is code for my DIY ATmega328p based watering system with monitoring soil temperature and humidity. In EasyEDA - schema folder, there is schematics for this project. It is not tested design and it can have some flaws.

project is unfinished due to hardware problem. Cheap dc pumps caused reset of microcontroler. It would require separated circuit for powering dc pumps with opto-isolator.

This source code use following libraries you must add by yourself:

**DallasTemperature**
https://github.com/milesburton/Arduino-Temperature-Control-Library

**OneWire**
http://www.pjrc.com/teensy/td_libs_OneWire.html

**Sodaq_DS3231**
https://github.com/SodaqMoja/Sodaq_DS3231

**SSD1306Ascii**
https://github.com/greiman/SSD1306Ascii
