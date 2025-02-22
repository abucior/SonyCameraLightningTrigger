// Lightning camera trigger for Sony cameras, via Bluetooth, for a CYD 2432S024.
//
// Tested with resistive touch. Capacitive touch is not tested but should work
// with some calibration and appropriate libraries.
//
// Automatic config of touch hw depending from board environement choice in
// platform.ini
//
// Portions of this file, most importantly touch screen support, are derived
// from Mike Eitel's ESP32_CYD_MQTT project, which served as the gateway to
// understanding how to get this device to work.
// https://github.com/MikeEitel/ESP-32_CYD_MQTT
//

#ifdef ESP32_2432S024N
#define LCDtypeN
#elif ESP32_2432S024C
#define LCDtypeC
#elif ESP32_2432S024R
#define LCDtypeR
#endif

#define TEST_UI_ONLY // Define to test UI only without camera connected

// Needed standard libraries
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <Arduino.h>
#include <SPI.h>
#include <WiFi.h>
#include <cstdint>
#include <driver/adc.h>
#include <gfxfont.h>
#include <vector>

#include "sonyBluetoothRemote.h"

#if defined(LCDtypeC)
#include <bb_captouch.h>
#elif defined(LCDtypeR)
#include "XPT2046_Touchscreen.h" // Adapted Adafruit with permanant change to HSPI
#endif

// Convert 888 24-bit RGB to 565 16-bit color
constexpr uint16_t rgb565(int r, int g, int b)
{
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}

#define COL_BLACK rgb565(0, 0, 0)
#define COL_NAVY rgb565(0, 0, 128)
#define COL_DARKGREEN rgb565(0, 128, 0)
#define COL_DARKCYAN rgb565(0, 128, 128)
#define COL_MAROON rgb565(128, 0, 0)
#define COL_PURPLE rgb565(128, 0, 128)
#define COL_OLIVE rgb565(128, 128, 0)
#define COL_LIGHTGREY rgb565(192, 192, 192)
#define COL_DARKGREY rgb565(128, 128, 128)
#define COL_BLUE rgb565(0, 0, 255)
#define COL_GREEN rgb565(0, 255, 0)
#define COL_CYAN rgb565(0, 255, 255)
#define COL_RED rgb565(255, 0, 0)
#define COL_MAGENTA rgb565(255, 0, 255)
#define COL_YELLOW rgb565(255, 255, 0)
#define COL_WHITE rgb565(255, 255, 255)
#define COL_ORANGE rgb565(255, 165, 0)
#define COL_GREENYELLOW rgb565(173, 255, 41)
#define COL_PINK rgb565(255, 130, 198)

// CYD uses HSPI and even same pins for screen and XPT2046 chip !!!
#define HSPI_MISO 12 // GPIO pin for HSPI MISO
#define HSPI_MOSI 13 // GPIO pin for HSPI MOSI
#define HSPI_SCK 14  // GPIO pin for HSPI clock
#define HSPI_SS 15   // GPIO pin for HSPI SS (slave select)

// Define used pins of the LCD
#define CYD_RST -1 // org -1 but if needed probably unused pin 23 used
#define CYD_DC 2
#define CYD_MISO HSPI_MISO // 12
#define CYD_MOSI HSPI_MOSI // 13
#define CYD_SCLK HSPI_SCK  // 14
#define CYD_CS 15          // Chip select for screen
#define CYD_BL 27          // The display backlight
#define CYD_LDR 34         // The ldr light sensor.

#define RES_X 240
#define RES_Y 320

#if defined(LCDtypeC) // These are for the capacitive touch version
#define CST820_SDA 33
#define CST820_SCL 32
#define CST820_RST -1 // 25
#define CST820_IRQ -1 // 21
// Define touch areas on screen
const int XTmin = 1;     // measured values from touch upper left corner
const int XTmax = RES_Y; // measured values from touch lower right corner
const int YTmin = 1;     // measured values from touch upper left corner
const int YTmax = RES_X; // measured values from touch lower right corner

#elif defined(LCDtypeR)        // These are for the capacitive touch version
// Problem with standard libs as only one SPI is used -> special CYD_xxx lib needed
#define XPT2046_IRQ 36         // Unused interupt pin
#define XPT2046_MOSI HSPI_MOSI // 13 // diffrent from CYD source = 32
#define XPT2046_MISO HSPI_MISO // 12 // diffrent from CYD source = 39
#define XPT2046_SCLK HSPI_SCK  // 14 // diffrent from CYD source = 25
#define XPT2046_CS 33          // Chip select for touch
// Define touch areas on screen
const int XTmin = 57;   // measured values from touch upper left corner
const int XTmax = 3788; // measured values from touch lower right corner
const int YTmin = 3911; // measured values from touch upper left corner
const int YTmax = 299;  // measured values from touch lower right corner
#endif

