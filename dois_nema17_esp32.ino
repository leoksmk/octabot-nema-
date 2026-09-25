/*
 * Carrinho 2x NEMA 17 (tracao diferencial) - Mainboard Future Makers 2k26
 * ESP32 DevKit v1 (30 pinos) + 2 drivers DRV8825.
 * Controle SEM FIO por uma pagina web servida pelo proprio ESP32 (o "app").
 * Movimento nao-bloqueante com RAMPA de aceleracao/desaceleracao. Curva estilo TANK.
 *
 * COMO USAR:
 *   1. Grave este codigo no ESP32.
 *   2. No celular, conecte no WiFi:  rede "Carrinho-NEMA"  senha "12345678"
 *   3. Abra o navegador em:  http://192.168.4.1
 *   4. Segure os botoes pra andar; solte pra parar. +/- muda a velocidade.
 *      (No celular: menu do navegador > "Adicionar a tela inicial" = vira icone de app)
 *
 * Esta placa (ver esquematico) ja resolve por hardware:
 *   - VMOT de cada driver -> 12V, com capacitor (C3/C4)
 *   - SLEEP e RESET -> VCC (drivers sempre ativos)
 *   - M0/M1/M2 livres -> passo cheio (200 passos/volta)
 *   - Motores nos conectores X3 (DRV3) e X1 (DRV1); alimentacao em J1(5V) e J2(12V)
 * Ajuste a corrente (Vref) no potenciometro de cada DRV8825 antes de energizar
 *   (DRV8825: corrente_limite ~= Vref x 2 com Rsense 0.1ohm; ex.: 1A -> Vref ~0.5V).
 */

#include <WiFi.h>
#include <WebServer.h>
#include <ESP32Servo.h>   // instale pelo Library Manager: "ESP32Servo" (Kevin Harrington)

// ---------- WiFi (o ESP32 cria a propria rede) ----------
const char* AP_SSID = "Carrinho-NEMA";
const char* AP_PASS = "12345678";   // minimo 8 caracteres
WebServer server(80);

// ---------- Pinos (conforme esquematico Mainboard Future Makers 2k26) ----------
// Motor ESQUERDO  = driver DRV3, conector de motor X3
const int L_STEP = 25;   // PIN25
const int L_DIR  = 33;   // PIN33
const int L_EN   = 26;   // PIN26  (LOW = driver ligado)
// Motor DIREITO   = driver DRV1, conector de motor X1
const int R_STEP = 27;   // PIN27
const int R_DIR  = 14;   // PIN14
const int R_EN   = 12;   // PIN12  (LOW = driver ligado) -- GPIO12 e strapping, ver nota no README
// Obs.: nesta placa cada driver tem seu proprio EN (nao e compartilhado).
// SLEEP e RESET ja vao no VCC por hardware; M0/M1/M2 livres = passo cheio (200 passos/volta).

// ---------- Servos (gimbal pan/tilt) ----------
// Conectores de servo da placa: U3=GPIO15, U1=GPIO2, U4=GPIO4.
// Usamos U3 e U4 (mais seguros no boot; U1/GPIO2 evitado por ser strapping delicado).
const int SERVO_PAN_PIN  = 15;  // U3  -> movimento horizontal
const int SERVO_TILT_PIN = 4;   // U4  -> movimento vertical
const bool PAN_INVERTE  = false; // troque se o eixo girar ao contrario
const bool TILT_INVERTE = false;
Servo servoPan, servoTilt;
int panAng = 90, tiltAng = 90;   // posicao atual (o gimbal segura onde parar)

// ---------- Ajuste de sentido ----------
// Se uma roda girar ao contrario, troque true<->false na dela.
const bool L_INVERTE = false;
const bool R_INVERTE = true;

// ---------- Velocidade e rampa (intervalo entre passos em us; MENOR = mais rapido) ----------
const unsigned long INTERVALO_LENTO = 1500; // largada / parada
const unsigned long INTERVALO_MIN   = 200;  // teto de velocidade (menor = mais rapido; se travar/chiar, suba p/ 250-300)
const unsigned long INTERVALO_MAX   = 2500; // piso de velocidade
const unsigned long RAMPA_POR_PASSO = 15;   // acelera mais rapido (maior = chega na velocidade antes)

