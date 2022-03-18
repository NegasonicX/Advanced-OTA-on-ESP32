# Advanced OTA with Detailed Progress on ESP32
This is an advanced code to perform OTA ( Over The Air ) Firmware update on ESP32 either via Wifi or Ethernet Connection which would also show progress percentage.

# GPIO Functions:
I am using ENC28J60 Ethernet Module and Devkit v1's integrated Wifi.

* GPIO 2  : Builtin LED ( Devkit V1 )
* GPIO 18 : SCLK
* GPIO 23 : MOSI
* GPIO 19 : MISO
* GPIO 5 : CS
* SPI CLOCK MHz : 8MHz
* **1) SDK Configurations for SPI has to be done on individual level**
* **2) Change "Single Factory App, no OTA" to "Factory App, two OTA definitions" from the menuconfig settings.**

# Understanding the Flow:
* This code is developed for ESP32 on Embedded C Language using FreeRTOS.
* Having the ability to update or fix bugs in an exisiting firmwarm without the need to physically change it using hard wire is a gift in Embedded Systems.
* OTA or "Over The Air" update is a very well established methods for OEM Tech giants to update firmware of the existing devices on the field.
* The program has the provision to send URL and OTA Intialization commands via the MQTT "cmd topic".
* There are 2 MQTT Topics : "data topic" - for continuously publishing data if needed & "cmd topic" to perform OTA by sending commands to the client ( ESP32 ) from the MQTT Broker.
* Step 1 : Get the cmd topic which would look like : <mac_address/cmd>
* Step 2 : Send the OTA URL payload on the cmd topic as : {"OTA_URL":"<Paste Your OTA URL Here>"} ( it should be the "bin" file of the firmware )
* Step 3 : Send the OTA Initialization command on cmd topic as : {"START_OTA"}
* That's it, if the specificed OTA URL is correct and ESP32 has Internet Services, then via HTTP protocols, it will download the "bin" file and you can see the progress of the same on the serial monitor.
* Keep in mind, all the other RTOS task will be deleted while performing OTA and once successfully downloaded, ESP will create a partition and update the running firmware to the one provided in the bin file.
* Also, if the OTA fails in the middle or some other issues, ESP32 will just reboot and run on the existing firmware.

# Conclusion:
* There is nothing to conclude this time, it's super cool and works flawlessly ( unless you have not set the menuconfig properly and other including files like server certs files ).
* I hope you all like it :-D
  
