/*
 * Carrinho 2x NEMA 17 (tracao diferencial) - Mainboard Future Makers 2k26
 * ESP32 DevKit v1 (30 pinos) + 2 DRV8825 + 2 servos (gimbal) + 2 fitas WS2812B.
 * Controle SEM FIO por pagina web; comandos via WebSocket (baixa latencia).
 *
 * BIBLIOTECAS (Library Manager):
 *   - ESP32Servo   (Kevin Harrington)
 *   - FastLED
 *   - WebSockets   (Markus Sattler / links2004)   <-- NOVA
 *
 * Uso: WiFi "Octabot" (senha 12345678) -> navegador em http://192.168.4.1
 */

#include <WiFi.h>
#include <WebServer.h>
#include <WebSocketsServer.h>
#include <ESP32Servo.h>
#include <FastLED.h>

const char* AP_SSID = "Octabot";
const char* AP_PASS = "12345678";
WebServer server(80);
WebSocketsServer webSocket(81);   // controle em tempo real

// ---------- Motores (DRV8825) ----------
const int L_STEP = 25, L_DIR = 33, L_EN = 26;   // esquerdo (DRV3 / X3)
const int R_STEP = 27, R_DIR = 14, R_EN = 12;   // direito  (DRV1 / X1)

// ---------- Servos (gimbal) ----------
const int SERVO_PAN_PIN  = 15;  // U3
const int SERVO_TILT_PIN = 2;   // U1
const bool PAN_INVERTE  = true;
const bool TILT_INVERTE = true;
// LIMITES de angulo (graus FISICOS que o servo pode ir). Ajuste pro SEU gimbal:
// comece estreito e va abrindo ate pouco antes de travar na mecanica. NUNCA deixe
// o servo forcar contra o batente -> queima/estraga a engrenagem.
const int PAN_MIN  = 10,  PAN_MAX  = 170;
const int TILT_MIN = 30,  TILT_MAX = 150;
Servo servoPan, servoTilt;
int panAng = 90, tiltAng = 90;

// ---------- Fitas LED WS2812B (FastLED) ----------
#define LED1_PIN     4        // U4
#define LED2_PIN     5        // U5
#define NUM_LEDS     25
#define LED_TYPE     WS2812B
#define COLOR_ORDER  GRB
const uint8_t LED_BRILHO = 160;
CRGB LED_COR = CRGB(0, 120, 255);
CRGB leds1[NUM_LEDS];
CRGB leds2[NUM_LEDS];
bool ledLigado = true;

// ---------- Sentido das rodas ----------
const bool L_INVERTE = false;
const bool R_INVERTE = true;

// ---------- Velocidade e rampa (us; MENOR = mais rapido) ----------
const unsigned long INTERVALO_LENTO = 1200;
const unsigned long INTERVALO_MIN   = 150;   // se travar/chiar, suba p/ 180-220
const unsigned long INTERVALO_MAX   = 2500;
const unsigned long RAMPA_POR_PASSO = 15;
unsigned long intervaloCruzeiro = 1325;
unsigned long intervaloAtual    = INTERVALO_LENTO;

enum Movimento { PARADO, FRENTE, TRAS, ESQUERDA, DIREITA };
Movimento estado = PARADO, comandado = PARADO;
unsigned long ultimoPasso = 0;
bool nivelPasso = false;

const unsigned long TIMEOUT_MS = 500;
unsigned long ultimoComando = 0;

