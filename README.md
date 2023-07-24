# Basicmac-STM32WLE5.
Basicmac version for SEEED LoRa-E5-mini
This version of basicmac will work with the SEEED Lora-E5-HF module.
The testprogram sends a test packet to TTN every 10 minutes.
Edit src/LoRa_Device_01.h for your end node.
# Demonstration program.
The test program in main.c sends a short data packet to TTN.  After completion, the modules goes into deep sleep (shutdown).  The data in the RTC back-up registers will be preserved till the wake-up.  The RTC clock keeps running.  Note that the RTC clock is not synchronized with the real date/time.  It will start at 01-01-2023 00:00:00 after power-up.
# Debugging on serial.
There is a lot of debug info on serial line TX2 (PA2).
The lmic messages can be eliminated by commenting out the definitions of CFG_DEBUG_VERBOSE and CFG_DEBUG in ..../IBM LMIC framework/src/hal/target-config.h.
There is also a definition of "DEBUG" in the demonstration main.cpp source, which can be set to false to stop application debug output.
# Upload with ST-Link.
In deep sleep mode, the upload of the program may cause some problems.  Press and hold the reset button and start the upload.  As soon as the LED on the ST-Link starts to flash, release the reset button.
# Example PCB.
I have made a print for this module.  It has a I2C bus to connect a sensor.  The power to the I2C bus is switched by a FET to reduce current during sleep to 4.5 microAmp.  The print looks like this:
![image](https://github.com/Edzelf/Basicmac-STM32WLE5/assets/18257026/30304268-b8b1-4a99-b811-494230a3aa9c)
