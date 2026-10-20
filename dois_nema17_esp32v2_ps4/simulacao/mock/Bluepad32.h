// Mock da API Bluepad32 usada pelo sketch (mesmos nomes/constantes da lib real).
#pragma once
#include "Arduino.h"
extern int g_updateCostUs;

enum { DPAD_UP = 1 << 0, DPAD_DOWN = 1 << 1, DPAD_RIGHT = 1 << 2, DPAD_LEFT = 1 << 3 };
enum {
  BUTTON_A = 1 << 0, BUTTON_B = 1 << 1, BUTTON_X = 1 << 2, BUTTON_Y = 1 << 3,
  BUTTON_SHOULDER_L = 1 << 4, BUTTON_SHOULDER_R = 1 << 5, BUTTON_TRIGGER_L = 1 << 6,
  BUTTON_TRIGGER_R = 1 << 7, BUTTON_THUMB_L = 1 << 8, BUTTON_THUMB_R = 1 << 9,
};

struct ControllerProperties { uint16_t vendor_id = 0x054c, product_id = 0x09cc; };

class Controller {
 public:
  // estado "fisico" do controle (o teste mexe aqui)
  int ax = 0, ay = 0, arx = 0, ary = 0; uint16_t btn = 0; uint8_t dp = 0;
  bool connected = true, newData = false, refused = false;
  int r = -1, g = -1, b = -1, rumbles = 0;
  bool isConnected() const { return connected; }
  bool hasData() { bool d = newData; newData = false; return d; }   // real: consome o relatorio
  int axisX() const { return ax; } int axisY() const { return ay; }
  int axisRX() const { return arx; } int axisRY() const { return ary; }
  uint16_t buttons() const { return btn; } uint8_t dpad() const { return dp; }
  void setColorLED(uint8_t R, uint8_t G, uint8_t B) { r = R; g = G; b = B; }
  void playDualRumble(int, int, int, int) { rumbles++; }
  void disconnect() { refused = true; connected = false; }
  String getModelName() const { return String("DualShock 4 (mock BGP 2016)"); }
  ControllerProperties getProperties() const { return {}; }
};
typedef Controller* ControllerPtr;
typedef void (*GamepadCallback)(ControllerPtr);

class Bluepad32Mock {
 public:
  GamepadCallback onC = nullptr, onD = nullptr;
  int updates = 0;
  void setup(GamepadCallback c, GamepadCallback d) { onC = c; onD = d; }
  void enableVirtualDevice(bool) {}
  void forgetBluetoothKeys() {}
  const char* firmwareVersion() { return "mock"; }
  bool update() { updates++; g_us += g_updateCostUs; return true; }
};
extern Bluepad32Mock BP32;
