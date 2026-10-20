// Mock da Preferences (NVS): um "flash" que sobrevive ao setup() de novo, com contador de gravacoes.
#pragma once
#include "Arduino.h"
#include <map>
extern std::map<std::string, int> g_flash;
extern int g_gravacoesFlash;
extern uint64_t g_ultimaGravacaoUs;
class Preferences {
 public:
  bool begin(const char*, bool) { return true; }
  int getInt(const char* k, int def) { return g_flash.count(k) ? g_flash[k] : def; }
  size_t putInt(const char* k, int v) { g_flash[k] = v; g_gravacoesFlash++; g_ultimaGravacaoUs = g_us; return 4; }
};
