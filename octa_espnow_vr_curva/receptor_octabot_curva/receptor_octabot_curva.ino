/*
 * OCTA ESP-NOW VR + CURVA - RECEPTOR (Octabot)
 * Carrinho dirigido pela WEB com CURVA ANDANDO (joystick de dirigir) e
 * GIMBAL pelo giroscopio via ESP-NOW. (Carro = WiFi/WebSocket; gimbal = ESP-NOW.)
 *
 * BIBLIOTECAS: ESP32Servo, FastLED, WebSockets (Markus Sattler). ESP-NOW vem no core.
 * >>> Serial 115200 imprime o MAC; cole no transmissor (controle_vr). <<<
 * Uso do carro: WiFi "Octabot" (senha 12345678) -> http://192.168.4.1
 */

#include <WiFi.h>
#include <WebServer.h>
#include <WebSocketsServer.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <ESP32Servo.h>
#include <FastLED.h>

const char* AP_SSID = "Octabot";
const char* AP_PASS = "12345678";
#define WIFI_CANAL 1
WebServer server(80);
WebSocketsServer webSocket(81);

// Motores (DRV8825)
const int L_STEP = 25, L_DIR = 33, L_EN = 26;
const int R_STEP = 27, R_DIR = 14, R_EN = 12;
const bool L_INVERTE = false;
const bool R_INVERTE = true;

// Servos (gimbal) - via ESP-NOW
const int SERVO_PAN_PIN = 15, SERVO_TILT_PIN = 2;
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

const unsigned long INTERVALO_LENTO = 1200;
const unsigned long INTERVALO_MIN   = 150;
const unsigned long RAMPA_POR_PASSO = 15;
const int DEADZONE_RODA = 8;

volatile int acelerador = 0, esterco = 0;
const unsigned long TIMEOUT_MS = 400;
unsigned long ultimoComando = 0;

typedef struct { int pan; int tilt; } Pacote;   // pacote do giroscopio (igual ao transmissor)

struct Roda { int STEP, DIRP; bool INV; unsigned long curInt, ultimo; bool nivel, ativo, dir; };
Roda rodaL = { L_STEP, L_DIR, L_INVERTE, INTERVALO_LENTO, 0, false, false, true };
Roda rodaR = { R_STEP, R_DIR, R_INVERTE, INTERVALO_LENTO, 0, false, false, true };