const char PAGINA[] PROGMEM = R"HTML(
<!DOCTYPE html><html lang="pt-br"><head>
<meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1,maximum-scale=1,user-scalable=no">
<title>Octabot</title>
<style>
  :root{color-scheme:dark}
  *{box-sizing:border-box;-webkit-user-select:none;user-select:none;-webkit-tap-highlight-color:transparent}
  body{margin:0;font-family:system-ui,sans-serif;background:#111;color:#eee;
       display:flex;flex-direction:column;align-items:center;justify-content:center;
       min-height:100vh;gap:12px;padding:10px}
  h1{font-size:1rem;font-weight:600;margin:0;opacity:.7}
  /* layout HORIZONTAL: carrinho a esquerda, gimbal a direita */
  .wrap{display:flex;flex-direction:row;flex-wrap:wrap;gap:28px;align-items:center;justify-content:center}
  .col{display:flex;flex-direction:column;align-items:center;gap:12px}
  .pad{display:grid;grid-template-columns:repeat(3,76px);grid-template-rows:repeat(3,76px);gap:10px}
  button{border:0;border-radius:16px;background:#2a2a2a;color:#eee;font-size:1.5rem;
         touch-action:none;transition:background .05s}
  button:active{background:#3b82f6}
  .up{grid-area:1/2}.left{grid-area:2/1}.stop{grid-area:2/2;background:#7f1d1d;font-size:.95rem}
  .right{grid-area:2/3}.down{grid-area:3/2}
  .vel{display:flex;flex-direction:column;align-items:center;gap:6px;width:250px}
  .vel label{font-size:.85rem;opacity:.85;font-variant-numeric:tabular-nums}
  input[type=range]{width:100%;height:34px;accent-color:#3b82f6;touch-action:none}
  .sub{font-size:.8rem;opacity:.6;margin:0}
  .joy{position:relative;width:190px;height:190px;border-radius:50%;
       background:radial-gradient(circle,#242424 0%,#1a1a1a 70%);border:1px solid #333;touch-action:none}
  .knob{position:absolute;left:50%;top:50%;width:62px;height:62px;border-radius:50%;
        background:#3b82f6;transform:translate(-50%,-50%);transition:background .05s}
  .knob:active{background:#2563eb}
  .ctr{border:0;border-radius:12px;background:#2a2a2a;color:#eee;font-size:.9rem;padding:9px 16px}
  #st{font-size:.72rem;opacity:.5}
</style></head><body>
<h1>Octabot <span id="st">conectando...</span></h1>
<div class="wrap">
  <div class="col">
    <div class="pad">
      <button class="up"    data-m="f">&#9650;</button>
      <button class="left"  data-m="e">&#9664;</button>
      <button class="stop"  data-m="p">STOP</button>
      <button class="right" data-m="d">&#9654;</button>
      <button class="down"  data-m="t">&#9660;</button>
    </div>
    <div class="vel">
      <label>Velocidade: <span id="v">50</span>%</label>
      <input id="s" type="range" min="0" max="100" value="50">
    </div>
  </div>
  <div class="col">
    <p class="sub">Gimbal (pan / tilt)</p>
    <div class="joy" id="joy"><div class="knob" id="knob"></div></div>
    <div style="display:flex;gap:10px">
      <button class="ctr" id="ctr">Centralizar</button>
      <button class="ctr" id="led">LED: Ligado</button>
    </div>
  </div>
</div>
<script>
// ---- WebSocket (controle em tempo real, reconecta sozinho) ----
let ws, wsOk=false;
function wsConnect(){
  ws=new WebSocket('ws://'+location.hostname+':81/');
  ws.onopen =()=>{wsOk=true; document.getElementById('st').textContent='';};
  ws.onclose=()=>{wsOk=false; document.getElementById('st').textContent='reconectando...'; setTimeout(wsConnect,800);};
  ws.onerror=()=>{try{ws.close();}catch(e){}};
}
wsConnect();
function tx(s){ if(wsOk){ try{ws.send(s);}catch(e){} } }

// ---- Velocidade ----
const sl=document.getElementById('s');
sl.addEventListener('input',()=>{
  document.getElementById('v').textContent=sl.value;
  tx('v'+sl.value);
});

// ---- LED on/off ----
let ledOn=true;
const btLed=document.getElementById('led');
btLed.addEventListener('click',()=>{
  ledOn=!ledOn;
  btLed.textContent='LED: '+(ledOn?'Ligado':'Desligado');
  btLed.style.background=ledOn?'#2a2a2a':'#4a1111';
  tx(ledOn?'l1':'l0');
});

// ---- Gimbal: manche com mola; solta -> bolinha ao centro, servo mantem ----
const joy=document.getElementById('joy'), knob=document.getElementById('knob');
let arrastando=false;
let panAng=90, tiltAng=90, panAnc=90, tiltAnc=90;
const SPAN=90;
let ultG=0, pendG=null;   // throttle do envio do gimbal
function moverKnob(nx,ny){knob.style.left=(50+nx*50)+'%';knob.style.top=(50+ny*50)+'%';}
function enviarGimbal(){
  // limita a ~25 msg/s pra nao entupir o buffer (o que causava o atraso de ~1s)
  const now=Date.now(), falta=40-(now-ultG);
  if(pendG){clearTimeout(pendG);pendG=null;}
  if(falta<=0){ ultG=now; tx('g'+Math.round(panAng)+','+Math.round(tiltAng)); }
  else { pendG=setTimeout(()=>{ultG=Date.now(); tx('g'+Math.round(panAng)+','+Math.round(tiltAng)); pendG=null;}, falta); }
}
function pos(e){
  const r=joy.getBoundingClientRect(),jr=r.width/2;
  let dx=(e.clientX-(r.left+jr))/jr, dy=(e.clientY-(r.top+jr))/jr;
  const d=Math.hypot(dx,dy); if(d>1){dx/=d;dy/=d;}
  moverKnob(dx,dy);
  panAng =Math.max(0,Math.min(180,panAnc +dx*SPAN));
  tiltAng=Math.max(0,Math.min(180,tiltAnc-dy*SPAN));
  enviarGimbal();
}
joy.addEventListener('pointerdown',e=>{e.preventDefault();arrastando=true;knob.style.transition='background .05s';panAnc=panAng;tiltAnc=tiltAng;pos(e);});
window.addEventListener('pointermove',e=>{if(arrastando)pos(e);});
function soltar(){ if(!arrastando)return; arrastando=false; knob.style.transition='left .15s ease-out,top .15s ease-out'; moverKnob(0,0); enviarGimbal(); } // desliza so na volta
window.addEventListener('pointerup',soltar);
window.addEventListener('pointercancel',soltar);
window.addEventListener('touchend',soltar);
window.addEventListener('mouseup',soltar);
document.getElementById('ctr').addEventListener('click',()=>{
  panAng=90;tiltAng=90;panAnc=90;tiltAnc=90;moverKnob(0,0);tx('g90,90');
});

// ---- Carrinho: segura pra andar, solta pra parar (heartbeat + dead-man) ----
let hb=null;
function parar(){if(hb){clearInterval(hb);hb=null;}tx('p');}
document.querySelectorAll('.pad button').forEach(b=>{
  const m=b.dataset.m;
  const press=e=>{e.preventDefault();if(m==='p'){parar();return;}tx(m);if(hb)clearInterval(hb);hb=setInterval(()=>tx(m),150);};
  const release=e=>{if(e)e.preventDefault();parar();};
  b.addEventListener('touchstart',press,{passive:false});
  b.addEventListener('touchend',release);
  b.addEventListener('touchcancel',release);
  b.addEventListener('mousedown',press);
  b.addEventListener('mouseup',release);
  b.addEventListener('mouseleave',release);
});
document.addEventListener('visibilitychange',()=>{if(document.hidden)parar();});
</script></body></html>
)HTML";

// ---------- Acoes (chamadas pelo WebSocket) ----------
void doMove(char c){
  switch(c){
    case 'f': comandado=FRENTE;   ultimoComando=millis(); break;
    case 't': comandado=TRAS;     ultimoComando=millis(); break;
    case 'e': comandado=ESQUERDA; ultimoComando=millis(); break;
    case 'd': comandado=DIREITA;  ultimoComando=millis(); break;
    case 'p': comandado=PARADO; break;
  }
}
void doSpeed(int pct){ pct=constrain(pct,0,100); intervaloCruzeiro=map(pct,0,100,INTERVALO_MAX,INTERVALO_MIN); }
void doGimbal(int p,int t){
  int pf = PAN_INVERTE ?180-p:p;              // angulo fisico
  int tf = TILT_INVERTE?180-t:t;
  panAng  = constrain(pf, PAN_MIN,  PAN_MAX); // trava dentro do limite seguro
  tiltAng = constrain(tf, TILT_MIN, TILT_MAX);
  servoPan.write(panAng);
  servoTilt.write(tiltAng);
}
void aplicarLeds(){
  CRGB cor=ledLigado?LED_COR:CRGB::Black;
  fill_solid(leds1,NUM_LEDS,cor);
  fill_solid(leds2,NUM_LEDS,cor);
  FastLED.show();
}
void doLed(bool on){ ledLigado=on; aplicarLeds(); }

void onWsEvent(uint8_t num, WStype_t type, uint8_t* payload, size_t len){
  if(type!=WStype_TEXT || len<1) return;
  char* c=(char*)payload;                 // a lib termina em '\0'
  switch(c[0]){
    case 'f': case 't': case 'e': case 'd': case 'p': doMove(c[0]); break;
    case 'v': doSpeed(atoi(c+1)); break;
    case 'l': doLed(c[1]=='1'); break;
    case 'g': { int p=90,t=90; sscanf(c+1,"%d,%d",&p,&t); doGimbal(p,t); } break;
  }
}

void handleRaiz(){ server.send_P(200,"text/html",PAGINA); }

// ---------- Motores ----------
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

  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID,AP_PASS);
  server.on("/",handleRaiz);
  server.begin();
  webSocket.begin();
  webSocket.onEvent(onWsEvent);
}

void loop(){
  webSocket.loop();
  server.handleClient();

  // Reenvia o estado do LED a cada 1s: fita desconectada e reconectada reacende sozinha.
  static unsigned long ultimoLed=0;
  if(millis()-ultimoLed>1000){ ultimoLed=millis(); aplicarLeds(); }

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
