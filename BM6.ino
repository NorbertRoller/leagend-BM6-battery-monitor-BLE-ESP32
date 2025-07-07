/*
Battery Guard from intact (and many many other suppliers) is equivalent to the BM6 from Leagend who seems to be the OEM.
https://leagend.com/products/bm6

The BM6 from leagend is the perfect solution for monitoring 12V automotive batteries while consuming only 0.94mA.
It provides a real-time display of battery power, voltage, and temperature and is customizable. 
intact provides a nice app that allows you to connect via BLE and read and set several values.
The BM6 is connected in parallel with the 12V battery. 

After spending a week reverse engineering the Java code for the Android app, I was ready to give up.
I might never have found the key if it weren't for this post: https://www.tarball.ca/posts/reverse-engineering-the-bm6-ble-battery-monitor/
from https://github.com/JeffWDH. Unfortunately, he hadn't posted it on GitHub at that time. So, it was challenging to find it.

After I found Jeffs post, it was just a matter of converting the AES-key to C and using an existing and well documented AES-Stack for ESP32.
Within 2 days the correct values started flowing in.

The data the BM6 returns is AES-encrypted. Additionally, the app uses different keys to encrypt all data sent back to China.
While reverse-engineering the Android APP I found several keys. Most of them are readable ASCII strings. 
Only the AES-key for the BLE device is stored in hex and this was the reason I had no success...

After connecting and signing up for notifications and initiating transmission by writing a command, the data will be received every second until the device gets disconnected.

Below is a fully functioning test program that will be imported into a Camper-Monitoring-Display project.

The code below extracts the voltage, temperature, and SOC. It connects only for a short time, so that the app can be used as well. 
As with most BLE devices, only one device can connect at a time.

Important:
As the 2.4GHz Frequenz is used by a lot of devices, it may happen that a connection / reconnection fails. Therefore it is advisable to try a few times. 
This has been added to re-connect and connect.
The device requests connection parameters that I have set to the same value during initialisation [updateConnParams(6,12,0,150)]. This will save a little time while connecting.
The BM5 from leagend uses a different communication(!)

Negatives:
The Android app reports GPS and live data back to leagend (China), any time it is used.
Why would anyone want or even care about this data?
If the IOS App does it as well, I don't know.

The NimBLE-Arduino BLE stack (2.3.2) is used for the connection. https://github.com/h2zero/NimBLE-Arduino
The ESP-32 Dev Kit 4 is used as a hardware platform.
Arduino-IDE 2.3.6 compiled it nicely..
*/

// MAC: 38:3b:26:b5:15:a2
// Service 0xFFF0
// Notification Charcteristic 0xFFF4
// Enable send data 0xFFF3

#include "aes/esp_aes.h"

#include <Arduino.h>
#include <NimBLEDevice.h>
NimBLEScan *pBLEScan;
NimBLEAddress MAC_BatteryGuard("38:3b:26:b5:15:a2", 0); // 0=Public, 1= Random --- BLE_ADDR_PUBLIC (0) BLE_ADDR_RANDOM (1)
NimBLEClient* pClient_BM6 = nullptr;    

uint lastAdv = millis();
static bool doConnect_BM6 = false;
#define nullptr NULL
unsigned int Battery_Guard_Volt=0;          // Volt of Battery
byte Battery_Guard_T=0;                     // Temperatur at Sensor Housing
byte Battery_Guard_SOC=0;                   // SOC of Battery

char BM6_CMD[16]= {0x69, 0x7e, 0xa0, 0xb5, 0xd5, 0x4c, 0xf0, 0x24, 0xe7, 0x94, 0x77, 0x23, 0x55, 0x55, 0x41, 0x14};   // Command to start send after Notify signup
// 0x69, 0x7e, 0xa0, 0xb5, 0xd5, 0x4c, 0xf0, 0x24, 0xe7, 0x94, 0x77, 0x23, 0x55, 0x55, 0x41, 0x14
int BM6_CMD_Length = 16;

// Java byte[] SecretKey_f17746a = {108, 101, 97, 103, 101, 110, 100, -1, -2, 48, 49, 48, 48, 48, 48, 57}; // Secret Key ASCII "leagend,xFD,xFE,0100009"
// in Hex 0x6C, 0x65, 0x61, 0x67, 0x65, 0x6E, 0x64, 0xFF, 0xFE, 0x30, 0x31, 0x30, 0x30, 0x30, 0x30, 0x39
// AES SecretKey_f17746a for C++
byte aesKey[16] = { 0x6C, 0x65, 0x61, 0x67, 0x65, 0x6E, 0x64, 0xFF, 0xFE, 0x30, 0x31, 0x30, 0x30, 0x30, 0x30, 0x39 }; // Secret Key ASCII "leagend,xFF,xFE,0100009"

