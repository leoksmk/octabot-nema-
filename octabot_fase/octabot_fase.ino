/*
 * Octabot 2x NEMA 17 - COM SINCRONIA DE FASE DAS PERNAS
 * Mainboard Future Makers 2k26: ESP32 DevKit v1 (30 pinos) + 2 drivers DRV8825.
 *
 * Baseado em ../dois_nema17_esp32.ino (mesma pinagem, mesmo app, mesmo gimbal).
 * O que muda: o firmware passa a SABER em que angulo esta a manivela de cada lado
 * e mantem a defasagem entre o lado esquerdo e o direito.
 *
 * CINEMATICA (resumo - detalhes no README desta pasta):
 *   - Cada lado e um mecanismo de 1 grau de liberdade: todas as pernas daquele lado
 *     dependem de UM angulo, o da manivela (fase). Estado do robo = (faseL, faseR).
 *   - Pinhao do motor 6 dentes -> coroa 20 dentes: 1 volta da manivela = 200*20/6 =
 *     666,67 passos (NAO inteiro). Por isso contamos em "unidades": 1 volta = 200*20 =
 *     4000 unidades e 1 passo do motor = 6 unidades (0,54 graus). Conta exata, sem deriva.
 *   - Frente/tras: os dois lados avancam juntos -> defasagem constante.
 *   - Giro no eixo: um lado avanca N passos e o outro volta N -> defasagem muda 2N.
 *     Ao terminar o giro, o firmware avanca so o lado atrasado ate a defasagem voltar
 *     ao alvo (SINCRONIA AUTOMATICA).
 *   - A fase fica salva na flash ao parar, entao sobrevive a desligar o robo
 *     (desde que ninguem gire as pernas na mao com ele desligado).
 *   - Sensor opcional por lado (hall/optico): corrige passos perdidos a cada volta.
 *
 * COMO USAR: igual ao original (rede "Carrinho-NEMA", senha "12345678", http://192.168.4.1).
 *   Na primeira vez: use os botoes de ajuste para deixar as pernas dos DOIS lados
 *   na MESMA posicao e aperte "Zerar". O firmware aplica a defasagem sozinho.
 */

#include <WiFi.h>
#include <WebServer.h>
#include <ESP32Servo.h>   // Library Manager: "ESP32Servo" (Kevin Harrington)
#include <Preferences.h>  // nativa do core ESP32 (grava na flash / NVS)

// ---------- WiFi (o ESP32 cria a propria rede) ----------
const char* AP_SSID = "Carrinho-NEMA";
const char* AP_PASS = "12345678";   // minimo 8 caracteres
WebServer server(80);
Preferences prefs;

// ---------- Pinos (conforme esquematico Mainboard Future Makers 2k26) ----------
// Motor ESQUERDO  = driver DRV3, conector de motor X3
const int L_STEP = 25;
const int L_DIR  = 33;
const int L_EN   = 26;   // LOW = driver ligado
// Motor DIREITO   = driver DRV1, conector de motor X1
const int R_STEP = 27;
const int R_DIR  = 14;
const int R_EN   = 12;   // LOW = driver ligado -- GPIO12 e strapping, ver README principal

// ---------- Servos (gimbal pan/tilt) ----------
const int SERVO_PAN_PIN  = 15;  // U3
const int SERVO_TILT_PIN = 4;   // U4
const bool PAN_INVERTE  = false;
const bool TILT_INVERTE = false;
Servo servoPan, servoTilt;
int panAng = 90, tiltAng = 90;

// ---------- Ajuste de sentido ----------
// Se uma roda girar ao contrario, troque true<->false na dela.
const bool L_INVERTE = false;
const bool R_INVERTE = true;

// ---------- Velocidade e rampa (meio periodo do STEP em us; MENOR = mais rapido) ----------
const unsigned long INTERVALO_LENTO  = 1200; // largada / parada
const unsigned long INTERVALO_MIN    = 150;  // teto de velocidade
const unsigned long INTERVALO_MAX    = 2500; // piso de velocidade
const unsigned long RAMPA_POR_PASSO  = 15;
const unsigned long INTERVALO_AJUSTE = 1200; // velocidade da sincronia/ajuste (lento, sem rampa)

unsigned long intervaloCruzeiro = 1325; // = 50% do slider
unsigned long intervaloAtual    = INTERVALO_LENTO;

