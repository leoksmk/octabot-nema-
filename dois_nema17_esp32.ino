/*
 * Carrinho com 2 motores NEMA 17 (tracao diferencial) - ESP32 + 2 A4988 (PCB vermelha).
 * Controle por comando SERIAL, movimento continuo, nao-bloqueante e com RAMPA de
 * aceleracao/desaceleracao (comeca devagar e vai ganhando velocidade sem forcar o motor).
 * Curva estilo TANK (gira no proprio eixo).
 *
 * Comandos (Serial Monitor a 115200):
 *   f ou w  -> frente
 *   t ou s  -> tras
 *   e ou a  -> gira para a esquerda (no lugar)
 *   d       -> gira para a direita (no lugar)
 *   p ou x  -> parar (com desaceleracao)
 *   + / -   -> aumenta / diminui a velocidade de cruzeiro
 *
 * Ligacao do A4988 (cada driver):
 *   VMOT -> 12V (fonte dos motores) + capacitor 100uF entre VMOT e GND
 *   GND  -> GND da fonte E GND do ESP32 (terra comum obrigatorio!)
 *   VDD  -> 3.3V do ESP32
 *   STEP/DIR -> pinos abaixo
 *   EN   -> pino EN (LOW = ligado)
 *   RESET ligado no SLEEP; MS1/MS2/MS3 livres = passo cheio
 *   1A/1B/2A/2B -> bobinas do NEMA 17
 * Ajuste a corrente (Vref) no potenciometro antes de energizar.
 */

// ---------- Pinos ----------
// Motor ESQUERDO
const int L_STEP = 26;
const int L_DIR  = 27;
// Motor DIREITO
const int R_STEP = 32;
const int R_DIR  = 33;
// Enable compartilhado (LOW = drivers ligados)
const int EN_PIN = 25;

// ---------- Ajuste de sentido ----------
// Se uma roda girar ao contrario do esperado, troque true<->false na dela.
const bool L_INVERTE = false;
const bool R_INVERTE = true;   // motor direito costuma ficar espelhado

// ---------- Velocidade e rampa (intervalo entre passos em microssegundos) ----------
// MENOR intervalo = mais rapido.
const unsigned long INTERVALO_LENTO   = 2000; // largada / parada (bem devagar)
const unsigned long INTERVALO_MIN     = 350;  // mais rapido permitido
const unsigned long INTERVALO_MAX     = 2500; // mais lento permitido
const unsigned long RAMPA_POR_PASSO   = 8;    // quanto o intervalo muda a cada passo (suavidade)

unsigned long intervaloCruzeiro = 500;  // velocidade alvo quando andando (ajustavel com +/-)
unsigned long intervaloAtual    = INTERVALO_LENTO; // intervalo real neste instante (rampa)

// ---------- Estado ----------
enum Movimento { PARADO, FRENTE, TRAS, ESQUERDA, DIREITA };
Movimento estado    = PARADO; // o que o carrinho esta fazendo agora
Movimento comandado = PARADO; // o que o usuario pediu

unsigned long ultimoPasso = 0;
bool nivelPasso = false;   // alterna HIGH/LOW dos pinos STEP

void aplicarSentido(Movimento m) {
  bool esquerdaFrente, direitaFrente;
  switch (m) {
    case FRENTE:   esquerdaFrente = true;  direitaFrente = true;  break;
    case TRAS:     esquerdaFrente = false; direitaFrente = false; break;
    case ESQUERDA: esquerdaFrente = false; direitaFrente = true;  break; // tank: eixos opostos
    case DIREITA:  esquerdaFrente = true;  direitaFrente = false; break; // tank: eixos opostos
    default:       return;
  }
  digitalWrite(L_DIR, (esquerdaFrente ^ L_INVERTE) ? HIGH : LOW);
  digitalWrite(R_DIR, (direitaFrente ^ R_INVERTE) ? HIGH : LOW);
}

