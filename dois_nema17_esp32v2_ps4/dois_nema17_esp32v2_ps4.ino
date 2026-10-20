/*
 * v2 PS4 = mesmo firmware do old/dois_nema17_esp32v2.ino (micropasso, rampa corrigida,
 * curva TANK, gimbal pan/tilt), mas SEM WiFi/HTML: o controle agora e um CONTROLE DE PS4
 * via Bluetooth (testado pensando no paralelo "BGP 2016"; tambem serve o DualShock 4 original).
 *
 * Usa a biblioteca Bluepad32 (aceita DS4 original e a maioria dos clones, sem precisar
 * gravar MAC no controle com SixaxisPairTool como a lib "PS4Controller" exige).
 *
 * INSTALACAO (Arduino IDE) - ver README.md desta pasta:
 *   1. Arquivo > Preferencias > URLs adicionais de placas, adicione:
 *        https://raw.githubusercontent.com/ricardoquesada/esp32-arduino-lib-builder/master/bluepad32_files/package_esp32_bluepad32_index.json
 *   2. Gerenciador de Placas: instale "esp32_bluepad32".
 *   3. Placa: esp32_bluepad32 > "ESP32 Dev Module".
 *   4. Library Manager: "ESP32Servo" (Kevin Harrington).
 *
 * PAREAR O CONTROLE:
 *   Ligue o carrinho, segure SHARE + PS no controle ate a luz piscar rapido.
 *   Conectou = luz fica AZUL e o controle vibra. Depois disso, so apertar PS para reconectar.
 *
 * COMANDOS:
 *   Analogico ESQUERDO  -> anda (frente/tras/gira tank). Quanto mais empurra, mais rapido.
 *   D-pad               -> anda no limite de velocidade (igual aos botoes do app antigo).
 *   L1 / R1             -> diminui / aumenta o LIMITE de velocidade (10% por toque, = slider antigo).
 *   Analogico DIREITO   -> gimbal (pan/tilt). Move enquanto empurra; soltar = segura a posicao.
 *   R3 (aperta o direito) ou TRIANGULO -> centraliza o gimbal.
 *   X (cruz)            -> STOP (freia na rampa ate o controle soltar tudo).
 *   Soltou tudo / controle desligou / caiu o Bluetooth -> o carrinho para sozinho.
 *
 * LOG SEM FIO (Telnet), alem do Monitor Serial:
 *   Conecte no WiFi "Octabot-Log" (senha 12345678) e rode:  telnet 192.168.4.1
 *   (celular: app "Serial WiFi Terminal" / "JuiceSSH" em modo telnet, porta 23)
 *   No telnet: "s" liga/desliga a linha de status (2x por segundo), "h" ajuda.
 *   O WiFi divide o radio com o Bluetooth do controle. Se o controle ficar lento ou
 *   cair, desligue o Telnet: TELNET_LOG = false.
 *
 * Esta placa (Mainboard Future Makers 2k26) ja resolve por hardware:
 *   - VMOT de cada driver -> 12V, com capacitor (C3/C4)
 *   - SLEEP e RESET -> VCC (drivers sempre ativos)
 *   - M0/M1/M2 livres -> passo cheio (200 passos/volta)
 *   - Motores nos conectores X3 (DRV3) e X1 (DRV1); alimentacao em J1(5V) e J2(12V)
 * Ajuste a corrente (Vref) no potenciometro de cada DRV8825 antes de energizar
 *   (DRV8825: corrente_limite ~= Vref x 2 com Rsense 0.1ohm; ex.: 1A -> Vref ~0.5V).
 */

#include <Bluepad32.h>
#include <WiFi.h>
#include <stdarg.h>
#include <ESP32Servo.h>   // instale pelo Library Manager: "ESP32Servo" (Kevin Harrington)

// ---------- Pinos (conforme esquematico Mainboard Future Makers 2k26) ----------
// Motor ESQUERDO  = driver DRV3, conector de motor X3
const int L_STEP = 25;   // PIN25
const int L_DIR  = 33;   // PIN33
const int L_EN   = 26;   // PIN26  (LOW = driver ligado)
// Motor DIREITO   = driver DRV1, conector de motor X1
const int R_STEP = 27;   // PIN27
const int R_DIR  = 14;   // PIN14
const int R_EN   = 12;   // PIN12  (LOW = driver ligado) -- GPIO12 e strapping, ver nota no README

