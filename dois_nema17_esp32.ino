/*
 * Carrinho 2x NEMA 17 (tracao diferencial) - ESP32 + 2 A4988 (PCB vermelha).
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
 * Ligacao do A4988 (cada driver):
 *   VMOT -> 12V + capacitor 100uF entre VMOT e GND
 *   GND  -> GND da fonte E GND do ESP32 (terra comum obrigatorio!)
 *   VDD  -> 3.3V do ESP32 | STEP/DIR -> pinos abaixo | EN -> LOW liga
 *   RESET ligado no SLEEP; MS1/MS2/MS3 livres = passo cheio
 * Ajuste a corrente (Vref) no potenciometro antes de energizar.
 */

#include <WiFi.h>
#include <WebServer.h>

// ---------- WiFi (o ESP32 cria a propria rede) ----------
const char* AP_SSID = "Carrinho-NEMA";
const char* AP_PASS = "12345678";   // minimo 8 caracteres
WebServer server(80);

// ---------- Pinos ----------
const int L_STEP = 26, L_DIR = 27;   // motor ESQUERDO
const int R_STEP = 32, R_DIR = 33;   // motor DIREITO
const int EN_PIN = 25;               // enable compartilhado (LOW = ligado)

// ---------- Ajuste de sentido ----------
// Se uma roda girar ao contrario, troque true<->false na dela.
const bool L_INVERTE = false;
const bool R_INVERTE = true;

// ---------- Velocidade e rampa (intervalo entre passos em us; MENOR = mais rapido) ----------
const unsigned long INTERVALO_LENTO = 2000; // largada / parada
const unsigned long INTERVALO_MIN   = 350;  // teto de velocidade
const unsigned long INTERVALO_MAX   = 2500; // piso de velocidade
const unsigned long RAMPA_POR_PASSO = 8;    // suavidade da rampa

unsigned long intervaloCruzeiro = 500;
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
       display:flex;flex-direction:column;align-items:center;justify-content:center;min-height:100vh;gap:18px}
  h1{font-size:1.1rem;font-weight:600;margin:0;opacity:.8}
  .pad{display:grid;grid-template-columns:repeat(3,86px);grid-template-rows:repeat(3,86px);gap:12px}
  button{border:0;border-radius:16px;background:#2a2a2a;color:#eee;font-size:1.6rem;
         touch-action:none;transition:background .05s}
  button:active{background:#3b82f6}
  .up{grid-area:1/2}.left{grid-area:2/1}.stop{grid-area:2/2;background:#7f1d1d;font-size:1rem}
  .right{grid-area:2/3}.down{grid-area:3/2}
  .vel{display:flex;align-items:center;gap:14px}
  .vel button{width:64px;height:56px;font-size:1.4rem;border-radius:12px}
  #v{min-width:120px;text-align:center;font-variant-numeric:tabular-nums;opacity:.85}
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
  <button onclick="send('-')">&minus;</button>
  <span id="v">velocidade</span>
  <button onclick="send('+')">&plus;</button>
</div>
<script>
function send(m){fetch('/cmd?m='+m).then(r=>r.text()).then(t=>{if(t)document.getElementById('v').textContent=t;});}
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
  String resp = "";
  if      (m == "f") { comandado = FRENTE;   ultimoComando = millis(); }
  else if (m == "t") { comandado = TRAS;     ultimoComando = millis(); }
  else if (m == "e") { comandado = ESQUERDA; ultimoComando = millis(); }
  else if (m == "d") { comandado = DIREITA;  ultimoComando = millis(); }
  else if (m == "p") comandado = PARADO;
  else if (m == "+") { if (intervaloCruzeiro > INTERVALO_MIN) intervaloCruzeiro -= 50; }
  else if (m == "-") { if (intervaloCruzeiro < INTERVALO_MAX) intervaloCruzeiro += 50; }

  if (m == "+" || m == "-") {
    // devolve um valor legivel (0-100%) so pra mostrar na tela
    int pct = map(intervaloCruzeiro, INTERVALO_MAX, INTERVALO_MIN, 0, 100);
    resp = "vel: " + String(pct) + "%";
  }
  server.send(200, "text/plain", resp);
}

// ---------- Motores ----------
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
  pinMode(EN_PIN, OUTPUT);
  digitalWrite(EN_PIN, LOW);

  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASS);

  server.on("/", handleRaiz);
  server.on("/cmd", handleCmd);
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
        if (comandado == PARADO) estado = PARADO;
        else { estado = comandado; aplicarSentido(estado); }
      }
    }
  }
}
