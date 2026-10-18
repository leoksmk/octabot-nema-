/*
 * OCTA ESP-NOW VR - TRANSMISSOR (head-tracker)
 * Placa: ESP32-C3 Super Mini.  Giroscopio: MPU6050 (I2C).  LED vermelho 2 pinos (status).
 *
 * MOVER A CABECA MOVE O GIMBAL (1:1):
 *   - inclinar (acenar) -> TILT   : vem do acelerometro (sem drift).
 *   - virar p/ os lados (yaw) -> PAN : vem do giroscopio INTEGRADO.
 *
 * ATENCAO ao yaw (pan): como o MPU6050 nao tem bussola, o pan e integrado e
 * DERIVA devagar. Por isso:
 *   1) Mantenha o controle PARADO ~1s ao ligar (calibra o zero do giroscopio).
 *   2) Use o BOTAO DE RECENTRALIZAR pra zerar o pan quando estiver olhando pra frente.
 *
 * LED de status:
 *   - PISCANDO  = procurando / sem conexao
 *   - ACESO FIXO = conectado (receptor recebendo)
 *
 * NAO precisa de biblioteca extra (MPU6050 lido direto por Wire).
 *
 * >>> ANTES DE USAR: cole em RECEIVER_MAC o MAC que o Octabot imprime no Serial. <<<
 *
 * Ligacao:
 *   MPU6050: VCC->3V3, GND->GND, SDA->GPIO8, SCL->GPIO9
 *   LED vermelho: GPIO10 -> resistor ~220ohm -> LED(+) ; LED(-) -> GND
 *   Botao recentralizar: GPIO3 -> botao -> GND  (usa pull-up interno)
 */

#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <Wire.h>

// ===================== AJUSTES =====================
uint8_t RECEIVER_MAC[] = {0x00,0x00,0x00,0x00,0x00,0x00}; // <-- COLE o MAC do Octabot
#define WIFI_CANAL    1

#define SDA_PIN       8
#define SCL_PIN       9
#define STATUS_LED    10
#define RECENTER_BTN  3          // botao p/ GND (zera o pan)
#define MPU_ADDR      0x68

// Ganho: 1.0 = 1:1 (virar a cabeca 90 graus = gimbal 90 graus).
const float PAN_GANHO  = 1.0;
const float TILT_GANHO = 1.0;
const bool  PAN_INVERTE  = false;
const bool  TILT_INVERTE = false;
// ===================================================

typedef struct { int pan; int tilt; } Pacote;
Pacote pac;

volatile unsigned long ultimoOk = 0;
void onSent(const uint8_t* mac, esp_now_send_status_t status){
  if(status == ESP_NOW_SEND_SUCCESS) ultimoOk = millis();
}

float gyBias=0, gzBias=0;     // offset do giroscopio (calibrado no boot)
float pitch=0, yaw=0;         // angulos estimados (graus)
unsigned long ultimoMicros=0;

void mpuEscreve(uint8_t reg, uint8_t val){
  Wire.beginTransmission(MPU_ADDR); Wire.write(reg); Wire.write(val); Wire.endTransmission(true);
}
// Le accel(g) e gyro(graus/s)
bool mpuLer(float&ax,float&ay,float&az,float&gx,float&gy,float&gz){
  Wire.beginTransmission(MPU_ADDR); Wire.write(0x3B);
  if(Wire.endTransmission(false)!=0) return false;
  if(Wire.requestFrom(MPU_ADDR,14,true)!=14) return false;
  int16_t axr=(Wire.read()<<8)|Wire.read();
  int16_t ayr=(Wire.read()<<8)|Wire.read();
  int16_t azr=(Wire.read()<<8)|Wire.read();
  Wire.read();Wire.read();                 // temperatura (ignora)
  int16_t gxr=(Wire.read()<<8)|Wire.read();
  int16_t gyr=(Wire.read()<<8)|Wire.read();
  int16_t gzr=(Wire.read()<<8)|Wire.read();
  ax=axr/16384.0; ay=ayr/16384.0; az=azr/16384.0;   // +-2g
  gx=gxr/131.0;   gy=gyr/131.0;   gz=gzr/131.0;      // +-250 graus/s
  return true;
}

void calibrarGiro(){            // media com o controle PARADO
  float sy=0,sz=0; int n=0;
  for(int i=0;i<300;i++){
    float ax,ay,az,gx,gy,gz;
    if(mpuLer(ax,ay,az,gx,gy,gz)){ sy+=gy; sz+=gz; n++; }
    delay(3);
  }
  if(n){ gyBias=sy/n; gzBias=sz/n; }
}

void setup(){
  pinMode(STATUS_LED,OUTPUT);
  pinMode(RECENTER_BTN,INPUT_PULLUP);

  Wire.begin(SDA_PIN,SCL_PIN);
  mpuEscreve(0x6B,0);           // acorda o MPU6050
  delay(100);
  calibrarGiro();               // << fique parado ao ligar

  WiFi.mode(WIFI_STA);
  esp_wifi_set_channel(WIFI_CANAL, WIFI_SECOND_CHAN_NONE);
  if(esp_now_init()!=ESP_OK){ while(true){ digitalWrite(STATUS_LED, millis()%120<60);} }
  esp_now_register_send_cb(onSent);
  esp_now_peer_info_t peer={};
  memcpy(peer.peer_addr,RECEIVER_MAC,6);
  peer.channel=WIFI_CANAL; peer.encrypt=false;
  esp_now_add_peer(&peer);

  ultimoMicros=micros();
}

unsigned long ultimoEnvio=0, ultimoBlink=0;
bool ledEst=false;

void loop(){
  float ax,ay,az,gx,gy,gz;
  unsigned long agora=micros();
  float dt=(agora-ultimoMicros)/1000000.0;
  ultimoMicros=agora;
  if(dt<=0||dt>0.2) dt=0.01;    // protege contra saltos

  if(mpuLer(ax,ay,az,gx,gy,gz)){
    // TILT: filtro complementar (acelerometro corrige o drift do giro)
    float accPitch = atan2(-ax, sqrt(ay*ay+az*az)) * 57.2958;
    pitch = 0.98*(pitch + (gy-gyBias)*dt) + 0.02*accPitch;
    // PAN (yaw): integra o giroscopio (deriva devagar -> botao recentraliza)
    yaw += (gz-gzBias)*dt;
  }

  if(digitalRead(RECENTER_BTN)==LOW){ yaw=0; }   // recentraliza o pan

  // Envia ~25x/s
  if(millis()-ultimoEnvio>=40){
    ultimoEnvio=millis();
    float p = 90 + yaw   * PAN_GANHO  * (PAN_INVERTE ?-1:1);
    float t = 90 + pitch * TILT_GANHO * (TILT_INVERTE?-1:1);
    pac.pan  = constrain((int)p, 0, 180);
    pac.tilt = constrain((int)t, 0, 180);
    esp_now_send(RECEIVER_MAC,(uint8_t*)&pac,sizeof(pac));
  }

  // LED: conectado = fixo; senao = piscando
  if(millis()-ultimoOk < 500){ digitalWrite(STATUS_LED,HIGH); }
  else if(millis()-ultimoBlink>200){ ultimoBlink=millis(); ledEst=!ledEst; digitalWrite(STATUS_LED,ledEst); }
}
