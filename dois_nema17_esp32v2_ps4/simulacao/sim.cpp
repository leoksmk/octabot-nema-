// Simulacao do dois_nema17_esp32v2_ps4.ino no PC.
// O sketch entra SEM modificacao; Arduino/ESP32Servo/Bluepad32 sao mocks (pasta mock/).
#include "Arduino.h"
#include <vector>
#include <string>

uint64_t g_us = 0;
int g_updateCostUs = 0;
int g_pin[64] = {0};
std::function<void(int, int)> g_onWrite;
SerialMock Serial;

#include "Bluepad32.h"
Bluepad32Mock BP32;

#ifndef SKETCH
#define SKETCH "../dois_nema17_esp32v2_ps4.ino"
#endif
#include SKETCH   // o .ino real, sem modificacao

// ---------------- instrumentacao ----------------
const int LOOP_US = 4;        // custo estimado de uma volta do loop() no ESP32
const int REPORT_US = 4000;   // DS4 manda relatorio a cada ~4ms via BT

struct Passo { uint64_t t; int dirL, dirR; };
std::vector<Passo> passosL;
std::vector<uint64_t> passosR;
long dessincronizados = 0;
struct TrocaDir { uint64_t t; double meioPeriodoAntes; };
std::vector<TrocaDir> trocasDir;
std::vector<uint64_t> enOn, enOff;

Controller* pad = nullptr;
bool padEnviando = true;          // false = clone que parou de mandar relatorio
uint64_t proxRelatorio = 0;
FILE* csv;
struct Cena { uint64_t t; int n; }; std::vector<Cena> cenas;

int falhas = 0;
void check(bool ok, const char* nome, const std::string& det = "") {
  std::printf("  %s %s %s\n", ok ? "[OK]  " : "[FALHA]", nome, det.c_str());
  if (!ok) falhas++;
}
std::string fmt(const char* f, double a, double b = 0, double c = 0) {
  char buf[200]; std::snprintf(buf, sizeof buf, f, a, b, c); return buf;
}

int lastDirL = -1, lastDirR = -1;
void onWrite(int p, int v) {
  if (p == L_STEP && v == HIGH) {
    if (lastDirL != -1 && (g_pin[L_DIR] != lastDirL || g_pin[R_DIR] != lastDirR)) {
      double meio = passosL.empty() ? 0 : (g_us - passosL.back().t) / 2.0;
      trocasDir.push_back({g_us, meio});
    }
    lastDirL = g_pin[L_DIR]; lastDirR = g_pin[R_DIR];
    passosL.push_back({g_us, g_pin[L_DIR], g_pin[R_DIR]});
  }
  if (p == R_STEP && v == HIGH) {
    passosR.push_back(g_us);
    if (passosL.empty() || passosL.back().t != g_us) dessincronizados++;
  }
  if (p == L_EN) (v == LOW ? enOn : enOff).push_back(g_us);
}

uint64_t ultimaAmostra = 0; size_t passosAmostra = 0;
const char* nomeEstado[] = {"PARADO", "FRENTE", "TRAS", "ESQUERDA", "DIREITA"};