// ---------- Servos (gimbal pan/tilt) ----------
// Conectores de servo da placa: U3=GPIO15, U1=GPIO2, U4=GPIO4.
const int SERVO_PAN_PIN  = 15;  // U3  -> movimento horizontal
const int SERVO_TILT_PIN = 4;   // U4  -> movimento vertical
const bool PAN_INVERTE  = false; // troque se o eixo girar ao contrario
const bool TILT_INVERTE = false;
const float GIMBAL_GRAUS_POR_S = 120; // velocidade do gimbal com o analogico no fundo
Servo servoPan, servoTilt;
float panAng = 90, tiltAng = 90;      // posicao atual (o gimbal segura onde parar)
int panEscrito = -1, tiltEscrito = -1;

// ---------- Ajuste de sentido ----------
// Se uma roda girar ao contrario, troque true<->false na dela.
const bool L_INVERTE = false;
const bool R_INVERTE = true;

// ---------- Velocidade e rampa (intervalo entre passos em us; MENOR = mais rapido) ----------
const unsigned long INTERVALO_LENTO = 1200; // largada / parada
const unsigned long INTERVALO_MIN   = 150;  // teto de velocidade (menor = mais rapido; se travar/chiar, suba p/ 180-220)
const unsigned long INTERVALO_MAX   = 2500; // piso de velocidade
const unsigned long RAMPA_POR_PASSO = 15;   // acelera mais rapido (maior = chega na velocidade antes)

// ---------- Micropasso do DRV8825 (pinos M0/M1/M2 do modulo) ----------
// DEVE bater com o hardware. Nesta placa M0/M1/M2 vem soltos = passo cheio (1).
//   1 = passo cheio   2 = 1/2 (M0 em VCC)   4 = 1/4 (M1 em VCC)   8 = 1/8 (M0 e M1 em VCC)
const unsigned long MICROPASSO = 1;

// ---------- Controle PS4 ----------
const int ZONA_MORTA      = 90;   // analogicos vao de -512..511; abaixo disso = solto (clones tem drift)
const int LIMITE_INICIAL  = 50;   // % (igual ao slider da v2)
const int LIMITE_PASSO    = 10;   // % por toque em L1/R1
int limitePct = LIMITE_INICIAL;   // teto de velocidade escolhido no L1/R1

unsigned long intervaloCruzeiro = 1325; // recalculado a cada leitura do controle
unsigned long intervaloAtual    = INTERVALO_LENTO;

// ---------- Estado ----------
enum Movimento { PARADO, FRENTE, TRAS, ESQUERDA, DIREITA };
Movimento estado    = PARADO;
Movimento comandado = PARADO;

unsigned long ultimoPasso = 0;
bool nivelPasso = false;
unsigned long microContador = 0; // conta pulsos ate fechar 1 passo cheio (rampa e por passo cheio)

// ---------- Seguranca ----------
// Se o controle parar de mandar dados por este tempo, o carrinho para sozinho.
// O DS4 manda relatorio continuo; se o seu clone so mandar quando algo muda e o carrinho
// "engasgar" com o analogico parado, aumente este valor.
const unsigned long TIMEOUT_MS = 1000;
unsigned long ultimoDado = 0;

ControllerPtr controle = nullptr;   // so aceitamos UM controle
uint16_t botoesAntes = 0;

// ---------- Log (Serial + Telnet) ----------
const bool TELNET_LOG = true;          // false = sem WiFi (so Monitor Serial)
const char* AP_SSID = "Octabot-Log";
const char* AP_PASS = "12345678";      // minimo 8 caracteres
WiFiServer telnetServer(23);
WiFiClient telnet;
bool statusLigado = true;              // linha de status periodica ("s" no telnet alterna)
unsigned long bytesPerdidos = 0;       // log descartado porque o WiFi nao deu conta

// printf para o Serial e para o telnet. Nunca espera o WiFi: se o buffer do telnet
// estiver cheio, a mensagem e descartada (o loop dos passos nao pode travar).
void logMsg(const char* fmt, ...) {
  char buf[200];
  va_list ap; va_start(ap, fmt);
  int n = vsnprintf(buf, sizeof buf, fmt, ap);
  va_end(ap);
  if (n <= 0) return;
  if (n >= (int)sizeof buf) n = sizeof buf - 1;
  Serial.write((const uint8_t*)buf, n);
  if (TELNET_LOG && telnet && telnet.connected()) {
    if (telnet.availableForWrite() >= n) telnet.write((const uint8_t*)buf, n);
    else bytesPerdidos += n;
  }
}

