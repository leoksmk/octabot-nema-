/*
 * OCTA ESP-NOW VR - RECEPTOR (Octabot)
 * Igual ao codigo principal do Octabot, MAS o gimbal agora e movido pelo
 * giroscopio do controle via ESP-NOW (sai o joystick da tela).
 * Carrinho, velocidade e LEDs continuam iguais, controlados pelo celular.
 *
 * Placa: ESP32 DevKit v1 + 2 DRV8825 + 2 servos (gimbal) + 2 fitas WS2812B.
 * BIBLIOTECAS: ESP32Servo (Kevin Harrington), FastLED. (ESP-NOW ja vem no core ESP32.)
 *
 * >>> AO LIGAR, abra o Serial Monitor (115200): ele imprime o MAC desta placa.
 *     Copie esse MAC para o RECEIVER_MAC do transmissor (ESP32-C3). <<<
 *
 * Uso do carrinho: WiFi "Octabot" (senha 12345678) -> http://192.168.4.1
 */

#include <WiFi.h>
#include <WebServer.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <ESP32Servo.h>
#include <FastLED.h>

const char* AP_SSID = "Octabot";
const char* AP_PASS = "12345678";
#define WIFI_CANAL 1          // canal do SoftAP; o transmissor usa o MESMO
WebServer server(80);

// Motores (DRV8825)
const int L_STEP = 25, L_DIR = 33, L_EN = 26;   // esquerdo (DRV3 / X3)
const int R_STEP = 27, R_DIR = 14, R_EN = 12;   // direito  (DRV1 / X1)

// Servos (gimbal) - agora movidos pelo giroscopio via ESP-NOW
const int SERVO_PAN_PIN  = 15;  // U3
const int SERVO_TILT_PIN = 2;   // U1
const bool PAN_INVERTE  = false; // direcao e calibrada no transmissor; aqui fica neutro
const bool TILT_INVERTE = false;
const int PAN_MIN  = 10,  PAN_MAX  = 170;   // limites fisicos - CALIBRE pro seu gimbal
const int TILT_MIN = 30,  TILT_MAX = 150;
Servo servoPan, servoTilt;
int panAng = 90, tiltAng = 90;

// Fitas LED WS2812B (FastLED)
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

const unsigned long TIMEOUT_MS = 500;
unsigned long ultimoComando = 0;

// Pacote recebido do controle (giroscopio)
typedef struct { int pan; int tilt; } Pacote;

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
  .sub{font-size:.8rem;opacity:.6;margin:0;text-align:center}
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
    <button class="ctr" id="led">LED: Ligado</button>
    <p class="sub">Gimbal: controlado pelo<br>giroscopio (ESP-NOW)</p>
  </div>
</div>
<script>
// Controle do carrinho/velocidade/LED continua por requisicao HTTP simples.
function send(m){fetch('/cmd?m='+m).catch(()=>{});}

const sl=document.getElementById('s');
sl.addEventListener('input',()=>{
  document.getElementById('v').textContent=sl.value;
  fetch('/cmd?m=v&val='+sl.value).catch(()=>{});
});

let ledOn=true;
const btLed=document.getElementById('led');
btLed.addEventListener('click',()=>{
  ledOn=!ledOn;
  btLed.textContent='LED: '+(ledOn?'Ligado':'Desligado');
  btLed.style.background=ledOn?'#2a2a2a':'#4a1111';
  fetch('/cmd?m='+(ledOn?'l1':'l0')).catch(()=>{});
});
document.getElementById('st').textContent='';

let hb=null;
function parar(){if(hb){clearInterval(hb);hb=null;}send('p');}
document.querySelectorAll('.pad button').forEach(b=>{
  const m=b.dataset.m;
  const press=e=>{e.preventDefault();if(m==='p'){parar();return;}send(m);if(hb)clearInterval(hb);hb=setInterval(()=>send(m),150);};
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

// ---------- Acoes ----------
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
  int pf = PAN_INVERTE ?180-p:p;
  int tf = TILT_INVERTE?180-t:t;
  panAng  = constrain(pf, PAN_MIN,  PAN_MAX);  // trava no limite seguro
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

// ---------- ESP-NOW: recebe os angulos do giroscopio ----------
void onRecv(const esp_now_recv_info_t* info, const uint8_t* data, int len){
  if(len == (int)sizeof(Pacote)){
    Pacote p; memcpy(&p, data, sizeof(p));
    doGimbal(p.pan, p.tilt);
  }
}

// ---------- Web (apenas carrinho/velocidade/LED) ----------
void handleRaiz(){ server.send_P(200,"text/html",PAGINA); }
void handleCmd(){
  String m=server.arg("m");
  if      (m=="v") doSpeed(server.arg("val").toInt());
  else if (m=="l1") doLed(true);
  else if (m=="l0") doLed(false);
  else if (m.length()==1) doMove(m[0]);
  server.send(200,"text/plain","");
}

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

  // SoftAP (carrinho) no canal fixo + ESP-NOW (gimbal) no mesmo canal
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASS, WIFI_CANAL);
  Serial.println();
  Serial.print("MAC desta placa (use no transmissor): ");
  Serial.println(WiFi.softAPmacAddress());   // <-- copie este MAC pro ESP32-C3

  if(esp_now_init()==ESP_OK){
    esp_now_register_recv_cb(onRecv);
  } else {
    Serial.println("ESP-NOW falhou ao iniciar!");
  }

  server.on("/",handleRaiz);
  server.on("/cmd",handleCmd);
  server.begin();
}

void loop(){
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
