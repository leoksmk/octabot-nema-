/*
 * OCTA ESP-NOW VR - TRANSMISSOR (controle de cabeca/mao)
 * Placa: ESP32-C3 Super Mini.  Giroscopio: MPU6050 (I2C).  LED vermelho 2 pinos (status).
 *
 * Inclinar o controle move o gimbal:
 *   - inclinar frente/tras (pitch) -> TILT
 *   - inclinar p/ os lados (roll)  -> PAN
 * Envia os angulos por ESP-NOW para o Octabot (receptor).
 *
 * LED de status:
 *   - PISCANDO  = procurando / sem conexao (ESP-NOW nao confirma entrega)
 *   - ACESO FIXO = conectado (o receptor esta recebendo)
 *
 * NAO precisa de biblioteca extra (MPU6050 lido direto por Wire).
 *
 * >>> ANTES DE USAR <<<
 *   1. Ligue o Octabot (receptor) e veja no Serial o MAC que ele imprime.
 *   2. Cole esse MAC em RECEIVER_MAC abaixo.
 *   3. WIFI_CANAL tem que ser o MESMO do SoftAP do Octabot (padrao 1).
 *
 * Ligacao:
 *   MPU6050: VCC->3V3, GND->GND, SDA->GPIO8, SCL->GPIO9
 *   LED vermelho: GPIO10 -> resistor ~220ohm -> LED(+)  ...  LED(-) -> GND
 */

#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <Wire.h>

// ===================== AJUSTES =====================
uint8_t RECEIVER_MAC[] = {0x00,0x00,0x00,0x00,0x00,0x00}; // <-- COLE o MAC do Octabot aqui
#define WIFI_CANAL   1          // igual ao canal do SoftAP do Octabot

#define SDA_PIN      8          // I2C do ESP32-C3 Super Mini
#define SCL_PIN      9
#define STATUS_LED   10         // LED vermelho de status
#define MPU_ADDR     0x68

const bool PAN_INVERTE  = false; // troque se o pan vier ao contrario
const bool TILT_INVERTE = false; // troque se o tilt vier ao contrario
// ===================================================

typedef struct { int pan; int tilt; } Pacote;
Pacote pac;

volatile unsigned long ultimoOk = 0;   // ultimo envio confirmado (ACK do ESP-NOW)

// Callback de envio: se o receptor confirmou, marca o tempo (= conectado).
void onSent(const uint8_t* mac, esp_now_send_status_t status){
  if(status == ESP_NOW_SEND_SUCCESS) ultimoOk = millis();
}

void mpuInit(){
  Wire.begin(SDA_PIN, SCL_PIN);
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(0x6B); Wire.write(0);   // tira o MPU6050 do sleep
  Wire.endTransmission(true);
}

bool mpuLerAccel(float &ax, float &ay, float &az){
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(0x3B);                  // registrador do acelerometro
  if(Wire.endTransmission(false) != 0) return false;
  if(Wire.requestFrom(MPU_ADDR, 6, true) != 6) return false;
  int16_t x = (Wire.read()<<8) | Wire.read();
  int16_t y = (Wire.read()<<8) | Wire.read();
  int16_t z = (Wire.read()<<8) | Wire.read();
  ax = x/16384.0; ay = y/16384.0; az = z/16384.0;  // +-2g
  return true;
}

void setup(){
  pinMode(STATUS_LED, OUTPUT);
  mpuInit();

  WiFi.mode(WIFI_STA);
  esp_wifi_set_channel(WIFI_CANAL, WIFI_SECOND_CHAN_NONE);

  if(esp_now_init() != ESP_OK){ while(true){ // falhou: pisca rapido pra sempre
      digitalWrite(STATUS_LED, millis()%120<60); } }
  esp_now_register_send_cb(onSent);

  esp_now_peer_info_t peer = {};
  memcpy(peer.peer_addr, RECEIVER_MAC, 6);
  peer.channel = WIFI_CANAL;
  peer.encrypt = false;
  esp_now_add_peer(&peer);
}

unsigned long ultimoEnvio=0, ultimoBlink=0;
bool ledEst=false;

void loop(){
  // Le o giroscopio e envia os angulos ~25x/s
  if(millis()-ultimoEnvio >= 40){
    ultimoEnvio = millis();
    float ax,ay,az;
    if(mpuLerAccel(ax,ay,az)){
      float pitch = atan2(-ax, sqrt(ay*ay + az*az)) * 57.2958; // -90..90
      float roll  = atan2(ay, az) * 57.2958;                   // -90..90
      int tilt = constrain((int)(map((long)pitch, -90, 90, 0, 180)), 0, 180);
      int pan  = constrain((int)(map((long)roll , -90, 90, 0, 180)), 0, 180);
      pac.tilt = TILT_INVERTE ? 180-tilt : tilt;
      pac.pan  = PAN_INVERTE  ? 180-pan  : pan;
      esp_now_send(RECEIVER_MAC, (uint8_t*)&pac, sizeof(pac));
    }
  }

  // LED de status: conectado = aceso fixo; senao = piscando
  bool conectado = (millis()-ultimoOk < 500);
  if(conectado){
    digitalWrite(STATUS_LED, HIGH);
  } else {
    if(millis()-ultimoBlink > 200){ ultimoBlink=millis(); ledEst=!ledEst; digitalWrite(STATUS_LED, ledEst); }
  }
}
