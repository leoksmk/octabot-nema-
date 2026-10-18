/*
 * OCTABOT ESP-NOW - CONTROLE DA MAO (dirige o carrinho pela inclinacao)
 * Placa: ESP32 DevKit v1.  Giroscopio: MPU6050.  LED vermelho 2 pinos (status).
 *
 * Inclinar a mao dirige o carrinho:
 *   - inclinar p/ FRENTE  -> anda pra frente
 *   - inclinar p/ TRAS    -> re
 *   - inclinar p/ ESQUERDA-> gira esquerda (tank)
 *   - inclinar p/ DIREITA -> gira direita (tank)
 *   - mao NIVELADA        -> parado  (zona morta = dead-man natural)
 *   - quanto MAIS inclina, MAIS rapido (velocidade proporcional)
 * Envia pacote tipo 'M' (x=direcao, y=velocidade) por ESP-NOW pro carrinho.
 *
 * LED: PISCANDO = sem conexao ; ACESO = conectado.
 *
 * >>> cole em RECEIVER_MAC o MAC que o carrinho imprime no Serial <<<
 *
 * Ligacao: MPU6050 VCC->3V3, GND->GND, SDA->GPIO21, SCL->GPIO22
 *          LED: GPIO2 (onboard) ou externo -> resistor 220ohm -> LED(+) ; LED(-) -> GND
 */

#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <Wire.h>

uint8_t RECEIVER_MAC[] = {0x00,0x00,0x00,0x00,0x00,0x00}; // <-- MAC do carrinho
#define WIFI_CANAL  1
#define SDA_PIN     21     // I2C padrao do ESP32 DevKit
#define SCL_PIN     22
#define STATUS_LED  2      // LED onboard (ou externo)
#define MPU_ADDR    0x68

const float DEADZONE = 12;  // graus: mao nivelada ate aqui = PARADO
const float MAXANG   = 45;  // graus p/ velocidade maxima
const bool  INV_PITCH = false; // troque se frente/tras vier invertido
const bool  INV_ROLL  = false; // troque se esquerda/direita vier invertido

typedef struct { uint8_t tipo; int16_t x; int16_t y; } Pacote;
Pacote pac;

volatile unsigned long ultimoOk=0;
void onSent(const uint8_t* mac, esp_now_send_status_t s){ if(s==ESP_NOW_SEND_SUCCESS) ultimoOk=millis(); }

void mpuEscreve(uint8_t r,uint8_t v){ Wire.beginTransmission(MPU_ADDR);Wire.write(r);Wire.write(v);Wire.endTransmission(true); }
bool mpuLerAccel(float&ax,float&ay,float&az){
  Wire.beginTransmission(MPU_ADDR); Wire.write(0x3B);
  if(Wire.endTransmission(false)!=0) return false;
  if(Wire.requestFrom(MPU_ADDR,6,true)!=6) return false;
  int16_t x=(Wire.read()<<8)|Wire.read(), y=(Wire.read()<<8)|Wire.read(), z=(Wire.read()<<8)|Wire.read();
  ax=x/16384.0; ay=y/16384.0; az=z/16384.0; return true;
}

void setup(){
  pinMode(STATUS_LED,OUTPUT);
  Wire.begin(SDA_PIN,SCL_PIN);
  mpuEscreve(0x6B,0); delay(100);

  WiFi.mode(WIFI_STA);
  esp_wifi_set_channel(WIFI_CANAL, WIFI_SECOND_CHAN_NONE);
  if(esp_now_init()!=ESP_OK){ while(true){digitalWrite(STATUS_LED,millis()%120<60);} }
  esp_now_register_send_cb(onSent);
  esp_now_peer_info_t peer={}; memcpy(peer.peer_addr,RECEIVER_MAC,6); peer.channel=WIFI_CANAL; peer.encrypt=false;
  esp_now_add_peer(&peer);
}

unsigned long ultimoEnvio=0, ultimoBlink=0; bool ledEst=false;
void loop(){
  if(millis()-ultimoEnvio>=50){
    ultimoEnvio=millis();
    float ax,ay,az;
    int dir=0, vel=0;
    if(mpuLerAccel(ax,ay,az)){
      float pitch=atan2(-ax,sqrt(ay*ay+az*az))*57.2958; if(INV_PITCH)pitch=-pitch;
      float roll =atan2(ay,az)*57.2958;                 if(INV_ROLL) roll =-roll;
      float ap=fabs(pitch), ar=fabs(roll);
      if(ap<DEADZONE && ar<DEADZONE){ dir=0; vel=0; }     // nivelada -> parado
      else if(ap>=ar){                                     // frente/tras domina
        dir = (pitch>0)?1:2;
        vel = map((long)constrain(ap,DEADZONE,MAXANG), (long)DEADZONE,(long)MAXANG, 0,100);
      } else {                                             // esquerda/direita domina
        dir = (roll>0)?4:3;                                // 4=D, 3=E
        vel = map((long)constrain(ar,DEADZONE,MAXANG), (long)DEADZONE,(long)MAXANG, 0,100);
      }
    }
    pac.tipo='M'; pac.x=dir; pac.y=constrain(vel,0,100);
    esp_now_send(RECEIVER_MAC,(uint8_t*)&pac,sizeof(pac));
  }
  if(millis()-ultimoOk<500) digitalWrite(STATUS_LED,HIGH);
  else if(millis()-ultimoBlink>200){ultimoBlink=millis();ledEst=!ledEst;digitalWrite(STATUS_LED,ledEst);}
}
