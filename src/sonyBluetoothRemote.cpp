#include "sonyBluetoothRemote.h"
#include <Arduino.h>

namespace
{
uint8_t PRESS_TO_FOCUS[]   = {0x01, 0x07};
uint8_t TAKE_PICTURE[]     = {0x01, 0x09};
uint8_t SHUTTER_RELEASED[] = {0x01, 0x06};
uint8_t HOLD_FOCUS[]       = {0x01, 0x08};
} // namespace

// ================================================
// BLE Callbacks
// ================================================

// The BLEAdvertisedDeviceCallbacks class is used during the initial scanning
// to find the camera to connect to.
void SonyBluetoothRemote::onResult(BLEAdvertisedDevice advertisedDevice)
{
    if (advertisedDevice.getName().empty())
        return;

    Serial.print("BLE: something found: ");
    Serial.println(advertisedDevice.getName().c_str());

    // Check if the name of the advertiser matches
    if (advertisedDevice.getName() == _targetCameraName)
    {
        // Scan can be stopped, we found what we are looking for
        advertisedDevice.getScan()->stop();

        // Address of advertiser is the one we need
        _pCameraAddress.reset(new BLEAddress(advertisedDevice.getAddress()));

        Serial.println("Camera found. Connecting!");

        auto data = advertisedDevice.getPayload();
        for (size_t i = 1; i < advertisedDevice.getPayloadLength(); i++)
        {
            if (data[i - 1] == 0x22)
            {
                if ((data[i] & 0x40) == 0x40 && (data[i] & 0x02) == 0x02)
                {
                    Serial.println("Camera is ready to pair");
                    _doPairing = true;
                }
                else
                {
                    _doConnect = true;
                    Serial.println("Camera is not ready to pair, but "
                                    "trying to connect");
                }
            }
        }
    }
}

// BLEClientCallbacks
void SonyBluetoothRemote::onConnect(BLEClient* pclient) 
{ 
    Serial.println("Connected"); 
}


void SonyBluetoothRemote::onDisconnect(BLEClient* pclient)
{
    onConnectionStateChange(false);
    Serial.println("Disconnected");
}

// BLESecurityCallbacks
// Accept any pair request from Camera
uint32_t SonyBluetoothRemote::onPassKeyRequest()
{
    Serial.println("PassKeyRequest");
    return 123456;
}

void SonyBluetoothRemote::onPassKeyNotify(uint32_t pass_key)
{
    Serial.print("The passkey Notify number:");
    Serial.println(pass_key);
}

bool SonyBluetoothRemote::onSecurityRequest()
{
    Serial.println("SecurityRequest");
    return true;
}


void SonyBluetoothRemote::setConnectedStateChangeCallback(std::function<void(bool)> callback)
{
    _connectedStateChangeCallback = callback;
}


void SonyBluetoothRemote::onAuthenticationComplete(esp_ble_auth_cmpl_t cmpl)
{
    Serial.println("Authentication Complete");
    if (cmpl.success)
        Serial.println("Pairing success");
    else
        Serial.println("Pairing failed");
}

void SonyBluetoothRemote::init(std::string thisDeviceName)
{
    BLEDevice::init(thisDeviceName.c_str());
    BLEDevice::setEncryptionLevel(ESP_BLE_SEC_ENCRYPT);
    BLEDevice::setSecurityCallbacks(this);

    onConnectionStateChange(false);
}

bool SonyBluetoothRemote::connectToServer()
{
    if (!_pClient)
    {
        _pClient = BLEDevice::createClient();
        _pClient->setClientCallbacks(this);
    }

    // Connect to the remove BLE Server.
    if (_pClient->connect(*_pCameraAddress))
    {
        Serial.println(" - Connected to server");
        _doPairing = false;
        _doConnect = false;

        BLERemoteService* pRemoteService =
            _pClient->getService("8000FF00-FF00-FFFF-FFFF-FFFFFFFFFFFF");
        if (!pRemoteService)
        {
            Serial.print("Failed to find our service UUID");
            return false;
        }

        _remoteCommand = pRemoteService->getCharacteristic(BLEUUID((uint16_t)0xFF01));
        _remoteNotify  = pRemoteService->getCharacteristic(BLEUUID((uint16_t)0xFF02));

        if (!_remoteCommand)
        {
            Serial.println("Failed to find our characteristic command");
            return false;
        }

        if (!_remoteNotify)
        {
            Serial.println("Failed to find our characteristic notify");
            return false;
        }

        Serial.println("Camera BLE service and characteristic found");

        onConnectionStateChange(true);

        return true;
    }
    else
    {
        Serial.println(" - fail to BLE connect");
        return false;
    }
}

void SonyBluetoothRemote::pairOrConnect()
{
    BLEScan* pBLEScan = BLEDevice::getScan();
    pBLEScan->setAdvertisedDeviceCallbacks(this);
    pBLEScan->setActiveScan(true);
    Serial.println("BLE: Looking for camera");
    pBLEScan->start(5);
    Serial.println("BLE: end of searching");
}

void SonyBluetoothRemote::trigger()
{
    if (!_connected)
        return;

    _remoteCommand->writeValue(PRESS_TO_FOCUS, 2, true);
    _remoteCommand->writeValue(TAKE_PICTURE, 2, true);
    delay(100);
    _remoteCommand->writeValue(SHUTTER_RELEASED, 2, true);
    delay(100);
    _remoteCommand->writeValue(HOLD_FOCUS, 2, true);

    Serial.println("BLE: took photo");
}

void SonyBluetoothRemote::update()
{
    if (!_connected)
        pairOrConnect();

    if (_doPairing || _doConnect)
        connectToServer();
}

void SonyBluetoothRemote::onConnectionStateChange(bool newConnected)
{
    _connected = newConnected;
    if (_connectedStateChangeCallback)
        _connectedStateChangeCallback(_connected);
}