// ---------- Bluetooth ----------
void aoConectar(ControllerPtr ctl) {
  if (controle != nullptr) {          // ja tem um controle: recusa o segundo
    logMsg("Segundo controle recusado.\n");
    ctl->disconnect();
    return;
  }
  controle = ctl;
  ultimoDado = millis();
  botoesAntes = 0;
  ControllerProperties p = ctl->getProperties();
  logMsg("Controle conectado: %s (VID=0x%04x PID=0x%04x)\n",
                ctl->getModelName().c_str(), p.vendor_id, p.product_id);
  ctl->setColorLED(0, 0, 255);
  ctl->playDualRumble(0, 250, 0x80, 0x40);
}

void aoDesconectar(ControllerPtr ctl) {
  if (ctl != controle) return;
  controle = nullptr;
  comandado = PARADO;                 // caiu o Bluetooth -> para (com rampa)
  logMsg("Controle desconectado: parando.\n");
}

// Cor da barra de luz = limite de velocidade (azul = devagar ... vermelho = rapido).
void corLimite() {
  if (!controle) return;
  uint8_t r = map(limitePct, 0, 100, 0, 255);
  controle->setColorLED(r, 0, 255 - r);
}

void moverGimbal(float dPan, float dTilt) {
  panAng  = constrain(panAng  + dPan,  0.0f, 180.0f);
  tiltAng = constrain(tiltAng + dTilt, 0.0f, 180.0f);
  int p = (int)panAng, t = (int)tiltAng;
  if (p != panEscrito)  { panEscrito = p;  servoPan.write(PAN_INVERTE ? 180 - p : p); }
  if (t != tiltEscrito) { tiltEscrito = t; servoTilt.write(TILT_INVERTE ? 180 - t : t); }
}

// Le o controle e define comandado / intervaloCruzeiro / gimbal.
void lerControle() {
  static unsigned long ultimaLeitura = 0;
  unsigned long agoraMs = millis();
  float dt = (agoraMs - ultimaLeitura) / 1000.0f;
  ultimaLeitura = agoraMs;

  if (!controle || !controle->isConnected()) { comandado = PARADO; return; }
  if (!controle->hasData()) {
    if (agoraMs - ultimoDado > TIMEOUT_MS) comandado = PARADO;
    return;
  }
  ultimoDado = agoraMs;

  uint16_t botoes = controle->buttons();
  uint8_t  dpad   = controle->dpad();
  uint16_t novos  = botoes & ~botoesAntes;   // apertou agora (borda)
  botoesAntes = botoes;

  // L1/R1 = limite de velocidade (substitui o slider)
  if (novos & BUTTON_SHOULDER_L) { limitePct = max(0,   limitePct - LIMITE_PASSO); corLimite(); }
  if (novos & BUTTON_SHOULDER_R) { limitePct = min(100, limitePct + LIMITE_PASSO); corLimite(); }

  // Gimbal: analogico direito move por velocidade (soltar = segura). R3/triangulo centraliza.
  if (novos & (BUTTON_THUMB_R | BUTTON_Y)) {
    panAng = 90; tiltAng = 90; moverGimbal(0, 0);
  } else {
    int rx = controle->axisRX(), ry = controle->axisRY();
    float gx = abs(rx) > ZONA_MORTA ? rx / 512.0f : 0;
    float gy = abs(ry) > ZONA_MORTA ? ry / 512.0f : 0;
    if (dt > 0.1f) dt = 0.1f;                      // evita salto apos pausa
    moverGimbal(gx * GIMBAL_GRAUS_POR_S * dt, -gy * GIMBAL_GRAUS_POR_S * dt); // cima = tilt maior
  }

  // X (cruz) = STOP enquanto apertado
  if (botoes & BUTTON_A) { comandado = PARADO; return; }

  // Movimento: D-pad tem prioridade (anda no limite); senao, analogico esquerdo (proporcional).
  int pct = 0;
  Movimento m = PARADO;
  if      (dpad & DPAD_UP)    { m = FRENTE;   pct = 100; }
  else if (dpad & DPAD_DOWN)  { m = TRAS;     pct = 100; }
  else if (dpad & DPAD_LEFT)  { m = ESQUERDA; pct = 100; }
  else if (dpad & DPAD_RIGHT) { m = DIREITA;  pct = 100; }
  else {
    int x = controle->axisX(), y = controle->axisY();   // y negativo = para cima
    int ax = abs(x), ay = abs(y);
    // Eixo dominante decide; com vantagem para a direcao atual (evita ficar trocando na diagonal).
    bool vertical = (estado == ESQUERDA || estado == DIREITA) ? (ay > ax * 1.3f) : (ay * 1.3f >= ax);
    int mag = vertical ? ay : ax;
    if (mag > ZONA_MORTA) {
      if (vertical) m = (y < 0) ? FRENTE : TRAS;
      else          m = (x < 0) ? ESQUERDA : DIREITA;
      pct = map(min(mag, 512), ZONA_MORTA, 512, 1, 100);
    }
  }

  comandado = m;
  if (m != PARADO) {
    int vel = pct * limitePct / 100;   // % final (0..100), igual ao slider antigo
    intervaloCruzeiro = map(vel, 0, 100, INTERVALO_MAX, INTERVALO_MIN);
  }
}

