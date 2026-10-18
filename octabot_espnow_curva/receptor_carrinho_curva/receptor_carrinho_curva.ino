/*
 * OCTABOT ESP-NOW (CURVA ANDANDO) - RECEPTOR (carrinho)  -- 100% ESP-NOW
 * Drive DIFERENCIAL com mistura: cada roda gira em velocidade independente,
 * entao o carrinho faz CURVA ANDANDO (frente + giro ao mesmo tempo).
 *
 * Recebe por ESP-NOW:
 *   - Controle da MAO (pacote 'M'): x = acelerador (-100..100), y = esterco (-100..100)
 *   - Controle VR     (pacote 'G'): x = pan, y = tilt (gimbal)
 *
 * Mistura: rodaEsq = acelerador + esterco ; rodaDir = acelerador - esterco
 *
 * BIBLIOTECAS: ESP32Servo + FastLED. (ESP-NOW ja vem no core.)
 * >>> Serial 115200 imprime o MAC; cole nos dois controles. <<<
 */

#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <ESP32Servo.h>
#include <FastLED.h>

#define WIFI_CANAL 1

// Motores (DRV8825)
const int L_STEP = 25, L_DIR = 33, L_EN = 26;   // esquerdo (DRV3 / X3)
const int R_STEP = 27, R_DIR = 14, R_EN = 12;   // direito  (DRV1 / X1)
const bool L_INVERTE = false;
const bool R_INVERTE = true;

// Servos (gimbal)
const int SERVO_PAN_PIN = 15, SERVO_TILT_PIN = 2;   // U3 / U1
const bool PAN_INVERTE = false, TILT_INVERTE = false;
const int PAN_MIN = 10, PAN_MAX = 170, TILT_MIN = 30, TILT_MAX = 150;
Servo servoPan, servoTilt;
int panAng = 90, tiltAng = 90;

// Fitas LED WS2812B
#define LED1_PIN 4
#define LED2_PIN 5
#define NUM_LEDS 25
#define LED_TYPE WS2812B
#define COLOR_ORDER GRB
const uint8_t LED_BRILHO = 160;
CRGB LED_COR = CRGB(0,120,255);
CRGB leds1[NUM_LEDS], leds2[NUM_LEDS];
bool ledLigado = true;

// Velocidade/rampa (us; MENOR = mais rapido)
const unsigned long INTERVALO_LENTO = 1200;  // partida de cada roda
const unsigned long INTERVALO_MIN   = 150;   // teto de velocidade
const unsigned long RAMPA_POR_PASSO = 15;
const int DEADZONE_RODA = 8;                 // abaixo disso a roda fica parada

// Comandos recebidos
volatile int acelerador = 0, esterco = 0;    // -100..100
const unsigned long TIMEOUT_MS = 400;
unsigned long ultimoComando = 0;

typedef struct { uint8_t tipo; int16_t x; int16_t y; } Pacote;

// Estado de cada roda (stepping independente)
struct Roda { int STEP, DIRP; bool INV; unsigned long curInt, ultimo; bool nivel, ativo, dir; };
Roda rodaL = { L_STEP, L_DIR, L_INVERTE, INTERVALO_LENTO, 0, false, false, true };
Roda rodaR = { R_STEP, R_DIR, R_INVERTE, INTERVALO_LENTO, 0, false, false, true };

void doGimbal(int p,int t){
  int pf = PAN_INVERTE ?180-p:p, tf = TILT_INVERTE?180-t:t;
  panAng  = constrain(pf, PAN_MIN,  PAN_MAX);
  tiltAng = constrain(tf, TILT_MIN, TILT_MAX);
  servoPan.write(panAng); servoTilt.write(tiltAng);
}
void aplicarLeds(){
  CRGB c = ledLigado?LED_COR:CRGB::Black;
  fill_solid(leds1,NUM_LEDS,c); fill_solid(leds2,NUM_LEDS,c); FastLED.show();
}

