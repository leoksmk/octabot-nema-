/*
 * Carrinho 2x NEMA 17 (tracao diferencial) - Mainboard Future Makers 2k26
 * VERSAO ESP-NOW: recebe comandos do controle por inclinacao (controle_mpu6050.ino).
 * ESP32 DevKit v1 (30 pinos) + 2 drivers DRV8825. Mesma pinagem do dois_nema17_esp32.ino.
 *
 *   Controle inclinado p/ frente -> anda para frente
 *   Controle inclinado p/ tras   -> anda para tras
 *   Controle inclinado p/ lados  -> so gira no proprio eixo (estilo tank)
 *   Velocidade proporcional a inclinacao. Rampa de aceleracao/desaceleracao.
 *
 * Sem app/WiFi/gimbal: e o firmware "normal" (sem cinematica de fase) trocado para ESP-NOW.
 * Seguranca: se nao chegar pacote do controle em TIMEOUT_MS, freia e para sozinho.
 */

#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>

// ---------- Pacote (IGUAL ao do controle_mpu6050.ino) ----------
const uint32_t MAGICO = 0x0C7AB07E;  // troque nos DOIS codigos se tiver outro robo por perto
typedef struct __attribute__((packed)) {
  uint32_t magico;
  int8_t   frente;   // -100 (tras) .. +100 (frente)
  int8_t   giro;     // -100 (esquerda) .. +100 (direita)
  uint8_t  seq;
} Pacote;
const int CANAL_WIFI = 1;            // tem que ser o mesmo no controle

// ---------- Pinos (conforme esquematico Mainboard Future Makers 2k26) ----------
// Motor ESQUERDO  = driver DRV3, conector de motor X3
const int L_STEP = 25;
const int L_DIR  = 33;
const int L_EN   = 26;   // LOW = driver ligado
// Motor DIREITO   = driver DRV1, conector de motor X1
const int R_STEP = 27;
const int R_DIR  = 14;
const int R_EN   = 12;   // LOW = driver ligado -- GPIO12 e strapping, ver README principal

// ---------- Ajuste de sentido ----------
const bool L_INVERTE = false;
const bool R_INVERTE = true;

// ---------- Velocidade e rampa (intervalo entre meios-passos em us; MENOR = mais rapido) ----------
const unsigned long INTERVALO_LENTO = 1200; // largada / parada
const unsigned long INTERVALO_MIN   = 150;  // 100% de inclinacao (se travar/chiar, suba p/ 180-220)
const unsigned long INTERVALO_MAX   = 2500; // inclinacao minima fora da zona morta
const unsigned long RAMPA_POR_PASSO = 15;   // maior = acelera mais rapido
const int GIRO_MAX_PCT = 60;                // limita a velocidade do giro (girar rapido derrapa)
const int HISTERESE_EIXO = 15;              // evita ficar trocando frente<->giro na diagonal
// Todos os intervalos acima sao em PASSO CHEIO. Com micropasso, o pulso sai MICROPASSO
// vezes mais rapido, mas a velocidade da roda (e a rampa) continua a mesma.

// ---------- Micropasso do DRV8825 (pinos M0/M1/M2 do modulo) ----------
// DEVE bater com o hardware (ver README, secao Barulho):
//   1 = passo cheio (M0/M1/M2 soltos, como vem na placa) -> ruidoso em velocidade media
//   2 = meio passo (M0 em VCC)   4 = 1/4 (M1 em VCC)   8 = 1/8 (M0 e M1 em VCC)
const unsigned long MICROPASSO = 1;

unsigned long intervaloCruzeiro = INTERVALO_MAX;
unsigned long intervaloAtual    = INTERVALO_LENTO;

// ---------- Estado ----------
enum Movimento { PARADO, FRENTE, TRAS, ESQUERDA, DIREITA };
Movimento estado    = PARADO;
Movimento comandado = PARADO;

unsigned long ultimoPasso = 0;
bool nivelPasso = false;
unsigned long microContador = 0;

// ---------- Recepcao ESP-NOW (callback roda na task do WiFi) ----------
const unsigned long TIMEOUT_MS = 300;
portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
volatile int8_t rxFrente = 0, rxGiro = 0;
volatile unsigned long rxMillis = 0;
volatile bool rxNovo = false;

void tratarPacote(const uint8_t *dados, int len) {
  if (len != sizeof(Pacote)) return;
  Pacote pk;
  memcpy(&pk, dados, sizeof(pk));
  if (pk.magico != MAGICO) return;
  portENTER_CRITICAL(&mux);
  rxFrente = pk.frente;
  rxGiro   = pk.giro;
  rxMillis = millis();
  rxNovo   = true;
  portEXIT_CRITICAL(&mux);
}