// Onboard led
#define CYD_LED_RED 4    // The all in one led defining the lower left corner
#define CYD_LED_GREEN 16 // All in one led
#define CYD_LED_BLUE 17  // All in one led
#define LED_ON LOW
#define LED_OFF HIGH

byte backlightTarget  = 32; // Control of pwm dimmed backlight
byte backlightCurrent = 1;  // Helper to control dimmed backlight

// Touch-related variables
int           touchX        = 0;     // Last calculated X position on touch
int           touchY        = 0;     // Last calculated Y position on touch
unsigned long lastTouchTime = 0;     // Last time touch was detected
bool          touchReleased = false; // True if touch was released in the last frame
bool          touchHeld     = false; // True if touch is currently held

Adafruit_ST7789 cyd = Adafruit_ST7789(
    CYD_CS, CYD_DC, CYD_RST); // When resistive touch below software spi is not usable !

// ================================================
// Bluetooth remote
SonyBluetoothRemote sonyBluetoothRemote;

// ================================================
// Application data

int           lightCurrentReading    = 20;
float         triggerSensitivity     = 50.0f;
bool          triggerEnabled         = false;
bool          triggerManual          = false;
int           triggerMinimumInterval = 1000; // Trigger at most once per second
unsigned long triggerLastFired       = 0;

// ================================================
// User interface

struct Button
{
    Button(int x, int y, int w, int h, uint16_t fill, const char* label, int fontSize,
           void (*action)(Button&), bool visible = true)
        : x(x), y(y), w(w), h(h), fill(fill), label(label), fontSize(fontSize), action(action),
          visible(visible)
    {
    }
    int         x, y, w, h;
    uint16_t    fill;
    const char* label;
    int         fontSize;
    void        (*action)(Button&);
    bool        visible = true;
};

void onSensitivityUp(Button& button);
void onSensitivityDown(Button& button);
void onEnableDisable(Button& button);
void onTestTrigger(Button& button);
void onAutoManual(Button& button);
void fireTrigger();

Button button_auto(20, 100, 100, 50, COL_OLIVE, "Auto", 2, onAutoManual);
Button button_up(140, 100, 70, 50, COL_NAVY, "+", 3, onSensitivityUp, false);
Button button_down(140, 170, 70, 50, COL_NAVY, "-", 3, onSensitivityDown, false);
Button button_pause(20, 240, 100, 50, COL_MAROON, "Paused", 2, onEnableDisable);
Button button_fire(140, 240, 70, 50, COL_MAROON, "Fire", 2, onTestTrigger);

std::vector<Button*> buttons = {&button_auto, &button_up, &button_down, &button_pause,
                                &button_fire};

void drawText(const char* text, int x, int y, int fontSize, uint16_t color)
{
    cyd.setTextSize(fontSize);
    cyd.setTextColor(color);
    cyd.setCursor(x, y);
    int16_t  x1, y1;
    uint16_t tw, th;
    cyd.getTextBounds(text, x, y, &x1, &y1, &tw, &th);
    cyd.fillRect(x1, y1, tw, th, COL_BLACK);
    cyd.print(text);
}

void drawCenteredText(const char* text, int x, int y, int w, int h, int fontSize, uint16_t color,
                      bool clear = false)
{
    cyd.setTextSize(fontSize);
    int16_t  x1, y1;
    uint16_t tw, th;
    cyd.getTextBounds(text, 0, 0, &x1, &y1, &tw, &th);
    cyd.setCursor(x + (w - tw) / 2, y + (h - th) / 2);
    cyd.setTextColor(color);
    if (clear)
        cyd.fillRect(x, y, w, h, COL_BLACK);
    cyd.print(text);
}

void clearButton(Button& button)
{
    cyd.fillRect(button.x, button.y, button.w, button.h, COL_BLACK);
}

void drawButton(Button& button)
{
    cyd.fillRect(button.x, button.y, button.w, button.h, button.fill);
    cyd.drawRect(button.x, button.y, button.w, button.h, COL_WHITE);
    drawCenteredText(button.label, button.x, button.y, button.w, button.h, button.fontSize,
                     COL_WHITE);
}

void drawButtons()
{
    for (auto button : buttons)
    {
        if (!button->visible)
            continue;
        drawButton(*button);
    }
}

void drawConnectedState(bool isConnected)
{
    drawCenteredText(isConnected ? "Connected" : "Not connected", 0, 300, 240, 12, 2,
                     isConnected ? COL_GREEN : COL_RED, true);
}