// ---------- Transmissao e fase ----------
const long PASSOS_MOTOR  = 200; // passo cheio (M0/M1/M2 livres)
const long DENTES_PINHAO = 6;   // engrenagem no eixo do motor
const long DENTES_COROA  = 20;  // engrenagem da manivela
const long UNID_VOLTA = PASSOS_MOTOR * DENTES_COROA; // 4000 unidades = 1 volta da manivela
const long UNID_PASSO = DENTES_PINHAO;               // 6 unidades = 1 passo do motor (0,54 graus)

// A marcha se repete a cada quantos graus de manivela? 360 = so volta completa conta.
// (Se as pernas de um lado fossem todas iguais e a 90 graus, poderia ser 90.)
const long PERIODO_MARCHA_GRAUS = 360;
const long UNID_PERIODO = PERIODO_MARCHA_GRAUS * UNID_VOLTA / 360;

// Defasagem alvo: quanto o lado DIREITO esta a frente do ESQUERDO, em graus de manivela.
// -90 = direito 90 graus atras. Ajustavel pelo app (fica salvo).
int defasagemGraus = -90;

// Sincroniza sozinho ao terminar um giro no eixo.
const bool AUTO_SINCRONIZAR = true;

// Passos por toque nos botoes de ajuste fino (9 passos ~= 4,9 graus de manivela).
const long JOG_PASSOS = 9;

// Fase de cada manivela, em unidades (0..UNID_VOLTA-1). Cresce quando o lado anda PRA FRENTE.
long faseL = 0, faseR = 0;
long salvoL = -1, salvoR = -1;
bool calibrado = false;
int8_t sentidoL = 0, sentidoR = 0; // +1 frente, -1 tras, 0 parado (para contar a fase)

// ---------- Sensor de referencia (OPCIONAL) ----------
// Hall (ex.: A3144) ou optico, saida ativa em LOW, 1 ima/marca por manivela.
// -1 = sem sensor. Sugestao: GPIO34/35 (so entrada, sem strapping) se acessiveis na placa.
const int SENSOR_L_PIN = -1;
const int SENSOR_R_PIN = -1;
long refL = -1, refR = -1;       // fase em que a marca e vista (aprendida sozinha)
bool sensorAntL = false, sensorAntR = false;

// ---------- Estado ----------
enum Movimento { PARADO, FRENTE, TRAS, ESQUERDA, DIREITA, AJUSTE };
Movimento estado    = PARADO;
Movimento comandado = PARADO;   // so recebe PARADO..DIREITA

unsigned long ultimoPasso = 0;
bool nivelPasso = false;

// Ajuste = mover SO UM lado N passos (usado pela sincronia e pelos botoes de ajuste fino).
bool ajusteEsq = true;
long ajusteRestantes = 0;
bool pedidoSync = false;
bool abortarAjuste = false;
long pedidoJogL = 0, pedidoJogR = 0;   // passos com sinal

// ---------- Seguranca (dead-man switch) ----------
const unsigned long TIMEOUT_MS = 500;
unsigned long ultimoComando = 0;

