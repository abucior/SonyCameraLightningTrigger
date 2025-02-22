#pragma once
// Sony Bluetooth Remote for ESP32
//
// This class allows you to trigger a Sony camera via Bluetooth.
//
// This code is essentially a heavily refactored version of the code found here:
//  https://github.com/dzwiedziu-nkg/esp32-a7iv-rc
// which was derived from:
//  https://github.com/coral/freemote
//
// I'm only using a tiny subset of the functionality of the freemote code here.

#include <BLEDevice.h>
#include <String>
#include <functional>
#include <memory>

class SonyBluetoothRemote : public BLEAdvertisedDeviceCallbacks,
                            public BLEClientCallbacks,
                            public BLESecurityCallbacks
{
  public:
    void init(std::string thisDeviceName);
    void pairWith(std::string targetCameraName) { targetCameraName = targetCameraName; }
    void trigger();
    void update();
    void setConnectedStateChangeCallback(std::function<void(bool)> callback);

  public:
    // BLEAdvertisedDeviceCallbacks
    void onResult(BLEAdvertisedDevice advertisedDevice) override;

    // BLEClientCallbacks
    void onConnect(BLEClient* pclient) override;
    void onDisconnect(BLEClient* pclient) override;

    // BLESecurityCallbacks
    uint32_t onPassKeyRequest() override;
    void     onPassKeyNotify(uint32_t pass_key) override;
    bool     onSecurityRequest() override;
    void     onAuthenticationComplete(esp_ble_auth_cmpl_t cmpl) override;
    bool     onConfirmPIN(uint32_t pin) override { return true; }

  private:
    void onConnectionStateChange(bool newConnectionState);

    void pairOrConnect();
    bool connectToServer();

    std::function<void(bool)> _connectedStateChangeCallback;

    std::string _targetCameraName = "ILCE-7CM2";
    bool        _connected        = false;   // True if we're in a usable state
    bool        _doPairing        = false;   // Pairing is needed/ongoing
    bool        _doConnect        = false;   // When pairing is complete, connection is needed
    BLEClient*  _pClient          = nullptr; // This is us. We're the client.

    std::unique_ptr<BLEAddress> _pCameraAddress; // The Bluetooth address of the camera

    BLERemoteCharacteristic* _remoteCommand = nullptr;
    BLERemoteCharacteristic* _remoteNotify  = nullptr;
};