// Initalising Vektor (IV) for CBC-Mode
byte iv[16] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };

// Init AES-Context
esp_aes_context aes_ctx;            // Set only once! Otherwise the output will not update

static const NimBLEAdvertisedDevice* advDevice_BM6;

/**  None of these are required as they will be handled by the library with defaults. **
 **                       Remove as you see fit for your needs                        */
class ClientCallbacks : public NimBLEClientCallbacks 
  {
  void onConnect(NimBLEClient* pClient) 
    {
    Serial.println("Connected");
    /** After connection we should change the parameters if we don't need fast response times.
     *  These settings are 150ms interval, 0 latency, 450ms timout.
     *  Timeout should be a multiple of the interval, minimum is 100ms.
     *  I find a multiple of 3-5 * the interval works best for quick response/reconnect.
     *  Min interval: 120 * 1.25ms = 150, Max interval: 120 * 1.25ms = 150, 0 latency, 60 * 10ms = 600ms timeout
    */
    pClient->updateConnParams(6,12,0,150);
    };

  void onDisconnect(NimBLEClient* pClient, int reason) 
    {
    Serial.printf("%s Disconnected, reason = %d \n",
    pClient->getPeerAddress().toString().c_str(), reason);
    };

    /** Called when the peripheral requests a change to the connection parameters.
     *  Return true to accept and apply them or false to reject and keep
     *  the currently used parameters. Default will return true.
     */
  bool onConnParamsUpdateRequest(NimBLEClient* pClient, const ble_gap_upd_params* params) 
    {
    Serial.println("ClientCallback: onConnParamsUpdateRequest");
    Serial.printf("Device MAC address: %s\n", pClient->getPeerAddress().toString().c_str());
     
    Serial.print("onConnParamsUpdateRequest itvl_min, New Value: ");    //requested Minimum value for connection interval 1.25ms units
    Serial.println(params->itvl_min,DEC);

    Serial.print("onConnParamsUpdateRequest itvl_max, New Value: ");    //requested Minimum value for connection interval 1.25ms units
    Serial.println(params->itvl_max,DEC);

    Serial.print("onConnParamsUpdateRequest latency, New Value: ");
    Serial.println(params->latency,DEC);

    Serial.print("onConnParamsUpdateRequest supervision_timeout, New Value:");
    Serial.println(params->supervision_timeout,DEC);
      
    Serial.printf("NimBLE: onConnParamsUpdateRequest accepted. MAC address: %s\n", pClient->getPeerAddress().toString().c_str());
    return true;
  };
};

class scanCallbacks: public NimBLEScanCallbacks 
  {
  void onResult(const NimBLEAdvertisedDevice* advertisedDevice) override 
    {
    printf("NimBLE_scan: Discovered Device: %s Type: %d \n", advertisedDevice->toString().c_str(),advertisedDevice->getAddressType());
    /*  printf("getAddress: %s\n",advertisedDevice->getAddress().toString().c_str());
      printf("Anz Services: %d\n\n",advertisedDevice->getServiceUUIDCount());
      Serial.println(advertisedDevice->getServiceUUID(0).toString().c_str());
      Serial.println(advertisedDevice->getServiceUUID(1).toString().c_str());
      Serial.println(advertisedDevice->getServiceUUID(2).toString().c_str()); */

    // Check for Batterie Monitor BM6 MAC Adresse
    if (advertisedDevice->getAddress().equals(MAC_BatteryGuard))  // Discovered MAC == Target MAC ?
      {Serial.println("NimBLE_scan: Battery Guard found at MAC-Adress");
      if(advertisedDevice->isAdvertisingService(NimBLEUUID("FFF0"))) // Check for Battery Guard Service
        {
        Serial.println("NimBLE_scan: Found Battery Guard - BLE Service");
        advDevice_BM6 = advertisedDevice;
        doConnect_BM6 = true;
        Serial.println("NimBLE_scan: doConnect_BM6 = true");
        }
      }
    }

  void onScanEnd(const NimBLEScanResults& results, int reason) override
    {
    Serial.print("Scan Ended; reason = "); Serial.println(reason);
    }
  } scanCallbacks;