// ---------- Pagina de controle (o "app") ----------
const char PAGINA[] PROGMEM = R"HTML(
<!DOCTYPE html><html lang="pt-br"><head>
<meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1,maximum-scale=1,user-scalable=no">
<title>Octabot</title>
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
  .fase{font-size:.85rem;font-variant-numeric:tabular-nums;text-align:center;line-height:1.5;
        width:min(86vw,320px)}
  .ok{color:#4ade80}.ruim{color:#f87171}
  .jog{display:grid;grid-template-columns:repeat(4,1fr);gap:8px;width:min(86vw,320px)}
  .jog button,.acoes button{font-size:.85rem;padding:10px 4px;border-radius:12px}
  .acoes{display:flex;gap:8px;width:min(86vw,320px)}
  .acoes button{flex:1}
  .df{display:flex;gap:8px;align-items:center;font-size:.85rem}
  .df input{width:70px;padding:8px;border-radius:8px;border:1px solid #333;background:#1a1a1a;color:#eee}
</style></head><body>
<h1>Octabot NEMA17</h1>
<div class="pad">
  <button class="up"    data-m="f">&#9650;</button>
  <button class="left"  data-m="e">&#9664;</button>
  <button class="stop"  data-m="x">STOP</button>
  <button class="right" data-m="d">&#9654;</button>
  <button class="down"  data-m="t">&#9660;</button>
</div>
<div class="vel">
  <label>Velocidade: <span id="v">50</span>%</label>
  <input id="s" type="range" min="0" max="100" value="50">
</div>
<hr>
<p class="sub">Sincronia das pernas (fase das manivelas)</p>
<div class="fase" id="fase">...</div>
<div class="acoes">
  <button id="sync">Sincronizar</button>
  <button id="zero">Zerar (lados iguais)</button>
</div>
<div class="jog">
  <button data-j="jl" data-n="-1">Esq &minus;</button>
  <button data-j="jl" data-n="1">Esq +</button>
  <button data-j="jr" data-n="-1">Dir &minus;</button>
  <button data-j="jr" data-n="1">Dir +</button>
</div>
<div class="df">Defasagem (dir - esq):
  <input id="dfv" type="number" min="-180" max="180" step="15"> &deg;
  <button class="ctr" id="dfok">Aplicar</button>
</div>
<hr>
<p class="sub">Gimbal (pan / tilt)</p>
<div class="joy" id="joy"><div class="knob" id="knob"></div></div>
<button class="ctr" id="ctr">Centralizar</button>
<script>
function send(m){fetch('/cmd?m='+m);}
const sl=document.getElementById('s');
sl.addEventListener('input',()=>{
  document.getElementById('v').textContent=sl.value;
  fetch('/cmd?m=v&val='+sl.value);
});

// ---- Sincronia / fase ----
const NOMES=['parado','frente','tras','giro esq','giro dir','ajustando'];
let dfCarregado=false;
function atualizar(){
  fetch('/estado').then(r=>r.json()).then(s=>{
    const bom=Math.abs(s.e)<3;
    document.getElementById('fase').innerHTML=
      'Esq '+s.l.toFixed(1)+'&deg; &nbsp; Dir '+s.r.toFixed(1)+'&deg;<br>'+
      'Defasagem '+s.d.toFixed(1)+'&deg; (alvo '+s.a+'&deg;) &nbsp; '+
      '<span class="'+(bom?'ok':'ruim')+'">erro '+s.e.toFixed(1)+'&deg;</span><br>'+
      '<span style="opacity:.6">'+NOMES[s.s]+(s.c?'':' &middot; NAO calibrado: use Zerar')+'</span>';
    if(!dfCarregado){document.getElementById('dfv').value=s.a;dfCarregado=true;}
  }).catch(()=>{});
}
setInterval(atualizar,400); atualizar();
document.getElementById('sync').addEventListener('click',()=>send('sync'));
document.getElementById('zero').addEventListener('click',()=>{
  if(confirm('As pernas dos DOIS lados estao na MESMA posicao?'))send('zero');
});
document.querySelectorAll('.jog button').forEach(b=>{
  b.addEventListener('click',()=>fetch('/cmd?m='+b.dataset.j+'&n='+b.dataset.n));
});
document.getElementById('dfok').addEventListener('click',()=>{
  fetch('/cmd?m=df&val='+document.getElementById('dfv').value);
});

// ---- Joystick do gimbal (posicao absoluta; o servo segura onde soltar) ----
const joy=document.getElementById('joy'), knob=document.getElementById('knob');
let jr=0, arrastando=false, ultimoEnvio=0;
function moverKnob(nx,ny){
  knob.style.left=(50+nx*50)+'%';
  knob.style.top =(50+ny*50)+'%';
}
function enviarGimbal(nx,ny){
  const agora=Date.now();
  if(agora-ultimoEnvio<60) return;
  ultimoEnvio=agora;
  const pan =Math.round((nx+1)*90);
  const tilt=Math.round((1-ny)*90);
  fetch('/gimbal?p='+pan+'&t='+tilt);
}
function pos(e){
  const r=joy.getBoundingClientRect();
  jr=r.width/2;
  let dx=(e.clientX-(r.left+jr))/jr;
  let dy=(e.clientY-(r.top +jr))/jr;
  const d=Math.hypot(dx,dy);
  if(d>1){dx/=d;dy/=d;}
  moverKnob(dx,dy); enviarGimbal(dx,dy);
}
joy.addEventListener('pointerdown',e=>{arrastando=true;joy.setPointerCapture(e.pointerId);pos(e);});
joy.addEventListener('pointermove',e=>{if(arrastando)pos(e);});
joy.addEventListener('pointerup',()=>{arrastando=false;});
document.getElementById('ctr').addEventListener('click',()=>{
  moverKnob(0,0); ultimoEnvio=0; fetch('/gimbal?p=90&t=90');
});
// Segurar = anda; soltar = para (envia 'p', que NAO cancela a sincronia automatica).
// STOP envia 'x': para tudo, inclusive a sincronia em andamento.
let hb=null;
function soltar(){if(hb){clearInterval(hb);hb=null;}send('p');}
document.querySelectorAll('.pad button').forEach(b=>{
  const m=b.dataset.m;
  const press=e=>{e.preventDefault();
                  if(m==='x'){if(hb){clearInterval(hb);hb=null;}send('x');return;}
                  send(m);if(hb)clearInterval(hb);hb=setInterval(()=>send(m),150);};
  const release=e=>{if(e)e.preventDefault();if(m!=='x')soltar();};
  b.addEventListener('touchstart',press,{passive:false});
  b.addEventListener('touchend',release);
  b.addEventListener('touchcancel',release);
  b.addEventListener('mousedown',press);
  b.addEventListener('mouseup',release);
  b.addEventListener('mouseleave',release);
});
document.addEventListener('visibilitychange',()=>{if(document.hidden)soltar();});
</script></body></html>
)HTML";

// ---------- Matematica de fase ----------
long normaliza(long x, long m) { x %= m; if (x < 0) x += m; return x; }       // 0..m-1
long centraliza(long x, long m) { x = normaliza(x, m); return x >= m / 2 ? x - m : x; } // -m/2..m/2-1
float emGraus(long unid) { return unid * 360.0f / UNID_VOLTA; }
long alvoUnid() { return (long)defasagemGraus * UNID_VOLTA / 360; }

// Erro da defasagem: > 0 = direito adiantado demais; < 0 = direito atrasado demais.
long erroFase() { return centraliza(faseR - faseL - alvoUnid(), UNID_PERIODO); }

void contarPasso() {
  faseL = normaliza(faseL + sentidoL * UNID_PASSO, UNID_VOLTA);
  faseR = normaliza(faseR + sentidoR * UNID_PASSO, UNID_VOLTA);
}

// ---------- Flash (NVS) ----------
void salvarFases() {
  if (faseL == salvoL && faseR == salvoR) return;   // so grava se mudou (poupa a flash)
  prefs.putLong("fl", faseL);
  prefs.putLong("fr", faseR);
  salvoL = faseL; salvoR = faseR;
}

void salvarConfig() {
  prefs.putInt("df", defasagemGraus);
  prefs.putBool("cal", calibrado);
  prefs.putLong("rl", refL);
  prefs.putLong("rr", refR);
}

void carregar() {
  prefs.begin("octabot", false);
  defasagemGraus = prefs.getInt("df", defasagemGraus);
  calibrado = prefs.getBool("cal", false);
  refL = prefs.getLong("rl", -1);
  refR = prefs.getLong("rr", -1);
  if (prefs.isKey("fl")) {
    faseL = prefs.getLong("fl", 0);
    faseR = prefs.getLong("fr", 0);
  } else {
    // Nunca calibrado: assume que foi montado na defasagem certa (erro 0).
    faseL = 0;
    faseR = normaliza(alvoUnid(), UNID_VOLTA);
  }
  salvoL = faseL; salvoR = faseR;
}

// ---------- Sensor de referencia ----------
// Na 1a vez que a marca passa, aprende em que fase ela esta. Nas proximas, corrige a fase
// (desfaz passos perdidos). So conta andando pra frente: a borda muda com o sentido.
void lerSensor(int pin, bool &anterior, long &fase, long &ref, int8_t sentido) {
  if (pin < 0) return;
  bool ativo = digitalRead(pin) == LOW;
  if (ativo && !anterior && sentido > 0) {
    if (ref < 0) { ref = fase; salvarConfig(); }
    else fase = ref;
  }
  anterior = ativo;
}

// ---------- Web handlers ----------
void handleRaiz() { server.send_P(200, "text/html", PAGINA); }

void handleCmd() {
  String m = server.arg("m");
  if      (m == "f") { comandado = FRENTE;   ultimoComando = millis(); }
  else if (m == "t") { comandado = TRAS;     ultimoComando = millis(); }
  else if (m == "e") { comandado = ESQUERDA; ultimoComando = millis(); }
  else if (m == "d") { comandado = DIREITA;  ultimoComando = millis(); }
  else if (m == "p") comandado = PARADO;                     // soltou o botao
  else if (m == "x") {                                        // STOP: para tudo
    comandado = PARADO; pedidoSync = false; pedidoJogL = pedidoJogR = 0;
    if (estado != PARADO) abortarAjuste = true;  // cancela ajuste em curso E a sincronia pos-giro
  }
  else if (m == "v") {
    int pct = constrain(server.arg("val").toInt(), 0, 100);
    intervaloCruzeiro = map(pct, 0, 100, INTERVALO_MAX, INTERVALO_MIN);
  }
  else if (m == "sync") pedidoSync = true;
  else if (m == "jl")   pedidoJogL = server.arg("n").toInt() * JOG_PASSOS;
  else if (m == "jr")   pedidoJogR = server.arg("n").toInt() * JOG_PASSOS;
  else if (m == "zero" && estado == PARADO) {
    // Usuario alinhou os dois lados na MESMA posicao: esse passa a ser o zero.
    faseL = 0; faseR = 0;
    refL = -1; refR = -1;       // sensor reaprende a marca com o novo zero
    calibrado = true;
    salvarConfig(); salvarFases();
    pedidoSync = true;          // aplica a defasagem alvo
  }
  else if (m == "df") {
    defasagemGraus = constrain(server.arg("val").toInt(), -180, 180);
    salvarConfig();
  }
  server.send(200, "text/plain", "");
}

void handleEstado() {
  char buf[160];
  snprintf(buf, sizeof(buf),
           "{\"l\":%.1f,\"r\":%.1f,\"d\":%.1f,\"a\":%d,\"e\":%.1f,\"s\":%d,\"c\":%s}",
           emGraus(faseL), emGraus(faseR),
           emGraus(centraliza(faseR - faseL, UNID_VOLTA)),
           defasagemGraus, emGraus(erroFase()), (int)estado, calibrado ? "true" : "false");
  server.send(200, "application/json", buf);
}

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
void drivers(bool ligado) {
  digitalWrite(L_EN, ligado ? LOW : HIGH);
  digitalWrite(R_EN, ligado ? LOW : HIGH);
}

void escreverDir(bool esqFrente, bool dirFrente) {
  digitalWrite(L_DIR, (esqFrente ^ L_INVERTE) ? HIGH : LOW);
  digitalWrite(R_DIR, (dirFrente ^ R_INVERTE) ? HIGH : LOW);
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
  escreverDir(esq, dir);
  sentidoL = esq ? 1 : -1;
  sentidoR = dir ? 1 : -1;
}

// Baixa os dois STEP. Espera antes: o DRV8825 exige >= 1,9 us em HIGH para contar o passo,
// e o proximo movimento precisa comecar com o pino em LOW para gerar borda de subida.
void soltarStep() {
  delayMicroseconds(3);
  digitalWrite(L_STEP, LOW);
  digitalWrite(R_STEP, LOW);
  nivelPasso = false;
}

// Parou de vez: solta os pinos, desliga os drivers e grava a fase.
void repousar() {
  estado = PARADO;
  sentidoL = sentidoR = 0;
  abortarAjuste = false;
  soltarStep();
  drivers(false);
  salvarFases();
}

// Move SO um lado 'passos' passos (com sinal). O outro lado fica parado e travado.
void iniciarAjuste(bool esquerdo, long passos) {
  if (passos == 0) return;
  bool frente = passos > 0;
  ajusteEsq = esquerdo;
  ajusteRestantes = labs(passos);
  sentidoL = esquerdo ? (frente ? 1 : -1) : 0;
  sentidoR = esquerdo ? 0 : (frente ? 1 : -1);
  if (esquerdo) digitalWrite(L_DIR, (frente ^ L_INVERTE) ? HIGH : LOW);
  else          digitalWrite(R_DIR, (frente ^ R_INVERTE) ? HIGH : LOW);
  drivers(true);
  estado = AJUSTE;
  nivelPasso = false;
  ultimoPasso = micros();
}

// Sincronia: avanca (sempre pra frente) o lado que esta atrasado. Corrige no maximo
// meio periodo da marcha. Retorna false se ja estava sincronizado.
bool iniciarSincronia() {
  long e = erroFase();
  long passos = (labs(e) + UNID_PASSO / 2) / UNID_PASSO;
  if (passos == 0) return false;
  if (e > 0) iniciarAjuste(true, passos);    // direito adiantado -> esquerdo avanca
  else       iniciarAjuste(false, passos);   // direito atrasado  -> direito avanca
  return true;
}

void passoAjuste() {
  if (abortarAjuste || ajusteRestantes <= 0) {
    abortarAjuste = false;
    ajusteRestantes = 0;
    if (comandado == PARADO) repousar();
    else { estado = PARADO; sentidoL = sentidoR = 0; soltarStep(); } // o loop emenda no comando
    return;
  }
  unsigned long agora = micros();
  if (agora - ultimoPasso >= INTERVALO_AJUSTE) {
    ultimoPasso = agora;
    nivelPasso = !nivelPasso;
    digitalWrite(ajusteEsq ? L_STEP : R_STEP, nivelPasso ? HIGH : LOW);
    if (nivelPasso) { contarPasso(); ajusteRestantes--; }
  }
}

void setup() {
  pinMode(L_STEP, OUTPUT); pinMode(L_DIR, OUTPUT);
  pinMode(R_STEP, OUTPUT); pinMode(R_DIR, OUTPUT);
  pinMode(L_EN, OUTPUT);   pinMode(R_EN, OUTPUT);
  drivers(false);

  // GPIO34-39 nao tem pull-up interno: use resistor de 10k para 3V3 neles.
  if (SENSOR_L_PIN >= 0) pinMode(SENSOR_L_PIN, SENSOR_L_PIN >= 34 ? INPUT : INPUT_PULLUP);
  if (SENSOR_R_PIN >= 0) pinMode(SENSOR_R_PIN, SENSOR_R_PIN >= 34 ? INPUT : INPUT_PULLUP);

  carregar();

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
  server.on("/estado", handleEstado);
  server.on("/gimbal", handleGimbal);
  server.begin();
}

void loop() {
  server.handleClient();
  lerSensor(SENSOR_L_PIN, sensorAntL, faseL, refL, sentidoL);
  lerSensor(SENSOR_R_PIN, sensorAntR, faseR, refR, sentidoR);

  // Dead-man switch: sem comando de movimento recente, para sozinho.
  if (comandado != PARADO && millis() - ultimoComando > TIMEOUT_MS) {
    comandado = PARADO;
  }

  if (estado == AJUSTE) { passoAjuste(); return; }

  if (estado == PARADO) {
    if (comandado != PARADO) {
      drivers(true);
      estado = comandado;
      aplicarSentido(estado);
      intervaloAtual = INTERVALO_LENTO;
    } else if (pedidoSync) {
      pedidoSync = false;
      iniciarSincronia();
    } else if (pedidoJogL) {
      iniciarAjuste(true, pedidoJogL);  pedidoJogL = 0;
    } else if (pedidoJogR) {
      iniciarAjuste(false, pedidoJogR); pedidoJogR = 0;
    }
    if (estado != FRENTE && estado != TRAS && estado != ESQUERDA && estado != DIREITA) return;
  }

  bool freando = (comandado == PARADO) || (comandado != estado);
  unsigned long alvo = freando ? INTERVALO_LENTO : intervaloCruzeiro;

  unsigned long agora = micros();
  if (agora - ultimoPasso >= intervaloAtual) {
    ultimoPasso = agora;
    nivelPasso = !nivelPasso;
    int nivel = nivelPasso ? HIGH : LOW;
    digitalWrite(L_STEP, nivel);
    digitalWrite(R_STEP, nivel);

    if (nivelPasso) { // borda de subida = 1 passo nos dois motores
      contarPasso();
      if (intervaloAtual > alvo) {
        intervaloAtual -= RAMPA_POR_PASSO;
        if (intervaloAtual < alvo) intervaloAtual = alvo;
      } else if (intervaloAtual < alvo) {
        intervaloAtual += RAMPA_POR_PASSO;
        if (intervaloAtual > alvo) intervaloAtual = alvo;
      }
      if (freando && intervaloAtual >= INTERVALO_LENTO) {
        bool eraGiro  = (estado == ESQUERDA || estado == DIREITA);
        bool vaiGirar = (comandado == ESQUERDA || comandado == DIREITA);
        soltarStep();
        if (eraGiro && !vaiGirar && AUTO_SINCRONIZAR && !abortarAjuste && iniciarSincronia()) {
          // estado = AJUSTE; ao terminar, volta a PARADO e emenda no comando atual
        } else if (comandado == PARADO) {
          repousar();
        } else {
          estado = comandado;
          aplicarSentido(estado);
        }
      }
    }
  }
}
