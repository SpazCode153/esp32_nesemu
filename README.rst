ESP32-NESEMU, a Nintendo Entertainment System emulator for the ESP32
====================================================================

This is fork from the repository badvision/esp32_nesemu which is based on the espressif's nesemu port. I created this fork as the repository badvision/esp32_nesemu was updated a long time ago
and I struggled to build it using VSCode and EDP-IDF 4.4. I tried to use PlatformIO but that too gave alot of errors when building so I "converted" the source code to use
just VSCode and the ESP-IDF. 

Please note I am still learning C and C++ therefor the way that I have done the Includes and CMakeLists.txt may be incorrect but it works. For now please review the badvision/esp32_nesemu for additional
information until I have update all the information here.

Compiling
---------

Use VSCode with the ESP-IDF 4.4 framework and extension. Please alter the pin settings in the SDK configuration under the menu "NES Component Settings". At moment the code 
only works with a SD card, I am busy working on all the settings.

Display
-------

To display the NES output, please connect a 320x240 ili9341-based SPI display to the ESP32, specify the pins in teh SDK configuration under "NES Component Settings"

PSX Controller
--------------

I haven't tested this yet but there are settings for this in the SDK configuration under "NES Component Settings"

GPIO Controller
---------------

If you'd like to use your own buttons, specify the options in the SDK configuration under "NES Component Settings"

Sound
-----

Connect one Speaker-Pin to GPIO 25 and the other one to GND.  I recommend using a Class-D amplifier to boost the volume coming out of this because it's not going to be very loud on its own.

ROM
---

This includes no Roms. You'll have to put your own Roms on a SD card and modify the roms.txt (see /data) according to your needs.
Don't change format used in roms.txt because you might cause the menu to load incorrectly.  Review the file for further instructions.

Copyright
---------

Code in this repository is Copyright (C) 2016 Espressif Systems, licensed under the Apache License 2.0 as described in the file LICENSE. Code in the components/nofrendo is Copyright (c) 1998-2000 Matthew Conte (matt@conte.com) and licensed under the GPLv2.
Any changes in this repository are otherwise presented to you copyright myself and lisensed under the same Apache 2.0 license as the Espressif Systems repository.