// ---------- Telnet ----------
const char* NOME_ESTADO[] = {"PARADO", "FRENTE", "TRAS", "ESQUERDA", "DIREITA"};

void ajudaTelnet() {
  logMsg("Octabot v2 PS4 - comandos: s = status liga/desliga, h = ajuda\n");
}

// Aceita cliente novo, le comandos, imprime eventos e status. Chamado a cada 5ms.
void cuidarLog() {
  static unsigned long ultimoTelnet = 0;
  if (TELNET_LOG && millis() - ultimoTelnet >= 50) {   // rede a cada 50ms basta
    ultimoTelnet = millis();
    WiFiClient novo = telnetServer.available();
    if (novo) {                                   // novo cliente substitui o antigo
      if (telnet) telnet.stop();
      telnet = novo;
      telnet.setNoDelay(true);
      ajudaTelnet();
    }
    while (telnet && telnet.available()) {
      int c = telnet.read();
      if (c == 's') { statusLigado = !statusLigado; logMsg("status %s\n", statusLigado ? "ligado" : "desligado"); }
      else if (c == 'h') ajudaTelnet();           // demais bytes (negociacao telnet, enter) ignorados
    }
  }

  // Eventos: troca de estado e de limite.
  static Movimento estadoLogado = PARADO;
  static int limiteLogado = LIMITE_INICIAL;
  if (estado != estadoLogado) { logMsg("[%lu ms] estado: %s\n", millis(), NOME_ESTADO[estado]); estadoLogado = estado; }
  if (limitePct != limiteLogado) { logMsg("[%lu ms] limite: %d%%\n", millis(), limitePct); limiteLogado = limitePct; }

  // Status periodico.
  static unsigned long ultimoStatus = 0;
  if (statusLigado && millis() - ultimoStatus >= 500) {
    ultimoStatus = millis();
    unsigned long ps = (estado == PARADO) ? 0 : 1000000UL / (2 * intervaloAtual);
    logMsg("%s cmd=%s %lu passos/s limite=%d%% gimbal=%d/%d LX=%d LY=%d BT=%s perdidos=%lu\n",
           NOME_ESTADO[estado], NOME_ESTADO[comandado], ps, limitePct, (int)panAng, (int)tiltAng,
           controle ? controle->axisX() : 0, controle ? controle->axisY() : 0,
           controle ? "ok" : "--", bytesPerdidos);
  }
}

// ---------- Motores ----------
// Liga/desliga os dois drivers (EN ativo em nivel baixo).
void drivers(bool ligado) {
  digitalWrite(L_EN, ligado ? LOW : HIGH);
  digitalWrite(R_EN, ligado ? LOW : HIGH);
}

// Intervalo de largada/parada: o mais lento entre INTERVALO_LENTO e o cruzeiro.
unsigned long intervaloInicio() { return max(INTERVALO_LENTO, intervaloCruzeiro); }

