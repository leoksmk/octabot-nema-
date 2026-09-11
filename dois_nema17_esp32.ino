/*
 * Controle de 2 motores NEMA 17 com ESP32 + 2 drivers A4988 (PCB vermelha).
 *
 * Ligacao tipica do A4988:
 *   VMOT  -> 12V (fonte dos motores) + capacitor 100uF entre VMOT e GND
 *   GND   -> GND da fonte E GND do ESP32 (terra comum e obrigatorio)
 *   VDD   -> 3.3V do ESP32 (logica)
 *   STEP  -> pino de passo do ESP32
 *   DIR   -> pino de direcao do ESP32
 *   EN    -> pino de enable (nivel BAIXO liga o driver)
 *   RESET -> ligado em SLEEP (para o driver funcionar)
 *   MS1/MS2/MS3 -> definem o microstepping (deixe sem ligar = passo cheio)
 *   1A/1B/2A/2B -> bobinas do motor NEMA 17
 *
 * IMPORTANTE: ajuste a corrente no potenciometro do A4988 antes de usar,
 * senao o motor esquenta ou perde passos.
 */

// ---------- Pinos ----------
// Motor 1
const int M1_STEP = 26;
const int M1_DIR  = 27;

// Motor 2
const int M2_STEP = 32;
const int M2_DIR  = 33;

// Enable compartilhado pelos dois drivers (LOW = ligado)
const int EN_PIN  = 25;

// ---------- Parametros ----------
const int PASSOS_POR_VOLTA = 200;   // NEMA 17 padrao: 1.8 graus = 200 passos
const int PULSO_US = 800;           // largura do pulso em microssegundos (velocidade)

void setup() {
  pinMode(M1_STEP, OUTPUT);
  pinMode(M1_DIR,  OUTPUT);
  pinMode(M2_STEP, OUTPUT);
  pinMode(M2_DIR,  OUTPUT);
  pinMode(EN_PIN,  OUTPUT);

  digitalWrite(EN_PIN, LOW);        // habilita os dois drivers
}

// Gira os dois motores ao mesmo tempo, na mesma quantidade de passos.
// sentido1 / sentido2: true = horario, false = anti-horario.
void girar(int passos, bool sentido1, bool sentido2) {
  digitalWrite(M1_DIR, sentido1 ? HIGH : LOW);
  digitalWrite(M2_DIR, sentido2 ? HIGH : LOW);

  for (int i = 0; i < passos; i++) {
    digitalWrite(M1_STEP, HIGH);
    digitalWrite(M2_STEP, HIGH);
    delayMicroseconds(PULSO_US);

    digitalWrite(M1_STEP, LOW);
    digitalWrite(M2_STEP, LOW);
    delayMicroseconds(PULSO_US);
  }
}

void loop() {
  // Uma volta completa com os dois motores no mesmo sentido
  girar(PASSOS_POR_VOLTA, true, true);
  delay(1000);

  // Uma volta completa em sentidos opostos
  girar(PASSOS_POR_VOLTA, false, false);
  delay(1000);
}