void drawCurrentReading()
{
    static int lastReading            = -1;
    static int lastSensitivityReading = -1;
    if (lightCurrentReading == lastReading && triggerSensitivity == lastSensitivityReading)
        return; // Reduce flicker
    lastReading            = lightCurrentReading;
    lastSensitivityReading = triggerSensitivity;

    int x = 20;
    int y = 10;
    int w = 100;
    int h = 80;

    uint16_t color   = COL_WHITE;
    uint16_t bgColor = COL_BLACK;
    if (lightCurrentReading > triggerSensitivity)
    {
        bgColor = COL_RED;
    }

    cyd.fillRect(x, y, w, h, bgColor);
    drawCenteredText(String(lightCurrentReading).c_str(), x, y, w, h, 5, color);
}

void drawSensitivity()
{
    static int lastReading = -1;
    int        newReading  = int(triggerSensitivity);
    if (newReading == lastReading)
        return; // Reduce flicker
    lastReading = newReading;

    int x = 140;
    int y = 30;
    int w = 75;
    int h = 50;

    cyd.fillRect(x, y, w, h, COL_BLACK);
    drawCenteredText(String(newReading).c_str(), x, y, w, h, 3, COL_GREEN);
}

void drawLabels() { drawCenteredText("Trigger:", 140, 20, 75, 10, 1, COL_LIGHTGREY); }

void ui_processTouch(int x, int y)
{
    // Serial.printf("Touch at %d, %d\n", x, y);
    for (auto button : buttons)
    {
        if (!button->visible)
            continue;
        if (x >= button->x && x <= button->x + button->w && y >= button->y &&
            y <= button->y + button->h)
        {
            button->action(*button);
            break;
        }
    }
}

void ui_updateEffects()
{
    // The brightness of the Fire button go high when the trigger is fired,
    // and then slowly fades back to normal.
    unsigned long now             = millis();
    unsigned long triggerFadeTime = 600;
    if (now - triggerLastFired < triggerFadeTime)
    {
        int strength     = 127 * (triggerFadeTime - (now - triggerLastFired)) / triggerFadeTime;
        button_fire.fill = rgb565(128 + strength, strength, strength);
        drawButton(button_fire);
    }
    else
    {
        button_fire.fill = COL_MAROON;
    }
}

void drawLastTouch() { cyd.fillRect(touchX - 5, touchY - 5, 10, 10, COL_RED); }

// ================================================
// UI event handlers

void onAutoManual(Button& button)
{
    triggerManual       = !triggerManual;
    button.label        = triggerManual ? "Manual" : "Auto";
    button.fill         = triggerManual ? COL_MAROON : COL_OLIVE;
    button_up.visible   = triggerManual;
    button_down.visible = triggerManual;
    if (!triggerManual)
    {
        clearButton(button_up);
        clearButton(button_down);
    }
    drawButtons();
}

void onSensitivityUp(Button& button)
{
    triggerSensitivity += 10;
    triggerSensitivity = constrain(triggerSensitivity, 10, 110);
    drawSensitivity();
}

void onSensitivityDown(Button& button)
{
    triggerSensitivity -= 10;
    triggerSensitivity = constrain(triggerSensitivity, 10, 110);
    drawSensitivity();
}

void onEnableDisable(Button& button)
{
    triggerEnabled = !triggerEnabled;
    button.label   = triggerEnabled ? "Running" : "Paused";
    button.fill    = triggerEnabled ? COL_DARKGREEN : COL_MAROON;
    drawButtons();
}

void onTestTrigger(Button& button) { fireTrigger(); }

// ================================================
// Touch handling

#if defined(LCDtypeC)
BBCapTouch  touch;
TOUCHINFO   ti;
const char* szNames[] = {"Unknown", "FT6x36", "GT911", "CST820"};
#elif defined(LCDtypeR)
XPT2046_Touchscreen touchHW(XPT2046_CS);
#endif

void touchInit()
{
#if defined(LCDtypeC)
    touch.init(CST820_SDA, CST820_SCL, CST820_RST,
               CST820_IRQ); // sda, scl, rst, irq
    int iType = touch.sensorType();
    Serial.printf("Sensor type = %s\n", szNames[iType]);
#elif defined LCDtypeR
    touchHW.begin();
    touchHW.setRotation(3); // Light sensor is at top right
#endif
}

bool getTouch(int& x, int& y)
{
#if defined(LCDtypeR) // Resistive
    if (!touchHW.touched())
        return false;

    TS_Point p = touchHW.getPoint();
    x          = p.x;
    y          = p.y;
    return true;
#elif defined(LCDtypeC) // Capacitive
    if (!touch.getSamples(&ti))
        return false;
    x = ti.y[0];
    y = ti.x[0];
    return true;
#else
    return false;
#endif
}

