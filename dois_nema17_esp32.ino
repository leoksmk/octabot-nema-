/*
 * Carrinho com 2 motores NEMA 17 (tracao diferencial) - ESP32 + 2 A4988 (PCB vermelha).
 * Controle por comando SERIAL, movimento continuo e nao-bloqueante.
 *
 * Comandos (Serial Monitor a 115200, "Sem final de linha" ou Enter):
 *   f ou w  -> frente
 *   t ou s  -> tras (para tras)
 *   e ou a  -> gira para a esquerda (no lugar)
 *   d       -> gira para a direita (no lugar)
 *   p ou x  -> parar
 *   + / -   -> aumenta / diminui a velocidade
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

// ---------- Velocidade ----------
// Intervalo entre passos em microssegundos. MENOR = mais rapido.
unsigned long intervaloPasso = 900;   // comece devagar; motor de passo trava se acelerar demais
const unsigned long INTERVALO_MIN = 300;
const unsigned long INTERVALO_MAX = 3000;

// ---------- Estado ----------
enum Movimento { PARADO, FRENTE, TRAS, ESQUERDA, DIREITA };
Movimento estado = PARADO;

unsigned long ultimoPasso = 0;
bool nivelPasso = false;   // alterna HIGH/LOW dos pinos STEP

void aplicarSentido() {
  // Define DIR de cada motor conforme o movimento e as flags de inversao.
  bool esquerdaFrente, direitaFrente;

  switch (estado) {
    case FRENTE:   esquerdaFrente = true;  direitaFrente = true;  break;
    case TRAS:     esquerdaFrente = false; direitaFrente = false; break;
    case ESQUERDA: esquerdaFrente = false; direitaFrente = true;  break; // gira no lugar
    case DIREITA:  esquerdaFrente = true;  direitaFrente = false; break; // gira no lugar
    default:       return; // PARADO: nao precisa de DIR
  }

  // "frente da roda" -> nivel do pino DIR (com inversao opcional)
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

  Serial.println("Carrinho NEMA17 pronto.");
  Serial.println("Comandos: f/w frente | t/s tras | e/a esquerda | d direita | p parar | +/- velocidade");
}

void lerSerial() {
  while (Serial.available() > 0) {
    char c = Serial.read();
    switch (c) {
      case 'f': case 'w': estado = FRENTE;   aplicarSentido(); Serial.println("FRENTE");   break;
      case 't': case 's': estado = TRAS;     aplicarSentido(); Serial.println("TRAS");     break;
      case 'e': case 'a': estado = ESQUERDA; aplicarSentido(); Serial.println("ESQUERDA"); break;
      case 'd':           estado = DIREITA;  aplicarSentido(); Serial.println("DIREITA");  break;
      case 'p': case 'x': estado = PARADO;   Serial.println("PARADO");   break;
      case '+':
        if (intervaloPasso > INTERVALO_MIN) intervaloPasso -= 100;
        Serial.print("intervalo="); Serial.println(intervaloPasso);
        break;
      case '-':
        if (intervaloPasso < INTERVALO_MAX) intervaloPasso += 100;
        Serial.print("intervalo="); Serial.println(intervaloPasso);
        break;
      // ignora '\r' e '\n'
    }
  }
}

void loop() {
  lerSerial();

  if (estado == PARADO) return;

  // Gera pulsos de passo sem bloquear, para manter o serial responsivo.
  unsigned long agora = micros();
  if (agora - ultimoPasso >= intervaloPasso) {
    ultimoPasso = agora;
    nivelPasso = !nivelPasso;
    int nivel = nivelPasso ? HIGH : LOW;
    // Os dois motores dao o passo ao mesmo tempo.
    digitalWrite(L_STEP, nivel);
    digitalWrite(R_STEP, nivel);
  }
}
