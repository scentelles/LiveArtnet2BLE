#include <WiFiManager.h>          //https://github.com/kentaylor/WiFiManager

#include "BLEDevice.h"
//#include "BLEScan.h"

#include <Artnet.h>

Artnet artnet;
int frameNb = 0;
#define FIXTURE_DMX_ADDRESS_CHRIS 124

int newcolorR = 0;
int newcolorG = 0;
int newcolorB = 0;

int receivedRedColor   = 0;
int receivedGreenColor = 0;
int receivedBlueColor  = 0;

uint8_t value[7] = {0x56, 0x00, 0x00, 0x00, 0x00, 0xf0, 0xaa};

int lastDebounceTime = 0;

bool ledValue = LOW;
bool lastValueToBeSent = false;


// The remote service we wish to connect to.
static BLEUUID serviceUUID("0000FFE5-0000-1000-8000-00805F9B34FB");
// The characteristic of the remote service we are interested in.
static BLEUUID    charUUID("0000FFE9-0000-1000-8000-00805F9B34FB");

BLEAddress* Server_BLE_Address;

static boolean doConnect = false;
static boolean connected = false;
static boolean doScan = false;
static BLERemoteCharacteristic* pRemoteCharacteristic;
static BLEAdvertisedDevice* myDevice;


class MyClientCallback : public BLEClientCallbacks {
    void onConnect(BLEClient* pclient) {
    }

    void onDisconnect(BLEClient* pclient) {
      connected = false;
      Serial.println("onDisconnect");
    }
};


/**
   Scan for BLE servers and find the first one that advertises the service we are looking for.
*/
class MyAdvertisedDeviceCallbacks: public BLEAdvertisedDeviceCallbacks {
    /**
        Called for each advertising BLE server.
    */
    void onResult(BLEAdvertisedDevice advertisedDevice) {
      Serial.print("BLE Advertised Device found: ");
      Serial.println(advertisedDevice.toString().c_str());

      // We have found a device, let us now see if it contains the service we are looking for.
      if (advertisedDevice.getAddress().toString().compare("ff:ff:10:0f:51:dc") == 0) { // && advertisedDevice.isAdvertisingService(serviceUUID)) {

        BLEDevice::getScan()->stop();
        myDevice = new BLEAdvertisedDevice(advertisedDevice);
        doConnect = true;
        doScan = true;
        Serial.print("found BLE LED device");
        Serial.println(advertisedDevice.toString().c_str());

      } // Found our server
    } // onResult
}; // MyAdvertisedDeviceCallbacks



bool connectToServer() {


  BLEScan* pBLEScan = BLEDevice::getScan();
  pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
  pBLEScan->setActiveScan(true);
  BLEScanResults scanResults = pBLEScan->start(30);
  ESP_LOGD(LOG_TAG, "We found %d devices", scanResults.getCount());

  Server_BLE_Address = new BLEAddress("ff:ff:10:0f:51:dc");

  Serial.print("Forming a connection to ");
  BLEClient*  pClient  = BLEDevice::createClient();
  Serial.println(" - Created client");

  pClient->setClientCallbacks(new MyClientCallback());

  // Connect to the remove BLE Server.
  pClient->connect(*Server_BLE_Address);
  Serial.println("client handle : " + String((int)pClient));
  // pClient->connect(myDevice);  // if you pass BLEAdvertisedDevice instead of address, it will be recognized type of peer device address (public or private)
  Serial.println(" - Connected to server");

  // Obtain a reference to the service we are after in the remote BLE server.
  BLERemoteService* pRemoteService = pClient->getService(serviceUUID);
  if (pRemoteService == nullptr) {
    Serial.print("Failed to find our service UUID: ");
    Serial.println(serviceUUID.toString().c_str());
    pClient->disconnect();
    return false;
  }
  Serial.println(" - Found our service");


  // Obtain a reference to the characteristic in the service of the remote BLE server.
  pRemoteCharacteristic = pRemoteService->getCharacteristic(charUUID);
  if (pRemoteCharacteristic == nullptr) {
    Serial.print("Failed to find our characteristic UUID: ");
    Serial.println(charUUID.toString().c_str());
    pClient->disconnect();
    return false;
  }
  Serial.println(" - Found our characteristic");


  connected = true;
}

void setup() {
  Serial.begin(115200);
  Serial.println("Starting Arduino BLE Client application...");


  WiFiManager wifiManager;
  //wifiManager.resetSettings();
  wifiManager.autoConnect("AutoConnectAP");
  if (WiFi.status() != WL_CONNECTED)
  {
    Serial.println("failed to connect, finishing setup anyway");
  }
  else
  {
    Serial.print("local ip: ");
    Serial.println(WiFi.localIP());
  }


  BLEDevice::init("");

  pinMode(BUILTIN_LED, OUTPUT);

  //SetupArtnet
  artnet.begin();

}

void toggleLed()
{
  if (ledValue == HIGH)
    ledValue = LOW;
  else
    ledValue = HIGH;

  //digitalWrite(BUILTIN_LED, ledValue);

}



// This is the Arduino main loop function.
void loop() {


  if (connected == false) {
    if (connectToServer()) {
      Serial.println("We are now connected to the BLE Server.");
    } else {
      Serial.println("We have failed to connect to the server; there is nothin more we will do.");
    }
  }

  // If we are connected to a peer BLE Server, update the characteristic each time we are reached
  // with the current time since boot.
  if (connected) {
    if (artnet.read() == ART_DMX)
    {
      frameNb++;
      //TODO : get Address from config
      uint8_t* dmxFrames = artnet.getDmxFrame();
      //Serial.println("received artnet frame");
      receivedRedColor     = dmxFrames[FIXTURE_DMX_ADDRESS_CHRIS - 1];
      receivedGreenColor   = dmxFrames[FIXTURE_DMX_ADDRESS_CHRIS - 1 + 1];
      receivedBlueColor    = dmxFrames[FIXTURE_DMX_ADDRESS_CHRIS - 1 + 2];

      //  Serial.println(String(receivedRedColor) + ":" + String(receivedGreenColor) + ":" + String(receivedRedColor));
    }

    if ((newcolorR != receivedRedColor) or (newcolorG != receivedGreenColor) or (newcolorB != receivedBlueColor)    ) {

      newcolorR = receivedRedColor;
      newcolorG = receivedGreenColor;
      newcolorB = receivedBlueColor;

      value[1] = (uint8_t)newcolorR;
      value[2] = (uint8_t)newcolorG;
      value[3] = (uint8_t)newcolorB;

      //Avoid flooding the BT linkwhen artnet sends to many changes
      if ((millis() - lastDebounceTime) > 100) {
        pRemoteCharacteristic->writeValue(value, 7, 0);
        lastDebounceTime = millis();
        lastValueToBeSent = true;
      }
    }
  } else if (doScan) {
    //  BLEDevice::getScan()->start(0);  // this is just eample to start scan after disconnect, most likely there is better way to do it in arduino
  }

  //As we debounce to avoid flooding, last change received from artnet is often skipped and has to be sent after artnet no more send changes.
  if (((millis() - lastDebounceTime) > 200) and (lastValueToBeSent == true)) {
    pRemoteCharacteristic->writeValue(value, 7, 0);
    lastValueToBeSent = false;
    Serial.println("Sending last value after wait");
  }

  delay(10); 
} 
