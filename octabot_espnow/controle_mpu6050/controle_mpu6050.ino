/*
 * CONTROLE POR INCLINACAO (ESP32 + MPU6050) -> Carrinho 2x NEMA 17 via ESP-NOW
 *
 * Inclinar para FRENTE  -> carrinho anda para frente
 * Inclinar para TRAS    -> carrinho anda para tras
 * Inclinar para os LADOS -> carrinho so GIRA no proprio eixo (esquerda/direita)
 * Quanto mais inclina, mais rapido (proporcional). Controle nivelado = parado.
 *
 * LIGACAO DO MPU6050 (ESP32 DevKit):
 *   VCC -> 3V3     GND -> GND     SDA -> GPIO21     SCL -> GPIO22
 *   (AD0 solto ou no GND = endereco 0x68)
 *   Monte o modulo com a seta X apontando para a FRENTE do controle e o chip para cima.
 *   Se montar diferente, use INVERTE_FRENTE / INVERTE_GIRO.
 *
 * CALIBRACAO: ao ligar, deixe o controle PARADO na posicao "neutra" (como voce
 * vai segurar) por ~1 s. Essa posicao vira o zero. O LED azul pisca enquanto calibra.
 *
 * Nao precisa de biblioteca extra: o MPU6050 e lido direto pelo Wire.
 */

#include <Wire.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>

// ---------- Pacote (IGUAL ao do carrinho_espnow.ino) ----------
const uint32_t MAGICO = 0x0C7AB07E;  // troque nos DOIS codigos se tiver outro robo por perto
typedef struct __attribute__((packed)) {
  uint32_t magico;
  int8_t   frente;   // -100 (tras) .. +100 (frente)
  int8_t   giro;     // -100 (esquerda) .. +100 (direita)
  uint8_t  seq;
} Pacote;

// Broadcast: nao precisa descobrir o MAC do carrinho. O campo "magico" filtra.
uint8_t DESTINO[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
const int CANAL_WIFI = 1;            // tem que ser o mesmo no carrinho

// ---------- MPU6050 ----------
const uint8_t MPU_ADDR = 0x68;
const int PIN_SDA = 21;
const int PIN_SCL = 22;

// ---------- Ajustes do controle ----------
const float ZONA_MORTA_GRAUS = 8.0;   // abaixo disso = parado (evita tremida da mao)
const float ANGULO_MAX_GRAUS = 35.0;  // inclinacao que da 100% de velocidade
const float FILTRO = 0.2;             // 0..1  menor = mais suave (e mais atrasado)
const bool  INVERTE_FRENTE = false;   // troque se frente/tras sair ao contrario
const bool  INVERTE_GIRO   = false;   // troque se esquerda/direita sair ao contrario
const unsigned long PERIODO_ENVIO_MS = 40;  // 25 pacotes/s

// Botao opcional de "homem-morto": so anda enquanto segura (GPIO -> botao -> GND).
const bool USAR_BOTAO = false;
const int  PIN_BOTAO  = 13;

const int PIN_LED = 2;  // LED azul do DevKit

float pitchZero = 0, rollZero = 0;
float pitchF = 0, rollF = 0;
uint8_t seq = 0;

void mpuEscreve(uint8_t reg, uint8_t val) {
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(reg);
  Wire.write(val);
  Wire.endTransmission();
}

// Le o acelerometro e devolve pitch/roll em graus. Retorna false se o MPU nao responder.
bool lerAngulos(float &pitch, float &roll) {
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(0x3B);  // ACCEL_XOUT_H
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom(MPU_ADDR, (uint8_t)6) != 6) return false;
  uint8_t b[6];
  for (int i = 0; i < 6; i++) b[i] = Wire.read();  // le em ordem (H, L) de cada eixo
  int16_t ax = (int16_t)((b[0] << 8) | b[1]);
  int16_t ay = (int16_t)((b[2] << 8) | b[3]);
  int16_t az = (int16_t)((b[4] << 8) | b[5]);
  // Controle de mao quase parado: a gravidade domina, o acelerometro basta.
  pitch = atan2f(-(float)ax, sqrtf((float)ay * ay + (float)az * az)) * 57.2958f;
  roll  = atan2f((float)ay, (float)az) * 57.2958f;
  return true;
}