// A assinatura do callback mudou no core ESP32 3.x.
#if ESP_ARDUINO_VERSION_MAJOR >= 3
void aoReceber(const esp_now_recv_info_t *info, const uint8_t *dados, int len) { tratarPacote(dados, len); }
#else
void aoReceber(const uint8_t *mac, const uint8_t *dados, int len) { tratarPacote(dados, len); }
#endif

// ---------- Motores ----------
void drivers(bool ligado) {
  digitalWrite(L_EN, ligado ? LOW : HIGH);
  digitalWrite(R_EN, ligado ? LOW : HIGH);
}

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

// Intervalo de largada/parada: o mais lento entre INTERVALO_LENTO e o cruzeiro
// (com pouca inclinacao, largar em INTERVALO_LENTO seria MAIS RAPIDO que andar).
unsigned long intervaloInicio() { return max(INTERVALO_LENTO, intervaloCruzeiro); }

bool ehGiro(Movimento m) { return m == ESQUERDA || m == DIREITA; }

// Converte (frente, giro) do controle em movimento + velocidade.
// So um eixo por vez: o que estiver mais inclinado ganha (com histerese).
void interpretarComando(int f, int g) {
  int af = abs(f), ag = abs(g);
  if (af == 0 && ag == 0) { comandado = PARADO; return; }

  bool usarGiro;
  if (ehGiro(comandado))          usarGiro = !(af > ag + HISTERESE_EIXO);
  else if (comandado != PARADO)   usarGiro = ag > af + HISTERESE_EIXO;
  else                            usarGiro = ag > af;

  int pct;
  if (usarGiro) {
    if (ag == 0) { comandado = PARADO; return; }
    comandado = g > 0 ? DIREITA : ESQUERDA;
    pct = ag * GIRO_MAX_PCT / 100;
  } else {
    if (af == 0) { comandado = PARADO; return; }
    comandado = f > 0 ? FRENTE : TRAS;
    pct = af;
  }
  pct = constrain(pct, 1, 100);
  intervaloCruzeiro = map(pct, 1, 100, INTERVALO_MAX, INTERVALO_MIN);
}

void setup() {
  pinMode(L_STEP, OUTPUT); pinMode(L_DIR, OUTPUT);
  pinMode(R_STEP, OUTPUT); pinMode(R_DIR, OUTPUT);
  pinMode(L_EN, OUTPUT);   pinMode(R_EN, OUTPUT);
  drivers(false);          // comeca DESLIGADO (parado nao segura corrente = menos calor)

  Serial.begin(115200);
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  esp_wifi_set_channel(CANAL_WIFI, WIFI_SECOND_CHAN_NONE);
  if (esp_now_init() != ESP_OK) {
    Serial.println("Falha no esp_now_init");
    ESP.restart();
  }
  esp_now_register_recv_cb(aoReceber);
  Serial.print("Carrinho pronto. MAC: ");
  Serial.println(WiFi.macAddress());
}

void loop() {
  // Pega o ultimo pacote recebido (copia atomica).
  int8_t f, g; unsigned long t; bool novo;
  portENTER_CRITICAL(&mux);
  f = rxFrente; g = rxGiro; t = rxMillis; novo = rxNovo; rxNovo = false;
  portEXIT_CRITICAL(&mux);

  if (novo) interpretarComando(f, g);

  // Dead-man switch: controle desligou / saiu do alcance -> para.
  if (comandado != PARADO && millis() - t > TIMEOUT_MS) comandado = PARADO;

  // Troca de estado com seguranca: nao inverte em velocidade.
  if (comandado != estado && estado == PARADO) {
    drivers(true);
    estado = comandado;
    aplicarSentido(estado);
    intervaloAtual = intervaloInicio();
    microContador = 0;
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

    if (nivelPasso) { // borda de subida = 1 pulso nos dois motores
      if (++microContador < MICROPASSO) return;  // rampa so a cada passo cheio
      microContador = 0;
      if (intervaloAtual > alvo) {
        intervaloAtual -= RAMPA_POR_PASSO;
        if (intervaloAtual < alvo) intervaloAtual = alvo;
      } else if (intervaloAtual < alvo) {
        intervaloAtual += RAMPA_POR_PASSO;
        if (intervaloAtual > alvo) intervaloAtual = alvo;
      }
      // Chegou devagar o bastante: para de vez ou troca de sentido.
      if (freando && intervaloAtual >= intervaloInicio()) {
        if (comandado == PARADO) { estado = PARADO; drivers(false); }
        else { estado = comandado; aplicarSentido(estado); }
      }
    }
  }
}