unsigned long intervaloCruzeiro = 1350; // = 50% do slider (map 0-100% -> MAX..MIN)
unsigned long intervaloAtual    = INTERVALO_LENTO;

// ---------- Estado ----------
enum Movimento { PARADO, FRENTE, TRAS, ESQUERDA, DIREITA };
Movimento estado    = PARADO;
Movimento comandado = PARADO;

unsigned long ultimoPasso = 0;
bool nivelPasso = false;

// ---------- Seguranca (dead-man switch) ----------
// Se nao chegar comando de movimento dentro deste tempo, o carrinho para sozinho.
const unsigned long TIMEOUT_MS = 500;
unsigned long ultimoComando = 0;

// ---------- Pagina de controle (o "app") ----------
const char PAGINA[] PROGMEM = R"HTML(
<!DOCTYPE html><html lang="pt-br"><head>
<meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1,maximum-scale=1,user-scalable=no">
<title>Carrinho NEMA</title>
<style>
  :root{color-scheme:dark}
  *{box-sizing:border-box;-webkit-user-select:none;user-select:none;-webkit-tap-highlight-color:transparent}
  body{margin:0;font-family:system-ui,sans-serif;background:#111;color:#eee;
       display:flex;flex-direction:column;align-items:center;justify-content:flex-start;
       min-height:100vh;gap:16px;padding:18px 0 28px}
  h1{font-size:1.1rem;font-weight:600;margin:0;opacity:.8}
  .pad{display:grid;grid-template-columns:repeat(3,86px);grid-template-rows:repeat(3,86px);gap:12px}
  button{border:0;border-radius:16px;background:#2a2a2a;color:#eee;font-size:1.6rem;
         touch-action:none;transition:background .05s}
  button:active{background:#3b82f6}
  .up{grid-area:1/2}.left{grid-area:2/1}.stop{grid-area:2/2;background:#7f1d1d;font-size:1rem}
  .right{grid-area:2/3}.down{grid-area:3/2}
  .vel{display:flex;flex-direction:column;align-items:center;gap:8px;width:min(86vw,320px)}
  .vel label{font-size:.9rem;opacity:.85;font-variant-numeric:tabular-nums}
  input[type=range]{width:100%;height:36px;accent-color:#3b82f6;touch-action:none}
  hr{width:min(86vw,320px);border:0;border-top:1px solid #333;margin:4px 0}
  .sub{font-size:.85rem;opacity:.6;margin:0}
  .joy{position:relative;width:200px;height:200px;border-radius:50%;
       background:radial-gradient(circle,#242424 0%,#1a1a1a 70%);border:1px solid #333;touch-action:none}
  .knob{position:absolute;left:50%;top:50%;width:64px;height:64px;border-radius:50%;
        background:#3b82f6;transform:translate(-50%,-50%);transition:background .05s}
  .knob:active{background:#2563eb}
  .ctr{border:0;border-radius:12px;background:#2a2a2a;color:#eee;font-size:.9rem;padding:10px 18px}
</style></head><body>
<h1>Carrinho NEMA17</h1>
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
<hr>
<p class="sub">Gimbal (pan / tilt)</p>
<div class="joy" id="joy"><div class="knob" id="knob"></div></div>
<button class="ctr" id="ctr">Centralizar</button>
<script>
function send(m){fetch('/cmd?m='+m);}
// Slider de velocidade: envia o valor (0-100%) enquanto arrasta.
const sl=document.getElementById('s');
sl.addEventListener('input',()=>{
  document.getElementById('v').textContent=sl.value;
  fetch('/cmd?m=v&val='+sl.value);
});

// ---- Joystick do gimbal (posicao absoluta; o servo segura onde soltar) ----
const joy=document.getElementById('joy'), knob=document.getElementById('knob');
let jr=0, arrastando=false, ultimoEnvio=0;
function moverKnob(nx,ny){ // nx,ny em -1..1
  knob.style.left=(50+nx*50)+'%';
  knob.style.top =(50+ny*50)+'%';
}
function enviarGimbal(nx,ny){
  const agora=Date.now();
  if(agora-ultimoEnvio<60) return;   // limita a ~16 envios/s
  ultimoEnvio=agora;
  const pan =Math.round((nx+1)*90);  // -1..1 -> 0..180
  const tilt=Math.round((1-ny)*90);  // para cima = tilt maior
  fetch('/gimbal?p='+pan+'&t='+tilt);
}
function pos(e){
  const r=joy.getBoundingClientRect();
  jr=r.width/2;
  let dx=(e.clientX-(r.left+jr))/jr;
  let dy=(e.clientY-(r.top +jr))/jr;
  const d=Math.hypot(dx,dy);
  if(d>1){dx/=d;dy/=d;}              // prende dentro do circulo
  moverKnob(dx,dy); enviarGimbal(dx,dy);
}
joy.addEventListener('pointerdown',e=>{arrastando=true;joy.setPointerCapture(e.pointerId);pos(e);});
joy.addEventListener('pointermove',e=>{if(arrastando)pos(e);});
joy.addEventListener('pointerup',()=>{arrastando=false;}); // NAO recentraliza: gimbal segura
document.getElementById('ctr').addEventListener('click',()=>{
  moverKnob(0,0); ultimoEnvio=0; fetch('/gimbal?p=90&t=90');
});
// Segurar = anda; soltar = para. Funciona no toque e no mouse.
// Enquanto o botao esta apertado, reenvia o comando (heartbeat) a cada 150ms.
// Se soltar, ou travar, ou o WiFi cair, o ESP32 para sozinho pelo timeout.
let hb=null;
function parar(){if(hb){clearInterval(hb);hb=null;}send('p');}
document.querySelectorAll('.pad button').forEach(b=>{
  const m=b.dataset.m;
  const press=e=>{e.preventDefault();if(m==='p'){parar();return;}
                  send(m);if(hb)clearInterval(hb);hb=setInterval(()=>send(m),150);};
  const release=e=>{if(e)e.preventDefault();parar();};
  b.addEventListener('touchstart',press,{passive:false});
  b.addEventListener('touchend',release);
  b.addEventListener('touchcancel',release);
  b.addEventListener('mousedown',press);
  b.addEventListener('mouseup',release);
  b.addEventListener('mouseleave',release);
});
// Se a aba perder o foco (trocou de app, bloqueou a tela), para na hora.
document.addEventListener('visibilitychange',()=>{if(document.hidden)parar();});
</script></body></html>
)HTML";

// ---------- Web handlers ----------
void handleRaiz() { server.send_P(200, "text/html", PAGINA); }

void handleCmd() {
  String m = server.arg("m");
  if      (m == "f") { comandado = FRENTE;   ultimoComando = millis(); }
  else if (m == "t") { comandado = TRAS;     ultimoComando = millis(); }
  else if (m == "e") { comandado = ESQUERDA; ultimoComando = millis(); }
  else if (m == "d") { comandado = DIREITA;  ultimoComando = millis(); }
  else if (m == "p") comandado = PARADO;
  else if (m == "v") {
    // Slider: val = 0..100 (%). 0% = mais lento, 100% = mais rapido.
    int pct = constrain(server.arg("val").toInt(), 0, 100);
    intervaloCruzeiro = map(pct, 0, 100, INTERVALO_MAX, INTERVALO_MIN);
  }
  server.send(200, "text/plain", "");
}

// Gimbal: recebe angulos absolutos p (pan) e t (tilt), 0..180. O servo segura a posicao.
void handleGimbal() {
  if (server.hasArg("p")) {
    int a = constrain(server.arg("p").toInt(), 0, 180);
    panAng = PAN_INVERTE ? 180 - a : a;
    servoPan.write(panAng);
  }
  if (server.hasArg("t")) {
    int a = constrain(server.arg("t").toInt(), 0, 180);
    tiltAng = TILT_INVERTE ? 180 - a : a;
    servoTilt.write(tiltAng);
  }
  server.send(200, "text/plain", "");
}

// ---------- Motores ----------
// Liga/desliga os dois drivers (EN ativo em nivel baixo).
// Desligar quando parado evita que os motores fiquem puxando corrente (e esquentando) a toa.
void drivers(bool ligado) {
  digitalWrite(L_EN, ligado ? LOW : HIGH);
  digitalWrite(R_EN, ligado ? LOW : HIGH);
}

void aplicarSentido(Movimento mv) {
  bool esq, dir;
  switch (mv) {
    case FRENTE:   esq = true;  dir = true;  break;
    case TRAS:     esq = false; dir = false; break;
    case ESQUERDA: esq = false; dir = true;  break; // tank
    case DIREITA:  esq = true;  dir = false; break; // tank
    default: return;
  }
  digitalWrite(L_DIR, (esq ^ L_INVERTE) ? HIGH : LOW);
  digitalWrite(R_DIR, (dir ^ R_INVERTE) ? HIGH : LOW);
}

void setup() {
  pinMode(L_STEP, OUTPUT); pinMode(L_DIR, OUTPUT);
  pinMode(R_STEP, OUTPUT); pinMode(R_DIR, OUTPUT);
  pinMode(L_EN, OUTPUT);   pinMode(R_EN, OUTPUT);
  drivers(false);          // comeca DESLIGADO (parado nao segura corrente = menos calor)

  // Servos do gimbal (ESP32Servo usa os timers LEDC).
  ESP32PWM::allocateTimer(0);
  ESP32PWM::allocateTimer(1);
  ESP32PWM::allocateTimer(2);
  ESP32PWM::allocateTimer(3);
  servoPan.setPeriodHertz(50);
  servoTilt.setPeriodHertz(50);
  servoPan.attach(SERVO_PAN_PIN, 500, 2500);
  servoTilt.attach(SERVO_TILT_PIN, 500, 2500);
  servoPan.write(panAng);
  servoTilt.write(tiltAng);

  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASS);

  server.on("/", handleRaiz);
  server.on("/cmd", handleCmd);
  server.on("/gimbal", handleGimbal);
  server.begin();
}

void loop() {
  server.handleClient();

  // Dead-man switch: sem comando de movimento recente, para sozinho.
  if (comandado != PARADO && millis() - ultimoComando > TIMEOUT_MS) {
    comandado = PARADO;
  }

  // Troca de estado com seguranca: nao inverte em velocidade.
  if (comandado != estado) {
    if (estado == PARADO) {
      drivers(true);            // religa os drivers ao sair do repouso
      estado = comandado;
      aplicarSentido(estado);
      intervaloAtual = INTERVALO_LENTO;
    }
    // demais transicoes tratadas pela rampa abaixo (freia antes de trocar)
  }

  if (estado == PARADO) return;

  bool freando = (comandado == PARADO) || (comandado != estado);
  unsigned long alvo = freando ? INTERVALO_LENTO : intervaloCruzeiro;

  unsigned long agora = micros();
  if (agora - ultimoPasso >= intervaloAtual) {
    ultimoPasso = agora;
    nivelPasso = !nivelPasso;
    int nivel = nivelPasso ? HIGH : LOW;
    digitalWrite(L_STEP, nivel);
    digitalWrite(R_STEP, nivel);

    if (nivelPasso) { // aplica rampa uma vez por passo completo
      if (intervaloAtual > alvo) {
        intervaloAtual -= RAMPA_POR_PASSO;
        if (intervaloAtual < alvo) intervaloAtual = alvo;
      } else if (intervaloAtual < alvo) {
        intervaloAtual += RAMPA_POR_PASSO;
        if (intervaloAtual > alvo) intervaloAtual = alvo;
      }
      if (freando && intervaloAtual >= INTERVALO_LENTO) {
        if (comandado == PARADO) { estado = PARADO; drivers(false); } // desliga ao parar de vez
        else { estado = comandado; aplicarSentido(estado); }
      }
    }
  }
}