void onRecv(const esp_now_recv_info_t* info, const uint8_t* data, int len){
  if(len!=(int)sizeof(Pacote)) return;
  Pacote p; memcpy(&p,data,sizeof(p));
  if(p.tipo=='G'){ doGimbal(p.x,p.y); }
  else if(p.tipo=='M'){ acelerador=constrain((int)p.x,-100,100); esterco=constrain((int)p.y,-100,100); ultimoComando=millis(); }
}

// Gera os passos de UMA roda conforme a velocidade com sinal (-100..100)
void passoRoda(Roda &r, int v){
  int mag = abs(v);
  if(mag < DEADZONE_RODA){ r.ativo=false; return; }   // parada
  bool fwd = (v > 0);
  unsigned long alvo = map(constrain(mag,DEADZONE_RODA,100), DEADZONE_RODA,100, INTERVALO_LENTO, INTERVALO_MIN);
  if(!r.ativo || fwd!=r.dir){                         // (re)inicia: parte devagar e ajusta DIR
    r.ativo=true; r.dir=fwd; r.curInt=INTERVALO_LENTO;
    digitalWrite(r.DIRP, (fwd ^ r.INV)?HIGH:LOW);
  }
  unsigned long agora=micros();
  if(agora-r.ultimo >= r.curInt){
    r.ultimo=agora;
    r.nivel=!r.nivel;
    digitalWrite(r.STEP, r.nivel?HIGH:LOW);
    if(r.nivel){                                       // rampa 1x por passo completo
      if(r.curInt>alvo){ r.curInt-=RAMPA_POR_PASSO; if(r.curInt<alvo)r.curInt=alvo; }
      else if(r.curInt<alvo){ r.curInt+=RAMPA_POR_PASSO; if(r.curInt>alvo)r.curInt=alvo; }
    }
  }
}

void setup(){
  Serial.begin(115200);
  pinMode(L_STEP,OUTPUT);pinMode(L_DIR,OUTPUT);pinMode(L_EN,OUTPUT);
  pinMode(R_STEP,OUTPUT);pinMode(R_DIR,OUTPUT);pinMode(R_EN,OUTPUT);
  digitalWrite(L_EN,HIGH); digitalWrite(R_EN,HIGH);   // comeca desligado

  ESP32PWM::allocateTimer(0);ESP32PWM::allocateTimer(1);
  ESP32PWM::allocateTimer(2);ESP32PWM::allocateTimer(3);
  servoPan.setPeriodHertz(50); servoTilt.setPeriodHertz(50);
  servoPan.attach(SERVO_PAN_PIN,500,2500);
  servoTilt.attach(SERVO_TILT_PIN,500,2500);
  servoPan.write(panAng); servoTilt.write(tiltAng);

  FastLED.addLeds<LED_TYPE,LED1_PIN,COLOR_ORDER>(leds1,NUM_LEDS);
  FastLED.addLeds<LED_TYPE,LED2_PIN,COLOR_ORDER>(leds2,NUM_LEDS);
  FastLED.setBrightness(LED_BRILHO);
  aplicarLeds();

  WiFi.mode(WIFI_STA);
  esp_wifi_set_channel(WIFI_CANAL, WIFI_SECOND_CHAN_NONE);
  Serial.println();
  Serial.print("MAC deste carrinho (use nos controles): ");
  Serial.println(WiFi.macAddress());
  if(esp_now_init()==ESP_OK) esp_now_register_recv_cb(onRecv);
  else Serial.println("ESP-NOW falhou!");
}

void loop(){
  static unsigned long ultimoLed=0;
  if(millis()-ultimoLed>1000){ ultimoLed=millis(); aplicarLeds(); }

  // dead-man: sem comando recente, zera tudo
  if(millis()-ultimoComando > TIMEOUT_MS){ acelerador=0; esterco=0; }

  // MISTURA: curva andando
  int left  = constrain(acelerador + esterco, -100, 100);
  int right = constrain(acelerador - esterco, -100, 100);

  // liga/desliga cada driver conforme a roda esta ativa (menos calor)
  digitalWrite(L_EN, (abs(left) >=DEADZONE_RODA)?LOW:HIGH);
  digitalWrite(R_EN, (abs(right)>=DEADZONE_RODA)?LOW:HIGH);

  passoRoda(rodaL, left);
  passoRoda(rodaR, right);
}
