/*
 * OCTABOT ESP-NOW - CONTROLE VR (gimbal por head-tracking)
 * Placa: ESP32-C3 Super Mini.  Giroscopio: MPU6050.  LED vermelho 2 pinos (status).
 *
 * Mover a cabeca move o gimbal:
 *   - inclinar (acenar) -> TILT  (acelerometro, sem drift)
 *   - virar (yaw)       -> PAN   (giroscopio integrado; calibra no boot; botao recentraliza)
 * Envia pacote tipo 'G' (x=pan, y=tilt) por ESP-NOW pro carrinho.
 *
 * LED: PISCANDO = sem conexao ; ACESO = conectado.
 *
 * >>> cole em RECEIVER_MAC o MAC que o carrinho imprime no Serial <<<
 *
 * Ligacao: MPU6050 VCC->3V3, GND->GND, SDA->GPIO8, SCL->GPIO9
 *          LED: GPIO10 -> resistor 220ohm -> LED(+) ; LED(-) -> GND
 *          Botao recentralizar: GPIO3 -> botao -> GND
 */

#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <Wire.h>

uint8_t RECEIVER_MAC[] = {0x00,0x00,0x00,0x00,0x00,0x00}; // <-- MAC do carrinho
#define WIFI_CANAL   1
#define SDA_PIN      8
#define SCL_PIN      9
#define STATUS_LED   10
#define RECENTER_BTN 3
#define MPU_ADDR     0x68
const float PAN_GANHO=1.0, TILT_GANHO=1.0;
const bool  PAN_INVERTE=false, TILT_INVERTE=false;

typedef struct { uint8_t tipo; int16_t x; int16_t y; } Pacote;
Pacote pac;

volatile unsigned long ultimoOk=0;
void onSent(const uint8_t* mac, esp_now_send_status_t s){ if(s==ESP_NOW_SEND_SUCCESS) ultimoOk=millis(); }

float gyBias=0, gzBias=0, pitch=0, yaw=0;
unsigned long ultimoMicros=0;

void mpuEscreve(uint8_t r,uint8_t v){ Wire.beginTransmission(MPU_ADDR);Wire.write(r);Wire.write(v);Wire.endTransmission(true); }
bool mpuLer(float&ax,float&ay,float&az,float&gx,float&gy,float&gz){
  Wire.beginTransmission(MPU_ADDR); Wire.write(0x3B);
  if(Wire.endTransmission(false)!=0) return false;
  if(Wire.requestFrom(MPU_ADDR,14,true)!=14) return false;
  int16_t axr=(Wire.read()<<8)|Wire.read(), ayr=(Wire.read()<<8)|Wire.read(), azr=(Wire.read()<<8)|Wire.read();
  Wire.read();Wire.read();
  int16_t gxr=(Wire.read()<<8)|Wire.read(), gyr=(Wire.read()<<8)|Wire.read(), gzr=(Wire.read()<<8)|Wire.read();
  ax=axr/16384.0; ay=ayr/16384.0; az=azr/16384.0;
  gx=gxr/131.0;   gy=gyr/131.0;   gz=gzr/131.0;
  return true;
}
void calibrarGiro(){ float sy=0,sz=0; int n=0;
  for(int i=0;i<300;i++){ float ax,ay,az,gx,gy,gz; if(mpuLer(ax,ay,az,gx,gy,gz)){sy+=gy;sz+=gz;n++;} delay(3);}
  if(n){gyBias=sy/n; gzBias=sz/n;}
}

void setup(){
  pinMode(STATUS_LED,OUTPUT); pinMode(RECENTER_BTN,INPUT_PULLUP);
  Wire.begin(SDA_PIN,SCL_PIN);
  mpuEscreve(0x6B,0); delay(100);
  calibrarGiro();                     // fique PARADO ao ligar

  WiFi.mode(WIFI_STA);
  esp_wifi_set_channel(WIFI_CANAL, WIFI_SECOND_CHAN_NONE);
  if(esp_now_init()!=ESP_OK){ while(true){digitalWrite(STATUS_LED,millis()%120<60);} }
  esp_now_register_send_cb(onSent);
  esp_now_peer_info_t peer={}; memcpy(peer.peer_addr,RECEIVER_MAC,6); peer.channel=WIFI_CANAL; peer.encrypt=false;
  esp_now_add_peer(&peer);
  ultimoMicros=micros();
}

unsigned long ultimoEnvio=0, ultimoBlink=0; bool ledEst=false;
void loop(){
  float ax,ay,az,gx,gy,gz;
  unsigned long agora=micros();
  float dt=(agora-ultimoMicros)/1000000.0; ultimoMicros=agora;
  if(dt<=0||dt>0.2) dt=0.01;
  if(mpuLer(ax,ay,az,gx,gy,gz)){
    float accPitch=atan2(-ax,sqrt(ay*ay+az*az))*57.2958;
    pitch=0.98*(pitch+(gy-gyBias)*dt)+0.02*accPitch;
    yaw += (gz-gzBias)*dt;
  }
  if(digitalRead(RECENTER_BTN)==LOW) yaw=0;

  if(millis()-ultimoEnvio>=40){
    ultimoEnvio=millis();
    int p=(int)(90 + yaw  *PAN_GANHO *(PAN_INVERTE ?-1:1));
    int t=(int)(90 + pitch*TILT_GANHO*(TILT_INVERTE?-1:1));
    pac.tipo='G'; pac.x=constrain(p,0,180); pac.y=constrain(t,0,180);
    esp_now_send(RECEIVER_MAC,(uint8_t*)&pac,sizeof(pac));
  }
  if(millis()-ultimoOk<500) digitalWrite(STATUS_LED,HIGH);
  else if(millis()-ultimoBlink>200){ultimoBlink=millis();ledEst=!ledEst;digitalWrite(STATUS_LED,ledEst);}
}