const char PAGINA[] PROGMEM = R"HTML(
<!DOCTYPE html><html lang="pt-br"><head>
<meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1,maximum-scale=1,user-scalable=no">
<title>Octabot</title>
<style>
  :root{color-scheme:dark}
  *{box-sizing:border-box;-webkit-user-select:none;user-select:none;-webkit-tap-highlight-color:transparent}
  body{margin:0;font-family:system-ui,sans-serif;background:#111;color:#eee;
       display:flex;flex-direction:column;align-items:center;justify-content:center;min-height:100vh;gap:14px;padding:10px}
  h1{font-size:1rem;font-weight:600;margin:0;opacity:.7}
  .sub{font-size:.8rem;opacity:.6;margin:0;text-align:center}
  .joy{position:relative;width:200px;height:200px;border-radius:50%;
       background:radial-gradient(circle,#242424 0%,#1a1a1a 70%);border:1px solid #333;touch-action:none}
  .knob{position:absolute;left:50%;top:50%;width:66px;height:66px;border-radius:50%;
        background:#3b82f6;transform:translate(-50%,-50%);transition:background .05s}
  .knob:active{background:#2563eb}
  .ctr{border:0;border-radius:12px;background:#2a2a2a;color:#eee;font-size:.9rem;padding:9px 16px}
  #st{font-size:.72rem;opacity:.5}
</style></head><body>
<h1>Octabot <span id="st">conectando...</span></h1>
<p class="sub">Dirigir (curva andando)</p>
<div class="joy" id="drv"><div class="knob" id="kdrv"></div></div>
<button class="ctr" id="led">LED: Ligado</button>
<p class="sub">Gimbal: controlado pelo giroscopio (ESP-NOW)</p>
<script>
let ws, wsOk=false;
function wsConnect(){
  ws=new WebSocket('ws://'+location.hostname+':81/');
  ws.onopen=()=>{wsOk=true;document.getElementById('st').textContent='';};
  ws.onclose=()=>{wsOk=false;document.getElementById('st').textContent='reconectando...';setTimeout(wsConnect,800);};
  ws.onerror=()=>{try{ws.close();}catch(e){}};
}
wsConnect();
function tx(s){ if(wsOk){try{ws.send(s);}catch(e){}} }

// Joystick de dirigir: vertical=acelerador (cima=frente), horizontal=esterco. Mola -> para.
(function(){
  const area=document.getElementById('drv'), knob=document.getElementById('kdrv');
  let on=false, ult=0, pend=null;
  function mv(nx,ny){knob.style.left=(50+nx*50)+'%';knob.style.top=(50+ny*50)+'%';}
  function envia(nx,ny){const now=Date.now(),falta=40-(now-ult); if(pend){clearTimeout(pend);pend=null;}
    const f=()=>{ult=Date.now();tx('m'+Math.round(-ny*100)+','+Math.round(nx*100));};
    if(falta<=0){ult=now;f();} else pend=setTimeout(f,falta);}
  function pos(e){const r=area.getBoundingClientRect(),jr=r.width/2;
    let dx=(e.clientX-(r.left+jr))/jr,dy=(e.clientY-(r.top+jr))/jr; const d=Math.hypot(dx,dy);if(d>1){dx/=d;dy/=d;}
    mv(dx,dy); envia(dx,dy);}
  area.addEventListener('pointerdown',e=>{e.preventDefault();on=true;knob.style.transition='background .05s';pos(e);});
  window.addEventListener('pointermove',e=>{if(on)pos(e);});
  function soltar(){if(!on)return;on=false;knob.style.transition='left .15s ease-out,top .15s ease-out';mv(0,0);tx('m0,0');}
  window.addEventListener('pointerup',soltar);window.addEventListener('pointercancel',soltar);
  window.addEventListener('touchend',soltar);window.addEventListener('mouseup',soltar);
})();

let ledOn=true;
const btLed=document.getElementById('led');
btLed.addEventListener('click',()=>{ledOn=!ledOn;btLed.textContent='LED: '+(ledOn?'Ligado':'Desligado');btLed.style.background=ledOn?'#2a2a2a':'#4a1111';tx(ledOn?'l1':'l0');});
document.addEventListener('visibilitychange',()=>{if(document.hidden)tx('m0,0');});
</script></body></html>
)HTML";

void doGimbal(int p,int t){
  int pf=PAN_INVERTE?180-p:p, tf=TILT_INVERTE?180-t:t;
  panAng=constrain(pf,PAN_MIN,PAN_MAX); tiltAng=constrain(tf,TILT_MIN,TILT_MAX);
  servoPan.write(panAng); servoTilt.write(tiltAng);
}
void aplicarLeds(){ CRGB c=ledLigado?LED_COR:CRGB::Black; fill_solid(leds1,NUM_LEDS,c); fill_solid(leds2,NUM_LEDS,c); FastLED.show(); }

// ESP-NOW: gimbal do giroscopio
void onRecv(const esp_now_recv_info_t* info, const uint8_t* data, int len){
  if(len==(int)sizeof(Pacote)){ Pacote p; memcpy(&p,data,sizeof(p)); doGimbal(p.pan,p.tilt); }
}

void onWsEvent(uint8_t num, WStype_t type, uint8_t* payload, size_t len){
  if(type!=WStype_TEXT || len<1) return;
  char* c=(char*)payload;
  if(c[0]=='m'){ int a=0,e=0; sscanf(c+1,"%d,%d",&a,&e); acelerador=constrain(a,-100,100); esterco=constrain(e,-100,100); ultimoComando=millis(); }
  else if(c[0]=='l'){ ledLigado=(c[1]=='1'); aplicarLeds(); }
}

void handleRaiz(){ server.send_P(200,"text/html",PAGINA); }

void passoRoda(Roda &r, int v){
  int mag=abs(v);
  if(mag<DEADZONE_RODA){ r.ativo=false; return; }
  bool fwd=(v>0);
  unsigned long alvo=map(constrain(mag,DEADZONE_RODA,100),DEADZONE_RODA,100,INTERVALO_LENTO,INTERVALO_MIN);
  if(!r.ativo || fwd!=r.dir){ r.ativo=true; r.dir=fwd; r.curInt=INTERVALO_LENTO; digitalWrite(r.DIRP,(fwd^r.INV)?HIGH:LOW); }
  unsigned long agora=micros();
  if(agora-r.ultimo>=r.curInt){
    r.ultimo=agora; r.nivel=!r.nivel; digitalWrite(r.STEP,r.nivel?HIGH:LOW);
    if(r.nivel){ if(r.curInt>alvo){r.curInt-=RAMPA_POR_PASSO;if(r.curInt<alvo)r.curInt=alvo;}
                 else if(r.curInt<alvo){r.curInt+=RAMPA_POR_PASSO;if(r.curInt>alvo)r.curInt=alvo;} }
  }
}

void setup(){
  Serial.begin(115200);
  pinMode(L_STEP,OUTPUT);pinMode(L_DIR,OUTPUT);pinMode(L_EN,OUTPUT);
  pinMode(R_STEP,OUTPUT);pinMode(R_DIR,OUTPUT);pinMode(R_EN,OUTPUT);
  digitalWrite(L_EN,HIGH); digitalWrite(R_EN,HIGH);

  ESP32PWM::allocateTimer(0);ESP32PWM::allocateTimer(1);ESP32PWM::allocateTimer(2);ESP32PWM::allocateTimer(3);
  servoPan.setPeriodHertz(50); servoTilt.setPeriodHertz(50);
  servoPan.attach(SERVO_PAN_PIN,500,2500); servoTilt.attach(SERVO_TILT_PIN,500,2500);
  servoPan.write(panAng); servoTilt.write(tiltAng);

  FastLED.addLeds<LED_TYPE,LED1_PIN,COLOR_ORDER>(leds1,NUM_LEDS);
  FastLED.addLeds<LED_TYPE,LED2_PIN,COLOR_ORDER>(leds2,NUM_LEDS);
  FastLED.setBrightness(LED_BRILHO); aplicarLeds();

  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID,AP_PASS,WIFI_CANAL);          // canal fixo p/ conviver com ESP-NOW
  Serial.println(); Serial.print("MAC desta placa (use no transmissor): ");
  Serial.println(WiFi.softAPmacAddress());

  if(esp_now_init()==ESP_OK) esp_now_register_recv_cb(onRecv);
  else Serial.println("ESP-NOW falhou!");

  server.on("/",handleRaiz); server.begin();
  webSocket.begin(); webSocket.onEvent(onWsEvent);
}

void loop(){
  webSocket.loop(); server.handleClient();
  static unsigned long ultimoLed=0;
  if(millis()-ultimoLed>1000){ ultimoLed=millis(); aplicarLeds(); }

  if(millis()-ultimoComando>TIMEOUT_MS){ acelerador=0; esterco=0; }

  int left =constrain(acelerador+esterco,-100,100);
  int right=constrain(acelerador-esterco,-100,100);
  digitalWrite(L_EN,(abs(left) >=DEADZONE_RODA)?LOW:HIGH);
  digitalWrite(R_EN,(abs(right)>=DEADZONE_RODA)?LOW:HIGH);
  passoRoda(rodaL,left);
  passoRoda(rodaR,right);
}
