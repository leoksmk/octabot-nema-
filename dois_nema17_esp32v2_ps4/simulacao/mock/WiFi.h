// Mock de WiFi/Telnet: um cliente falso que o teste controla.
#pragma once
#include "Arduino.h"
#include <deque>

#define WIFI_AP 2
struct IPAddressMock { String toString() const { return String("192.168.4.1"); } };
struct WiFiMock {
  bool ap = false;
  void mode(int) {}
  bool softAP(const char*, const char*) { ap = true; return true; }
  IPAddressMock softAPIP() { return {}; }
};
extern WiFiMock WiFi;

// Estado do "outro lado" (o PC rodando telnet).
struct TelnetPeer {
  bool conectado = false, novo = false;
  std::string recebido;            // tudo que o ESP32 mandou
  std::deque<int> digitado;        // o que o usuario digita
  int espacoBuffer = 5744;         // availableForWrite(); 0 = WiFi engasgado
};
extern TelnetPeer g_peer;

class WiFiClient {
 public:
  bool valido = false;
  explicit operator bool() const { return valido; }
  bool connected() const { return valido && g_peer.conectado; }
  void setNoDelay(bool) {}
  void stop() { valido = false; }
  int available() { return valido ? (int)g_peer.digitado.size() : 0; }
  int read() { int c = g_peer.digitado.front(); g_peer.digitado.pop_front(); return c; }
  int availableForWrite() { return g_peer.espacoBuffer; }
  size_t write(const uint8_t* b, size_t n) { g_peer.recebido.append((const char*)b, n); return n; }
};

class WiFiServer {
 public:
  explicit WiFiServer(int) {}
  void begin() {}
  void setNoDelay(bool) {}
  WiFiClient available() { WiFiClient c; if (g_peer.novo) { g_peer.novo = false; c.valido = true; } return c; }
};