void setup() {
  Serial.begin(115200);

  pinMode(L_STEP, OUTPUT);
  pinMode(L_DIR,  OUTPUT);
  pinMode(R_STEP, OUTPUT);
  pinMode(R_DIR,  OUTPUT);
  pinMode(EN_PIN, OUTPUT);

  digitalWrite(EN_PIN, LOW);   // habilita os dois drivers

  Serial.println("Carrinho NEMA17 pronto (com rampa).");
  Serial.println("f/w frente | t/s tras | e/a esquerda | d direita | p parar | +/- velocidade");
}

void lerSerial() {
  while (Serial.available() > 0) {
    char c = Serial.read();
    switch (c) {
      case 'f': case 'w': comandado = FRENTE;   break;
      case 't': case 's': comandado = TRAS;     break;
      case 'e': case 'a': comandado = ESQUERDA; break;
      case 'd':           comandado = DIREITA;  break;
      case 'p': case 'x': comandado = PARADO;   break;
      case '+':
        if (intervaloCruzeiro > INTERVALO_MIN) intervaloCruzeiro -= 50;
        Serial.print("cruzeiro="); Serial.println(intervaloCruzeiro);
        break;
      case '-':
        if (intervaloCruzeiro < INTERVALO_MAX) intervaloCruzeiro += 50;
        Serial.print("cruzeiro="); Serial.println(intervaloCruzeiro);
        break;
    }
  }
}

// Gerencia mudancas de estado com seguranca:
// - trocar de direcao exige frear ate parar antes de virar (nao inverte em velocidade)
// - largada e parada passam pela rampa
void atualizarEstado() {
  if (comandado == estado) return;

  if (estado == PARADO) {
    // saindo do repouso: aplica o novo sentido e comeca devagar
    estado = comandado;
    aplicarSentido(estado);
    intervaloAtual = INTERVALO_LENTO;
  } else if (comandado == PARADO) {
    // pedido de parar: a rampa cuida da desaceleracao (ver loop)
    // mantem 'estado' andando enquanto desacelera; para de fato quando fica lento
  } else {
    // trocar de uma direcao para outra: primeiro freia ate parar
    // forcamos estado -> PARADO gradualmente reusando a logica de parada:
    // marca como se o usuario tivesse pedido para parar antes de virar
    if (intervaloAtual < INTERVALO_LENTO) {
      // ainda em movimento: desacelera antes de trocar (tratado no loop)
    } else {
      // ja esta lento o suficiente: pode inverter agora
      estado = comandado;
      aplicarSentido(estado);
      intervaloAtual = INTERVALO_LENTO;
    }
  }
}

void loop() {
  lerSerial();
  atualizarEstado();

  if (estado == PARADO) return;

  // Alvo da rampa: se o usuario quer parar OU quer trocar de direcao,
  // o alvo e "parar" (intervalo lento); senao, acelera ate a velocidade de cruzeiro.
  bool freando = (comandado == PARADO) || (comandado != estado);
  unsigned long alvo = freando ? INTERVALO_LENTO : intervaloCruzeiro;

  unsigned long agora = micros();
  if (agora - ultimoPasso >= intervaloAtual) {
    ultimoPasso = agora;
    nivelPasso = !nivelPasso;
    int nivel = nivelPasso ? HIGH : LOW;
    digitalWrite(L_STEP, nivel);
    digitalWrite(R_STEP, nivel);

    // Aplica a rampa uma vez por ciclo de passo (na borda de subida).
    if (nivelPasso) {
      if (intervaloAtual > alvo) {
        intervaloAtual -= RAMPA_POR_PASSO;
        if (intervaloAtual < alvo) intervaloAtual = alvo;
      } else if (intervaloAtual < alvo) {
        intervaloAtual += RAMPA_POR_PASSO;
        if (intervaloAtual > alvo) intervaloAtual = alvo;
      }

      // Se estava freando e ja chegou no lento, conclui a parada/troca.
      if (freando && intervaloAtual >= INTERVALO_LENTO) {
        if (comandado == PARADO) {
          estado = PARADO;
        } else {
          estado = comandado;        // agora sim inverte com seguranca
          aplicarSentido(estado);
        }
      }
    }
  }
}