void aplicarSentido(Movimento mv) {
  bool esq, dir;
  switch (mv) {
    case FRENTE:   esq = true;  dir = true;  break;
    case TRAS:     esq = false; dir = false; break;
    case ESQUERDA: esq = false; dir = true;  break; // tank
    case DIREITA:  esq = true;  dir = false; break; // tank
    default: return;
  }
  digitalWrite(L_DIR, (esq ^ L_INVERTE) ? HIGH : LOW);
  digitalWrite(R_DIR, (dir ^ R_INVERTE) ? HIGH : LOW);
}

void setup() {
  Serial.begin(115200);
  pinMode(L_STEP, OUTPUT); pinMode(L_DIR, OUTPUT);
  pinMode(R_STEP, OUTPUT); pinMode(R_DIR, OUTPUT);
  pinMode(L_EN, OUTPUT);   pinMode(R_EN, OUTPUT);
  drivers(false);          // comeca DESLIGADO (parado nao segura corrente = menos calor)

  ESP32PWM::allocateTimer(0);
  ESP32PWM::allocateTimer(1);
  ESP32PWM::allocateTimer(2);
  ESP32PWM::allocateTimer(3);
  servoPan.setPeriodHertz(50);
  servoTilt.setPeriodHertz(50);
  servoPan.attach(SERVO_PAN_PIN, 500, 2500);
  servoTilt.attach(SERVO_TILT_PIN, 500, 2500);
  moverGimbal(0, 0);

  BP32.setup(&aoConectar, &aoDesconectar);
  BP32.enableVirtualDevice(false);   // touchpad do DS4 nao vira "mouse"
  // Se o controle nao reconectar depois de parear com outro aparelho, descomente 1 vez:
  // BP32.forgetBluetoothKeys();
  if (TELNET_LOG) {
    WiFi.mode(WIFI_AP);
    WiFi.softAP(AP_SSID, AP_PASS);
    telnetServer.begin();
    telnetServer.setNoDelay(true);
    logMsg("Telnet: WiFi \"%s\" -> telnet %s\n", AP_SSID, WiFi.softAPIP().toString().c_str());
  }
  logMsg("Bluepad32 %s - segure SHARE + PS no controle para parear.\n", BP32.firmwareVersion());
}

void loop() {
  // Le o controle a cada 5ms (nao a cada volta do loop: o loop precisa ficar livre pros passos).
  static unsigned long ultimoBT = 0;
  if (millis() - ultimoBT >= 5) {
    ultimoBT = millis();
    BP32.update();
    lerControle();
    cuidarLog();
  }

  // Troca de estado com seguranca: nao inverte em velocidade.
  if (comandado != estado) {
    if (estado == PARADO) {
      drivers(true);            // religa os drivers ao sair do repouso
      estado = comandado;
      aplicarSentido(estado);
      intervaloAtual = intervaloInicio();
      microContador = 0;
    }
    // demais transicoes tratadas pela rampa abaixo (freia antes de trocar)
  }

  if (estado == PARADO) return;

  bool freando = (comandado == PARADO) || (comandado != estado);
  unsigned long alvo = freando ? intervaloInicio() : intervaloCruzeiro;

  unsigned long agora = micros();
  if (agora - ultimoPasso >= intervaloAtual / MICROPASSO) {
    ultimoPasso = agora;
    nivelPasso = !nivelPasso;
    int nivel = nivelPasso ? HIGH : LOW;
    digitalWrite(L_STEP, nivel);
    digitalWrite(R_STEP, nivel);

    if (nivelPasso) { // aplica rampa uma vez por passo CHEIO (a cada MICROPASSO pulsos)
      if (++microContador < MICROPASSO) return;
      microContador = 0;
      if (intervaloAtual > alvo) {
        intervaloAtual -= RAMPA_POR_PASSO;
        if (intervaloAtual < alvo) intervaloAtual = alvo;
      } else if (intervaloAtual < alvo) {
        intervaloAtual += RAMPA_POR_PASSO;
        if (intervaloAtual > alvo) intervaloAtual = alvo;
      }
      if (freando && intervaloAtual >= intervaloInicio()) {
        if (comandado == PARADO) { estado = PARADO; drivers(false); } // desliga ao parar de vez
        else { estado = comandado; aplicarSentido(estado); }
      }
    }
  }
}
