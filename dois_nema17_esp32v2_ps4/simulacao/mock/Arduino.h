// Mock minimo do Arduino/ESP32 para rodar o sketch no PC com relogio virtual.
#pragma once
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <string>
#include <algorithm>
#include <functional>

#define HIGH 1
#define LOW 0
#define OUTPUT 1
#define INPUT 0

// Relogio virtual (us). O loop do teste avanca isso.
extern uint64_t g_us;
inline unsigned long micros() { return (unsigned long)(uint32_t)g_us; }
inline unsigned long millis() { return (unsigned long)(uint32_t)(g_us / 1000); }

// Pinos: guarda nivel e chama o hook para registrar bordas.
extern int g_pin[64];
extern std::function<void(int, int)> g_onWrite;
inline void pinMode(int, int) {}
inline void digitalWrite(int p, int v) { if (g_pin[p] != v) { g_pin[p] = v; if (g_onWrite) g_onWrite(p, v); } }

inline long map(long x, long in_min, long in_max, long out_min, long out_max) {
  return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}
template <class T, class L, class H> T constrain(T x, L lo, H hi) { return x < (T)lo ? (T)lo : (x > (T)hi ? (T)hi : x); }
using std::min; using std::max;

struct SerialMock {
  bool quiet = false;
  void begin(int) {}
  template <class... A> void printf(const char* f, A... a) { if (!quiet) { std::printf("    [serial] "); std::printf(f, a...); } }
  void println(const char* s) { if (!quiet) std::printf("    [serial] %s\n", s); }
  size_t write(const uint8_t* b, size_t n) { if (!quiet) { std::printf("    [serial] "); std::fwrite(b, 1, n, stdout); } return n; }
};
extern SerialMock Serial;

class String : public std::string {
 public:
  String(const char* s = "") : std::string(s) {}
};