/** Notification / Indication receiving handler callback */
void notifyCB_BM6(NimBLERemoteCharacteristic* pRemoteCharacteristic, uint8_t* pData, size_t length, bool isNotify)
  {
  std::string str = (isNotify == true) ? "Notification" : "Indication";
  str += " from ";
  /** NimBLEAddress and NimBLEUUID have std::string operators */
  str += std::string(pRemoteCharacteristic->getClient()->getPeerAddress());
  str += ": Service = " + std::string(pRemoteCharacteristic->getRemoteService()->getUUID());
  str += ", Characteristic = " + std::string(pRemoteCharacteristic->getUUID());
  str += ", Value = " + std::string((char*)pData, length);
  Serial.println(str.c_str());
  Serial.printf("Length in Hex: %x Dec: %d\n",length,length);

  // for (int laenge=0; laenge<length; laenge++)   // Value per line
  //  {Serial.printf("Value%d in Hex %x, Dez %d\n",laenge, pData[laenge],pData[laenge]);}
  Serial.print("BM6 received text: ");
  for (int laenge=0; laenge<length; laenge++)      // Value per line
    {Serial.printf("0x%0X, ",pData[laenge]);}
  Serial.println(" ");
    
  // AES CBC decryption
  // esp_aes_context aes_ctx;             // set only once !!!
  // esp_aes_init(&aes_ctx);
  // esp_aes_setkey(&aes_ctx, aesKey, 128);

  uint8_t decryptedtext[16];
  uint8_t pDatay[16];

  memcpy(pDatay, pData, 16);   
    
  // AES CBC Entschlüsselung
  esp_aes_crypt_cbc(&aes_ctx, ESP_AES_DECRYPT, sizeof(pDatay), iv, pDatay, decryptedtext);

  Serial.print("Decrypted text: ");
  for (int i = 0; i < 16; i++) 
    {
    Serial.printf("%02x ", decryptedtext[i]);
    iv[i]=0;                                    // Reset IV as it gets corrupted during Decrypt
    }
  Serial.println();
  // esp_aes_free(&aes_ctx);
      
  Battery_Guard_T=decryptedtext[4];                         // Temperatur Sensor-Housing
  Battery_Guard_SOC=decryptedtext[6];                       // SOC Battery - State of Charge mostly useless
  Battery_Guard_Volt=decryptedtext[7]*256+decryptedtext[8]; // Voltage of Battery
  Serial.printf(" Temp: %d, SOC: %d, Volt: %d\n", Battery_Guard_T,Battery_Guard_SOC,Battery_Guard_Volt);

  delay(2000);    // wait for several dataset
// keep alive if all values 0
  if(Battery_Guard_T !=0 && Battery_Guard_SOC !=0 && Battery_Guard_Volt != 0) // First dataset is sometimes 0 so wait for next
    {
    if(pClient_BM6)                       // Only 1 reading required, Disconnect if active
      {pClient_BM6->disconnect();
      Serial.println("BM6-BLE disconnect");}
    }
}

/** Create a single global instance of the callback class to be used by all clients */
static ClientCallbacks clientCB;

