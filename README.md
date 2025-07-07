# leagend-BM6-battery-monitor-BLE-ESP32
BLE 12V Monitoring device connected to read the Voltage

Battery Guard from intact (and many many other suppliers) is equivalent to the **BM6 from Leagend** who seems to be the OEM.
https://leagend.com/products/bm6

The BM6 from leagend is the perfect solution for monitoring 12V automotive batteries while consuming only 0.94mA. 
It provides a real-time display of battery power, voltage, and temperature and is customizable. 
intact provides a nice app that allows you to connect via BLE and read and set several values.
The BM6 is connected in parallel with the 12V battery.

After spending a week reverse engineering the Java code for the Android app, I was ready to give up.
I might never have found the key if it weren't for this post: https://www.tarball.ca/posts/reverse-engineering-the-bm6-ble-battery-monitor/
from https://github.com/JeffWDH. Unfortunately, he hadn't posted it on GitHub at that time. So, it was challenging to find it.

After I found Jeffs post, it was just a mattery of converting the AES-key to C and using an existing and well documented AES-Stack for ESP32.
Within 2 days the correct values started flowing in.

The data the BM6 returns is AES-encrypted. Additionally, the app uses different keys to encrypt all data sent back to China.
While reverse-engineering the Android APP I found several keys. Most of them are readable ASCII strings. 
Only the AES-key for the BLE device is stored in hex and this was the reason I had no success...

After connecting and signing up for notifications and initiating transmission by writing a command, the data will be received every second until the device gets disconnected.

Below is a fully functioning test program that will be imported into a Camper-Monitoring-Display project.

The code below extracts the voltage, temperature, and SOC. It connects only for a short time, so that the app can be used as well. 
As with most BLE devices, only one device can connect at a time.

## Important:
As the 2.4GHz Frequenz is used by a lot of devices, it may happen that a connection / reconnection fails. Therefore it is advisable to try a few times. 
This has been added to re-connect and connect.
The device requests connection parameters that I have set to the same value during initialisation [updateConnParams(6,12,0,150)]. This will save a little time while connecting.
The BM**5** from leagend uses a different communication(!)
The NimBLE-Arduino BLE stack (2.3.2) is used for the connection. https://github.com/h2zero/NimBLE-Arduino

The ESP-32 Dev Kit 4 is used as a hardware platform.

Arduino-IDE 2.3.6 compiled it nicely..

## Negative:
The Android app reports GPS and live data back to leagend (China), any time it is used.

Why would anyone want or even care about this data?

If the IOS app does it as well, I don't know.