void updateTouch()
{
    int xRaw = 0;
    int yRaw = 0;
    if (!getTouch(xRaw, yRaw))
    {
        touchReleased = touchHeld;
        touchHeld     = false;
        return;
    }

    touchX = map(xRaw, XTmin, XTmax, 0, RES_X);
    touchY = map(yRaw, YTmin, YTmax, 0, RES_Y);
    touchX = constrain(touchX, 0, RES_X);
    touchY = constrain(touchY, 0, RES_Y);

    lastTouchTime = millis();
    touchHeld     = true;
}

// ================================================
// Update routines

void updateLightReading()
{
    // This could likely be sped up substantially if we use the raw ESP32 functions.
    lightCurrentReading = analogRead(CYD_LDR);
    lightCurrentReading = map(lightCurrentReading, 0, 2000, 100, 0);
    if (lightCurrentReading < 0)
        lightCurrentReading = 0;
}

void updateBacklight()
{
    if (backlightCurrent != backlightTarget)
    {
        // TODO: Can add slow fade in/out here
        backlightCurrent = backlightTarget;
        analogWrite(CYD_BL, backlightCurrent);
    }
}

void fireTrigger()
{
    sonyBluetoothRemote.trigger();

    Serial.print("Trigger fired at ");
    Serial.print(lightCurrentReading);
    Serial.print("  Millis:");
    Serial.println(millis());
    triggerLastFired = millis();
}

bool updateCheckTrigger()
{
    if (triggerEnabled && lightCurrentReading > triggerSensitivity &&
        (millis() - triggerLastFired) > triggerMinimumInterval)
    {
        fireTrigger();
        triggerLastFired = millis();
        return true;
    }
    return false;
}

void updateAutoSensitivity()
{
    // Honestly, we could probably just say triggerSensitivity = lightCurrentReading + 10
    // and call it a day, but this seems more fun.
    static unsigned long lastAutoUpdate = 0;
    unsigned long        now            = millis();
    float                tDiff          = (now - lastAutoUpdate) / 1000.0f;
    if (!triggerManual && tDiff > 0)
    {
        float target = lightCurrentReading + 10.0f;
        if (abs(target - triggerSensitivity) < 1.0f)
            triggerSensitivity = target;
        else
        {
            if (target > triggerSensitivity)
                triggerSensitivity += tDiff * 10.0f;
            else
                triggerSensitivity -= tDiff * 10.0f;
            triggerSensitivity = constrain(triggerSensitivity, 10, 110);
        }
        drawSensitivity();
    }
    lastAutoUpdate = now;
}

// ================================================
// Main setup and loop

void setup()
{
    // Initialize debug output and wifi and preset mqtt
    Serial.begin(115200);
    pinMode(CYD_LED_BLUE, OUTPUT);
    pinMode(CYD_LED_GREEN, OUTPUT);
    pinMode(CYD_LED_RED, OUTPUT);
    digitalWrite(CYD_LED_RED, LED_OFF);
    digitalWrite(CYD_LED_GREEN, LED_OFF);
    digitalWrite(CYD_LED_BLUE, LED_OFF);

    analogSetPinAttenuation(CYD_LDR, ADC_0db); // Needs maximum sensitivity.

    SPI.begin(HSPI_SCK, HSPI_MISO, HSPI_MOSI);

    // Start screen
    pinMode(CYD_BL, OUTPUT);
    analogWrite(CYD_BL, backlightTarget);

    cyd.init(RES_X, RES_Y);
    cyd.invertDisplay(false);
    cyd.setRotation(2); // Light sensor is at top right

    cyd.fillRect(0, 0, RES_X, RES_Y, COL_BLACK); // Clear the screen

    cyd.fillRect(0, 0, RES_X, RES_Y, COL_BLACK); // Clear the screen

    drawButtons();
    drawSensitivity();
    drawLabels();
    drawConnectedState(false);

    touchInit();

#ifndef TEST_UI_ONLY
    Serial.println("Connecting to camera...");
    sonyBluetoothRemote.init("AB Lightning Trigger");
    sonyBluetoothRemote.pairWith("ILCE-7CM2");
    sonyBluetoothRemote.setConnectedStateChangeCallback(drawConnectedState);
#endif
}

void loop()
{
#ifndef TEST_UI_ONLY
    sonyBluetoothRemote.update();
#endif

    // A tight loop to make sure we spend the majority of our time
    // checking the light sensor.
    // TODO: Ideally we'd just set up an interrupt to trigger when the value
    // goes above a certain threshold.
    for (int i = 0; i < 1000; i++)
    {
        updateLightReading();
        if (updateCheckTrigger())
            break;
    }

    drawCurrentReading();

    updateTouch();
    // drawLastTouch(); // For debugging and calibrating touch.

    if (touchReleased)
        ui_processTouch(touchX, touchY);

    ui_updateEffects();

    updateBacklight();

    updateAutoSensitivity();
}