// roda o firmware por 'ms' milissegundos de tempo virtual
void rodar(double ms) {
  uint64_t fim = g_us + (uint64_t)(ms * 1000);
  while (g_us < fim) {
    if (pad && pad->connected && padEnviando && g_us >= proxRelatorio) { pad->newData = true; proxRelatorio = g_us + REPORT_US; }
    loop();
    g_us += LOOP_US;
    if (g_us - ultimaAmostra >= 20000) {   // amostra a cada 20ms
      double sps = (passosL.size() - passosAmostra) * 1e6 / (g_us - ultimaAmostra);
      int sentido = (estado == FRENTE) ? 1 : (estado == TRAS) ? -1 : (estado == PARADO) ? 0 : 2;
      std::fprintf(csv, "%.3f,%.1f,%d,%d,%lu,%d,%.1f,%.1f\n", g_us / 1e6, sps, sentido,
                   g_pin[L_EN] == LOW, intervaloAtual, limitePct, panAng, tiltAng);
      ultimaAmostra = g_us; passosAmostra = passosL.size();
    }
  }
}
// taxa de passos (passos/s) nos ultimos 'ms'
double taxa(double ms) {
  uint64_t ini = g_us - (uint64_t)(ms * 1000); long n = 0;
  for (auto& p : passosL) if (p.t >= ini) n++;
  return n * 1e6 / (ms * 1000);
}
void toque(uint16_t b) { pad->btn |= b; rodar(60); pad->btn &= ~b; rodar(60); }
void solta() { pad->ax = pad->ay = pad->arx = pad->ary = 0; pad->btn = 0; pad->dp = 0; }
double esperado(int pct) { return 1e6 / (2.0 * map(pct, 0, 100, INTERVALO_MAX, INTERVALO_MIN)); }
// tempo ate parar de vez (drivers desligados) a partir de agora
double tempoAteParar(double maxMs) {
  uint64_t t0 = g_us;
  while (g_us - t0 < maxMs * 1000) { rodar(1); if (estado == PARADO && g_pin[L_EN] == HIGH) return (g_us - t0) / 1000.0; }
  return -1;
}