// Converte angulo em -100..100 com zona morta e saturacao.
int8_t anguloParaPct(float ang) {
  float a = fabsf(ang);
  if (a < ZONA_MORTA_GRAUS) return 0;
  float pct = (a - ZONA_MORTA_GRAUS) / (ANGULO_MAX_GRAUS - ZONA_MORTA_GRAUS) * 100.0f;
  if (pct > 100) pct = 100;
  if (pct < 1) pct = 1;
  return (int8_t)(ang > 0 ? pct : -pct);
}

void calibrar() {
  float sp = 0, sr = 0;
  int n = 0;
  for (int i = 0; i < 100; i++) {
    float p, r;
    if (lerAngulos(p, r)) { sp += p; sr += r; n++; }
    digitalWrite(PIN_LED, (i / 10) % 2);
    delay(10);
  }
  if (n > 0) { pitchZero = sp / n; rollZero = sr / n; }
  pitchF = rollF = 0;
  digitalWrite(PIN_LED, LOW);
}

void setup() {
  Serial.begin(115200);
  pinMode(PIN_LED, OUTPUT);
  if (USAR_BOTAO) pinMode(PIN_BOTAO, INPUT_PULLUP);

  Wire.begin(PIN_SDA, PIN_SCL);
  Wire.setClock(400000);
  mpuEscreve(0x6B, 0x00);  // PWR_MGMT_1: acorda o MPU
  mpuEscreve(0x1C, 0x00);  // ACCEL_CONFIG: +-2g
  mpuEscreve(0x1A, 0x04);  // CONFIG: filtro passa-baixa interno ~20 Hz
  delay(100);

  float p, r;
  while (!lerAngulos(p, r)) {  // sem MPU: pisca rapido e nao envia nada
    Serial.println("MPU6050 nao encontrado (confira SDA=21, SCL=22, 3V3, GND)");
    digitalWrite(PIN_LED, !digitalRead(PIN_LED));
    delay(100);
  }
  calibrar();

  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  esp_wifi_set_channel(CANAL_WIFI, WIFI_SECOND_CHAN_NONE);
  if (esp_now_init() != ESP_OK) {
    Serial.println("Falha no esp_now_init");
    ESP.restart();
  }
  esp_now_peer_info_t peer = {};
  memcpy(peer.peer_addr, DESTINO, 6);
  peer.channel = CANAL_WIFI;
  peer.encrypt = false;
  esp_now_add_peer(&peer);

  Serial.println("Controle pronto.");
}

void loop() {
  static unsigned long ultimoEnvio = 0;
  if (millis() - ultimoEnvio < PERIODO_ENVIO_MS) return;
  ultimoEnvio = millis();

  Pacote pk;
  pk.magico = MAGICO;
  pk.seq = seq++;
  pk.frente = 0;
  pk.giro = 0;

  float p, r;
  bool ok = lerAngulos(p, r);
  bool liberado = !USAR_BOTAO || digitalRead(PIN_BOTAO) == LOW;

  if (ok) {
    pitchF += FILTRO * ((p - pitchZero) - pitchF);
    rollF  += FILTRO * ((r - rollZero)  - rollF);
    if (liberado) {
      // Com X para frente: bico para baixo = pitch positivo = frente;
      // lado direito para baixo = roll positivo = gira para a direita.
      int8_t f = anguloParaPct(pitchF);
      int8_t g = anguloParaPct(rollF);
      pk.frente = INVERTE_FRENTE ? -f : f;
      pk.giro   = INVERTE_GIRO   ? -g : g;
    }
  }
  // Se o MPU falhar, manda 0/0 (para). Se o controle desligar, o carrinho para pelo timeout.
  esp_now_send(DESTINO, (uint8_t *)&pk, sizeof(pk));
  digitalWrite(PIN_LED, (pk.frente || pk.giro) ? HIGH : LOW);

  static uint8_t cont = 0;
  if (++cont >= 10) {  // ~4x por segundo no Serial Monitor, para ajustar
    cont = 0;
    Serial.printf("pitch=%6.1f roll=%6.1f -> frente=%4d giro=%4d%s\n",
                  pitchF, rollF, pk.frente, pk.giro, ok ? "" : "  [MPU FALHOU]");
  }
}
