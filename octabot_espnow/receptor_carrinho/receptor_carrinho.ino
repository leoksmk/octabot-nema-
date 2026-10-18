/*
 * OCTABOT ESP-NOW - RECEPTOR (carrinho)  -- 100% ESP-NOW, SEM WiFi/HTTP
 * Placa: ESP32 DevKit v1 (Mainboard Future Makers 2k26).
 * Recebe de DOIS controles por ESP-NOW:
 *   - Controle da MAO (DevKit + MPU)  -> movimento do carrinho (pacote tipo 'M')
 *   - Controle VR     (C3 + MPU)      -> gimbal pan/tilt       (pacote tipo 'G')
 * Aciona: 2 motores NEMA17 (DRV8825) + 2 servos (gimbal) + 2 fitas WS2812B.
 *
 * BIBLIOTECAS: ESP32Servo (Kevin Harrington) + FastLED. (ESP-NOW ja vem no core.)
 *
 * >>> AO LIGAR, abra o Serial (115200): imprime o MAC desta placa.
 *     Cole esse MAC no RECEIVER_MAC dos DOIS controles. <<<
 */

#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <ESP32Servo.h>
#include <FastLED.h>

#define WIFI_CANAL 1     // o MESMO nos 3 ESP32

// Motores (DRV8825)
const int L_STEP = 25, L_DIR = 33, L_EN = 26;   // esquerdo (DRV3 / X3)
const int R_STEP = 27, R_DIR = 14, R_EN = 12;   // direito  (DRV1 / X1)

// Servos (gimbal)
const int SERVO_PAN_PIN  = 15;  // U3
const int SERVO_TILT_PIN = 2;   // U1
const bool PAN_INVERTE  = false;
const bool TILT_INVERTE = false;
const int PAN_MIN  = 10,  PAN_MAX  = 170;   // limites fisicos - CALIBRE
const int TILT_MIN = 30,  TILT_MAX = 150;
Servo servoPan, servoTilt;
int panAng = 90, tiltAng = 90;

// Fitas LED WS2812B
#define LED1_PIN 4        // U4
#define LED2_PIN 5        // U5
#define NUM_LEDS 25
#define LED_TYPE WS2812B
#define COLOR_ORDER GRB
const uint8_t LED_BRILHO = 160;
CRGB LED_COR = CRGB(0, 120, 255);
CRGB leds1[NUM_LEDS], leds2[NUM_LEDS];
bool ledLigado = true;

// Sentido das rodas
const bool L_INVERTE = false;
const bool R_INVERTE = true;

// Velocidade e rampa (us; MENOR = mais rapido)
const unsigned long INTERVALO_LENTO = 1200;
const unsigned long INTERVALO_MIN   = 150;
const unsigned long INTERVALO_MAX   = 2500;
const unsigned long RAMPA_POR_PASSO = 15;
unsigned long intervaloCruzeiro = 1325;
unsigned long intervaloAtual    = INTERVALO_LENTO;

enum Movimento { PARADO, FRENTE, TRAS, ESQUERDA, DIREITA };
Movimento estado = PARADO, comandado = PARADO;
unsigned long ultimoPasso = 0;
bool nivelPasso = false;

const unsigned long TIMEOUT_MS = 400;   // sem pacote de movimento -> para (dead-man)
unsigned long ultimoComando = 0;

// Pacote comum aos dois controles
typedef struct { uint8_t tipo; int16_t x; int16_t y; } Pacote;
// tipo 'G': x=pan,  y=tilt        (0..180)
// tipo 'M': x=dir (0=parado,1=F,2=T,3=E,4=D),  y=velocidade (0..100)