int main(int argc, char** argv) {
  if (argc > 1) g_updateCostUs = atoi(argv[1]);
  std::printf("Custo simulado do BP32.update(): %d us\n", g_updateCostUs);
  csv = std::fopen(argc > 2 ? argv[2] : "sim.csv", "w");
  std::fprintf(csv, "t,passos_s,sentido,drivers,intervalo,limite,pan,tilt\n");
  Serial.quiet = false;
  g_onWrite = onWrite;
  setup();
  Serial.quiet = true;

  cenas.push_back({g_us, 1}); std::printf("\n1) Sem controle conectado, 1s\n");
  rodar(1000);
  check(passosL.empty() && g_pin[L_EN] == HIGH, "nao anda e drivers desligados");

  cenas.push_back({g_us, 2}); std::printf("\n2) Conecta o controle (BGP 2016 simulado)\n");
  pad = new Controller(); Serial.quiet = false; BP32.onC(pad); Serial.quiet = true;
  check(pad->b == 255 && pad->r == 0, "luz azul ao conectar");
  check(pad->rumbles == 1, "vibra ao conectar");

  cenas.push_back({g_us, 3}); std::printf("\n3) Drift de clone: analogico parado em (60,-70), 1s\n");
  pad->ax = 60; pad->ay = -70; rodar(1000);
  check(passosL.empty(), "zona morta segura o drift", fmt("(%g passos)", passosL.size()));
  solta();

  cenas.push_back({g_us, 4}); std::printf("\n4) Analogico esquerdo todo pra frente, limite 50%%, 2s\n");
  pad->ay = -512; rodar(2000);
  check(estado == FRENTE, "estado FRENTE");
  check(std::fabs(taxa(500) - esperado(50)) < esperado(50) * 0.03, "velocidade = slider 50% da v2",
        fmt("(%.0f passos/s, esperado %.0f)", taxa(500), esperado(50)));

  cenas.push_back({g_us, 5}); std::printf("\n5) Meio analogico (ay=-300)\n");
  pad->ay = -300; rodar(1500);
  int pctMeio = map(300, ZONA_MORTA, 512, 1, 100) * 50 / 100;
  check(std::fabs(taxa(500) - esperado(pctMeio)) < esperado(pctMeio) * 0.05, "proporcional ao analogico",
        fmt("(%.0f passos/s, esperado %.0f = %.0f%%)", taxa(500), esperado(pctMeio), pctMeio));

  cenas.push_back({g_us, 6}); std::printf("\n6) Solta o analogico\n");
  solta();
  double tp = tempoAteParar(3000);
  check(tp > 0 && tp < 1000, "freia e desliga drivers", fmt("(parou em %.0f ms)", tp));

  cenas.push_back({g_us, 7}); std::printf("\n7) R1 x6 (limite 50 -> 100, deve travar em 100)\n");
  for (int i = 0; i < 6; i++) toque(BUTTON_SHOULDER_R);
  check(limitePct == 100, "limite = 100%", fmt("(%.0f%%)", limitePct));
  check(pad->r == 255 && pad->b == 0, "luz vermelha no 100%");

  cenas.push_back({g_us, 8}); std::printf("\n8) D-pad pra cima no limite 100%%: aceleracao com rampa\n");
  size_t n0 = passosL.size(); uint64_t t0 = g_us;
  pad->dp = DPAD_UP; rodar(1500);
  double tAteCruzeiro = -1;
  for (size_t i = n0 + 1; i < passosL.size(); i++)
    if ((passosL[i].t - passosL[i - 1].t) / 2.0 <= INTERVALO_MIN + LOOP_US) { tAteCruzeiro = (passosL[i].t - t0) / 1000.0; break; }
  double meioPrimeiro = n0 + 1 < passosL.size() ? (passosL[n0 + 1].t - passosL[n0].t) / 2.0 : 0;
  check(meioPrimeiro >= INTERVALO_LENTO - RAMPA_POR_PASSO - LOOP_US, "larga devagar", fmt("(1o meio periodo %.0f us)", meioPrimeiro));
  check(tAteCruzeiro > 0, "chega no teto de velocidade", fmt("(em %.0f ms)", tAteCruzeiro));
  check(std::fabs(taxa(500) - esperado(100)) < esperado(100) * 0.05, "velocidade maxima",
        fmt("(%.0f passos/s, esperado %.0f)", taxa(500), esperado(100)));

  cenas.push_back({g_us, 9}); std::printf("\n9) Em velocidade maxima, troca para DIREITA (gira tank)\n");
  size_t trocas0 = trocasDir.size();
  pad->dp = DPAD_RIGHT; rodar(1500);
  check(estado == DIREITA, "estado DIREITA");
  bool trocaLenta = trocasDir.size() > trocas0;
  for (size_t i = trocas0; i < trocasDir.size(); i++) if (trocasDir[i].meioPeriodoAntes < INTERVALO_LENTO - LOOP_US) trocaLenta = false;
  check(trocaLenta, "so inverte o DIR depois de frear",
        trocasDir.size() > trocas0 ? fmt("(meio periodo na troca: %.0f us)", trocasDir.back().meioPeriodoAntes) : "(nao trocou)");
  check(g_pin[L_DIR] != g_pin[R_DIR] || (L_INVERTE != R_INVERTE), "rodas em sentidos opostos (tank)");

  cenas.push_back({g_us, 10}); std::printf("\n10) X (STOP) com o D-pad ainda apertado\n");
  pad->btn = BUTTON_A;
  tp = tempoAteParar(3000);
  check(tp > 0, "X para o carrinho", fmt("(parou em %.0f ms)", tp));
  pad->btn = 0; solta();

  cenas.push_back({g_us, 11}); std::printf("\n11) Analogico no fundo e o controle DESLIGA (cai o Bluetooth)\n");
  pad->ay = -512; rodar(800);
  pad->connected = false; Serial.quiet = false; BP32.onD(pad); Serial.quiet = true;
  tp = tempoAteParar(3000);
  check(tp > 0 && tp < 1000, "para sozinho", fmt("(parou em %.0f ms)", tp));

  cenas.push_back({g_us, 12}); std::printf("\n12) Reconecta (aperta PS) e anda de novo\n");
  pad = new Controller(); BP32.onC(pad);
  pad->ay = 512; rodar(800);
  check(estado == TRAS && taxa(300) > 100, "anda de re apos reconectar", fmt("(%.0f passos/s)", taxa(300)));

  cenas.push_back({g_us, 13}); std::printf("\n13) Segundo controle tenta conectar\n");
  Controller* intruso = new Controller(); BP32.onC(intruso);
  check(intruso->refused && controle == pad, "segundo controle recusado");

  cenas.push_back({g_us, 14}); std::printf("\n14) Clone que PARA de mandar dados com o analogico segurado\n");
  padEnviando = false; uint64_t tCorte = g_us;
  tp = tempoAteParar(4000);
  check(tp > 0, "para pelo TIMEOUT", fmt("(parou %.0f ms depois do ultimo dado; TIMEOUT_MS=%g)", (g_us - tCorte) / 1000.0, TIMEOUT_MS));
  padEnviando = true; solta(); rodar(200);

  cenas.push_back({g_us, 15}); std::printf("\n15) Gimbal: analogico direito todo pra direita e pra cima por 0.5s\n");
  double pan0 = panAng, tilt0 = tiltAng;
  pad->arx = 512; pad->ary = -512; rodar(500); pad->arx = pad->ary = 0; rodar(300);
  check(std::fabs((panAng - pan0) - 60) < 4, "pan anda ~120 graus/s", fmt("(%.0f -> %.0f)", pan0, panAng));
  check(tiltAng > tilt0 + 50, "tilt sobe com analogico pra cima", fmt("(%.0f -> %.0f)", tilt0, tiltAng));
  double panSolto = panAng; rodar(1000);
  check(panAng == panSolto, "soltou = segura a posicao");
  pad->arx = 512; rodar(2000); pad->arx = 0; rodar(100);
  check(panAng == 180, "trava no fim de curso (180)", fmt("(%.0f)", panAng));
  toque(BUTTON_THUMB_R);
  check(panAng == 90 && tiltAng == 90 && servoPan.ang == 90, "R3 centraliza");
  pad->arx = 512; rodar(300); pad->arx = 0; toque(BUTTON_Y);
  check(panAng == 90, "Triangulo centraliza");

  cenas.push_back({g_us, 16}); std::printf("\n16) L1 x15 (deve travar em 0%%) e andar no 0%%\n");
  for (int i = 0; i < 15; i++) toque(BUTTON_SHOULDER_L);
  check(limitePct == 0, "limite = 0%");
  pad->dp = DPAD_UP; rodar(1500);
  check(std::fabs(taxa(500) - esperado(0)) < esperado(0) * 0.05, "anda no minimo, nao para",
        fmt("(%.0f passos/s)", taxa(500)));
  solta(); tempoAteParar(3000);
  for (int i = 0; i < 5; i++) toque(BUTTON_SHOULDER_R);

  cenas.push_back({g_us, 17}); std::printf("\n17) Analogico na diagonal (45 graus) com ruido, 3s\n");
  size_t tr0 = trocasDir.size();
  for (int i = 0; i < 150; i++) { pad->ax = 360 + (rand() % 41 - 20); pad->ay = -360 + (rand() % 41 - 20); rodar(20); }
  check(trocasDir.size() - tr0 <= 1, "nao fica alternando frente/gira", fmt("(%g trocas de direcao)", trocasDir.size() - tr0));
  solta(); tempoAteParar(3000);

  std::printf("\nGeral\n");
  check(dessincronizados == 0, "rodas sempre no mesmo passo (pernas em fase)", fmt("(%g passos fora)", dessincronizados));
  check(passosL.size() == passosR.size(), "mesmo numero de passos L e R", fmt("(%g / %g)", passosL.size(), passosR.size()));

  std::fclose(csv);
  { FILE* f = std::fopen("cenas.csv", "w"); for (auto& c : cenas) std::fprintf(f, "%.3f,%d\n", c.t / 1e6, c.n); std::fclose(f); }
  std::printf("\n%s: %d falha(s). Tempo simulado: %.1f s, %zu passos.\n", falhas ? "RESULTADO" : "RESULTADO OK", falhas, g_us / 1e6, passosL.size());
  return falhas ? 1 : 0;
}