/** Handles the provisioning of clients and connects / interfaces with the server */
bool connectToServer_BM6() 
  {
    /** Check if we have a client we should reuse first **/
    if(NimBLEDevice::getCreatedClientCount()) 
      {
        /** Special case when we already know this device, we send false as the
         *  second argument in connect() to prevent refreshing the service database.
         *  This saves considerable time and power.
         */
      pClient_BM6 = NimBLEDevice::getClientByPeerAddress(advDevice_BM6->getAddress());
      if(pClient_BM6)
        {
        if(!pClient_BM6->connect(advDevice_BM6, false))
          {
          Serial.println("1. BM6-Reconnect failed");
          if(!pClient_BM6->connect(advDevice_BM6, false))
            {
            Serial.println("2. BM6-Reconnect failed");
            return false;
            }
          }
        Serial.println("BM6-Reconnected client");
        }
        /** We don't already have a client that knows this device,
         *  we will check for a client that is disconnected that we can use.
         */
      else
        {
        pClient_BM6 = NimBLEDevice::getDisconnectedClient();
        }
      }

    /** No client to reuse? Create a new one. */
    if(!pClient_BM6) 
      {
      if(NimBLEDevice::getCreatedClientCount() >= NIMBLE_MAX_CONNECTIONS) 
        {
        Serial.println("Max clients reached - no more connections available");
        return false;
        }

      pClient_BM6 = NimBLEDevice::createClient();

      Serial.println("New client created");

      pClient_BM6->setClientCallbacks(&clientCB, false);
      /** Set initial connection parameters: These settings are 15ms interval, 0 latency, 120ms timout.
       *  These settings are safe for 3 clients to connect reliably, can go faster if you have less
       *  connections. Timeout should be a multiple of the interval, minimum is 100ms.
       *  Min interval: 12 * 1.25ms = 15, Max interval: 12 * 1.25ms = 15, 0 latency, 51 * 10ms = 510ms timeout
       */
      pClient_BM6->setConnectionParams(6,12,0,150);
      /** Set how long we are willing to wait for the connection to complete (milliseconds), default is 30000. */
      pClient_BM6->setConnectTimeout(4 * 1000);

      if (!pClient_BM6->connect(advDevice_BM6)) // Created a client but failed to connect, don't need to keep it as it has no data
        {
        Serial.println("1. BM6-connect failed");
        if (!pClient_BM6->connect(advDevice_BM6)) // Try again
          {
          NimBLEDevice::deleteClient(pClient_BM6);
          Serial.println("2. BM6-connect failed, deleted client");
          return false;
          }
        }
      }

    if(!pClient_BM6->isConnected())         // No connection, try again
      {
      if (!pClient_BM6->connect(advDevice_BM6))
        {
        Serial.println("1. BM6-connect failed");
        if (!pClient_BM6->connect(advDevice_BM6))   // Try again
          {
          Serial.println("2. BM6-connect failed");
          return false;
          }
        }
      }

    Serial.print("Connected to BM6: ");
    Serial.println(pClient_BM6->getPeerAddress().toString().c_str());
    Serial.print("BM6 RSSI: ");
    Serial.println(pClient_BM6->getRssi());

    /** Now we can read/write/subscribe the charateristics of the services we are interested in */
    NimBLERemoteService* pSvc_BM6 = nullptr;
    NimBLERemoteCharacteristic* pChrW_BM6 = nullptr;
    NimBLERemoteCharacteristic* pChrR_BM6 = nullptr;

    pSvc_BM6 = pClient_BM6->getService("0000FFF0-0000-1000-8000-00805f9b34fb");   // Battery Guard
    if(pSvc_BM6)      // make sure it's not null
        {
        pChrR_BM6 = pSvc_BM6->getCharacteristic("0000FFF4-0000-1000-8000-00805f9b34fb"); // Notify (Subscribe) signup
        if(pChrR_BM6)      // make sure it's not null 
            {
            if(pChrR_BM6->canNotify())
              {
              if(!pChrR_BM6->subscribe(true, notifyCB_BM6, true))
                {
                pClient_BM6->disconnect(); // Disconnect if subscribe failed 
                return false;
                }
              }
            else
              {if(pChrR_BM6->canIndicate()) 
                { // Send false as first argument to subscribe to indications instead of notifications
                if(!pChrR_BM6->subscribe(false, notifyCB_BM6))
                  {
                  pClient_BM6->disconnect(); // Disconnect if subscribe failed
                  return false;
                  }
                }
            }
        pChrW_BM6 = pSvc_BM6->getCharacteristic("0000FFF3-0000-1000-8000-00805f9b34fb"); // Write Command only
        if(pChrW_BM6) 
          {     // make sure it's not null 
          if(pChrW_BM6->canWrite())     //canWriteNoResponse()
            {
            if (pChrW_BM6->writeValue(BM6_CMD,BM6_CMD_Length))
              {     
              Serial.printf("Wrote new value to: %s ->", pChrW_BM6->getUUID().toString().c_str());
              for (int d=0;d<BM6_CMD_Length;d++)
                {Serial.printf("%x ",BM6_CMD[d]);}
              Serial.println(" ");
              }
            }
          }
        }

      }
    else 
      {Serial.println("Service not found.");}

    Serial.println("Done with this device!");
    return true;
  }

void setup()
{
    Serial.begin(115200);
    NimBLEDevice::init("");
    //NimBLEDevice::whiteListAdd(a);
    pBLEScan = NimBLEDevice::getScan(); // Create the scan object.
    pBLEScan->setScanCallbacks(&scanCallbacks, false); // Set the callback for when devices are discovered, no duplicates.
    pBLEScan->setActiveScan(true);          // Set active scanning, this will get more data from the advertiser.
    pBLEScan->setFilterPolicy(0); // 1=BLE_HCI_SCAN_FILT_USE_WL
    //pBLEScan->setMaxResults(0); // 0=do not store the scan results => no connectio, use callback only.
    
    esp_aes_init(&aes_ctx);
    esp_aes_setkey(&aes_ctx, aesKey, 128);

}

void loop()
{
  Serial.println("Scanning for BLE Devices ...");
  pBLEScan->start(10000, nullptr, false);       // 0 == scan until infinity 

  while(pBLEScan->isScanning() == true)     // Scanned x Sec = Blocking
    {if(doConnect_BM6 == true)          // Device found-> stop scan
	    {pBLEScan->stop();
      Serial.println("BM6 found");}
    }
  Serial.println("Scan finished");
  pBLEScan->stop();
  delay(100);              // wait 100ms, so scan can finish and clean up

  if(doConnect_BM6) // MAC+Service found
    {doConnect_BM6 = false;                  // Reset for next loop
    Serial.println("Connect to BM6 and enable notifications");
    if(connectToServer_BM6()) 
      {
      Serial.println("Success! we should now be getting BM6 notifications!");
      }
    }

  while(pClient_BM6->isConnected())     // wait until disconnected
    {delay(10);}

  Serial.println("Wait 20 sec befor connecting, again"); 
  delay(20000);
}