void doSpeed(int pct){ pct=constrain(pct,0,100); intervaloCruzeiro=map(pct,0,100,INTERVALO_MAX,INTERVALO_MIN); }
void doGimbal(int p,int t){
  int pf = PAN_INVERTE ?180-p:p;
  int tf = TILT_INVERTE?180-t:t;
  panAng  = constrain(pf, PAN_MIN,  PAN_MAX);
  tiltAng = constrain(tf, TILT_MIN, TILT_MAX);
  servoPan.write(panAng); servoTilt.write(tiltAng);
}
void aplicarLeds(){
  CRGB c=ledLigado?LED_COR:CRGB::Black;
  fill_solid(leds1,NUM_LEDS,c); fill_solid(leds2,NUM_LEDS,c); FastLED.show();
}

void onRecv(const esp_now_recv_info_t* info, const uint8_t* data, int len){
  if(len!=(int)sizeof(Pacote)) return;
  Pacote p; memcpy(&p,data,sizeof(p));
  if(p.tipo=='G'){
    doGimbal(p.x, p.y);
  } else if(p.tipo=='M'){
    switch(p.x){
      case 1: comandado=FRENTE;   break;
      case 2: comandado=TRAS;     break;
      case 3: comandado=ESQUERDA; break;
      case 4: comandado=DIREITA;  break;
      default: comandado=PARADO;  break;
    }
    if(p.x!=0) doSpeed(p.y);
    ultimoComando=millis();
  }
}

void drivers(bool on){digitalWrite(L_EN,on?LOW:HIGH);digitalWrite(R_EN,on?LOW:HIGH);}
void aplicarSentido(Movimento mv){
  bool esq,dir;
  switch(mv){
    case FRENTE:   esq=true; dir=true;  break;
    case TRAS:     esq=false;dir=false; break;
    case ESQUERDA: esq=false;dir=true;  break;
    case DIREITA:  esq=true; dir=false; break;
    default: return;
  }
  digitalWrite(L_DIR,(esq^L_INVERTE)?HIGH:LOW);
  digitalWrite(R_DIR,(dir^R_INVERTE)?HIGH:LOW);
}

void setup(){
  Serial.begin(115200);
  pinMode(L_STEP,OUTPUT);pinMode(L_DIR,OUTPUT);
  pinMode(R_STEP,OUTPUT);pinMode(R_DIR,OUTPUT);
  pinMode(L_EN,OUTPUT);  pinMode(R_EN,OUTPUT);
  drivers(false);

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
  Serial.println(WiFi.macAddress());    // <-- cole nos dois controles
  if(esp_now_init()==ESP_OK) esp_now_register_recv_cb(onRecv);
  else Serial.println("ESP-NOW falhou!");
}

void loop(){
  static unsigned long ultimoLed=0;
  if(millis()-ultimoLed>1000){ ultimoLed=millis(); aplicarLeds(); }  // refresh WS2812

  // dead-man: sem pacote de movimento recente, para
  if(comandado!=PARADO && millis()-ultimoComando>TIMEOUT_MS) comandado=PARADO;

  if(comandado!=estado && estado==PARADO){
    drivers(true);
    estado=comandado; aplicarSentido(estado);
    intervaloAtual=INTERVALO_LENTO;
  }
  if(estado==PARADO) return;

  bool freando=(comandado==PARADO)||(comandado!=estado);
  unsigned long alvo=freando?INTERVALO_LENTO:intervaloCruzeiro;

  unsigned long agora=micros();
  if(agora-ultimoPasso>=intervaloAtual){
    ultimoPasso=agora;
    nivelPasso=!nivelPasso;
    int nivel=nivelPasso?HIGH:LOW;
    digitalWrite(L_STEP,nivel);
    digitalWrite(R_STEP,nivel);
    if(nivelPasso){
      if(intervaloAtual>alvo){intervaloAtual-=RAMPA_POR_PASSO;if(intervaloAtual<alvo)intervaloAtual=alvo;}
      else if(intervaloAtual<alvo){intervaloAtual+=RAMPA_POR_PASSO;if(intervaloAtual>alvo)intervaloAtual=alvo;}
      if(freando && intervaloAtual>=INTERVALO_LENTO){
        if(comandado==PARADO){estado=PARADO;drivers(false);}
        else {estado=comandado;aplicarSentido(estado);}
      }
    }
  }
}
