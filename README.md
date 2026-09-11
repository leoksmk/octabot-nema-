# octabot-nema — Carrinho com 2 NEMA 17 (ESP32 + A4988)

Carrinho de tração diferencial (2 rodas motrizes) controlado **sem fio pelo celular**.
O ESP32 cria a própria rede WiFi e serve uma página de controle (o "app") — você abre no
navegador, segura os botões pra andar e solta pra parar. Tem **rampa de aceleração**
(começa devagar e acelera sem forçar o motor), **curva estilo tank** (gira no próprio eixo)
e **corte de segurança automático** (se parar de chegar comando, o carrinho para sozinho).

Código-fonte: [`dois_nema17_esp32.ino`](dois_nema17_esp32.ino)

---

## 1. Componentes necessários

| Item | Quantidade | Observação |
|------|:---------:|------------|
| Placa ESP32 (DevKit v1, 38 pinos) | 1 | Serve qualquer ESP32 com WiFi |
| Motor de passo NEMA 17 | 2 | Bipolar, 4 fios, 1.8° (200 passos/volta) |
| Driver A4988 (PCB vermelha) | 2 | Um por motor |
| Fonte 12V | 1 | Corrente ≥ soma da corrente dos 2 motores (ex.: 2A a 3A) |
| Capacitor eletrolítico 100 µF / 25V | 2 | **Obrigatório**, um por driver |
| Chassi + 2 rodas + roda boba (castor) | 1 | Mecânica do carrinho |
| Protoboard / jumpers ou PCB | — | Para as ligações |

> **Por que o capacitor é obrigatório:** o motor de passo gera picos de tensão ao
> chavear as bobinas. Sem o capacitor de 100 µF entre VMOT e GND, esse pico pode
> **queimar o A4988**. Não pule esse item.

---

## 2. Ligação (wiring)

### 2.1. Pinos do ESP32 → A4988

| Sinal | Motor ESQUERDO | Motor DIREITO |
|-------|:-------------:|:-------------:|
| STEP  | GPIO **26**   | GPIO **32**   |
| DIR   | GPIO **27**   | GPIO **33**   |
| EN (enable) | GPIO **25** (compartilhado pelos dois) | GPIO **25** |

> O pino EN é **ativo em nível baixo**: `LOW` liga o driver, `HIGH` desliga.
> O código já coloca em `LOW` no `setup()`.

### 2.2. Cada driver A4988 — ligação completa

```
                 A4988 (visto de cima)
              +-----------------------+
   EN   ------|EN                 VMOT|------ +12V (fonte)  --+
   MS1  --x   |MS1                 GND|------ GND (fonte)  ----+---[ 100uF ]--- (entre VMOT e GND)
   MS2  --x   |MS2                  2B|------ bobina A do motor
   MS3  --x   |MS3                  2A|------ bobina A do motor
   RESET -+   |RESET                1A|------ bobina B do motor
   SLEEP -+   |SLEEP                1B|------ bobina B do motor
   STEP -----|STEP                 VDD|------ +3.3V (ESP32)
   DIR  -----|DIR                  GND|------ GND (ESP32)
              +-----------------------+

   --x  = deixar SEM ligar (passo cheio / full step)
   -+   = RESET e SLEEP ligados juntos (ponte entre eles), senão o driver fica inerte
```

### 2.3. Alimentação e terra comum

```
   Fonte 12V (+) ----+----> VMOT do driver ESQUERDO
                     +----> VMOT do driver DIREITO

   Fonte 12V (GND) --+----> GND dos dois drivers
                     +----> GND do ESP32     <=== TERRA COMUM (obrigatório!)

   ESP32 3.3V -------+----> VDD dos dois drivers (lógica)
```

> **O erro nº 1 de "não funciona":** esquecer de ligar o **GND da fonte no GND do ESP32**.
> Sem terra comum, os sinais STEP/DIR não têm referência e nada gira. O ESP32 é alimentado
> pela USB (ou pela Vin=5V), **não** pelos 12V direto.

### 2.4. Bobinas do NEMA 17

O NEMA 17 bipolar tem 4 fios = 2 bobinas (A e B). Você precisa descobrir os pares.
Com um multímetro em continuidade: os dois fios que têm continuidade entre si formam
uma bobina. Ligue uma bobina em `1A/1B` e a outra em `2A/2B`.

> **Nunca conecte/desconecte o motor com o driver energizado** — isso queima o A4988.
> Desligue a fonte antes de mexer nos fios do motor.

---

## 3. Ajuste da corrente (Vref) — faça ANTES de usar

O potenciômetro do A4988 define a corrente enviada ao motor. Errado, o motor esquenta,
perde passo ou queima.

1. Ligue só a lógica (USB do ESP32 + 12V), **sem** mandar o motor girar.
2. Meça a tensão entre o **potenciômetro (parafuso metálico)** e o **GND**.
3. Ajuste conforme a corrente nominal do seu motor. Fórmula do A4988:
   `Vref = corrente_desejada × 8 × Rsense`
   - Placas comuns têm `Rsense = 0.1 Ω` → `Vref = I × 0.8`
   - Ex.: motor de 1A → Vref ≈ **0.8V**. Comece com **~0.5V** e suba se faltar torque.
4. O motor deve ter torque sem esquentar demais (morno é normal; quente demais = baixe).

---

## 4. Gravando o código

1. Instale o **suporte ESP32** na Arduino IDE (Boards Manager → "esp32" by Espressif).
2. Selecione a placa (ex.: *ESP32 Dev Module*) e a porta COM.
3. Abra `dois_nema17_esp32.ino` e clique em **Upload**.
   Só usa bibliotecas nativas (`WiFi.h`, `WebServer.h`) — não precisa instalar nada.

---

## 5. Usando o controle (o "app")

1. No celular, abra o WiFi e conecte em:
   - **Rede:** `Carrinho-NEMA`
   - **Senha:** `12345678`
2. Abra o navegador em: **`http://192.168.4.1`**
3. Controles na tela:

   | Botão | Ação |
   |:-----:|------|
   | ▲ | Frente |
   | ▼ | Trás |
   | ◀ | Gira à esquerda (no eixo) |
   | ▶ | Gira à direita (no eixo) |
   | STOP | Para na hora |
   | − / + | Diminui / aumenta a velocidade |

4. **Segure o botão pra andar; solte pra parar.**
5. Dica: no menu do navegador, "Adicionar à tela inicial" cria um ícone que abre
   direto na página — fica igual a um app.

### Segurança automática (dead-man switch)

Enquanto o botão está apertado, a página reenvia o comando a cada 150 ms. Se o ESP32
ficar **mais de 500 ms sem receber comando**, ele **para sozinho**. Isso cobre: soltar
o botão, o dedo escorregar, trocar de app, bloquear a tela, o WiFi cair ou o celular
sair do alcance. O carrinho só anda enquanto um botão está ativamente pressionado.

---

## 6. Parâmetros que você pode ajustar (topo do `.ino`)

| Constante | Valor padrão | O que faz |
|-----------|:-----------:|-----------|
| `INTERVALO_LENTO` | 2000 µs | Velocidade da largada e da parada. Maior = arranca mais devagar. |
| `INTERVALO_MIN` | 350 µs | Teto de velocidade (menor = mais rápido). Se travar antes, aumente. |
| `INTERVALO_MAX` | 2500 µs | Piso de velocidade (mais lento permitido). |
| `RAMPA_POR_PASSO` | 8 | Agressividade da rampa. Maior = acelera mais rápido (arrisca travar). |
| `intervaloCruzeiro` | 500 µs | Velocidade alvo ao andar (ajustável ao vivo com −/+). |
| `TIMEOUT_MS` | 500 ms | Tempo sem comando até parar sozinho. WiFi ruim? Aumente p/ 800-1000. |
| `L_INVERTE` / `R_INVERTE` | false / true | Inverte o sentido de uma roda se ela girar ao contrário. |
| `AP_SSID` / `AP_PASS` | Carrinho-NEMA / 12345678 | Nome e senha da rede WiFi criada. |

---

## 7. Solução de problemas

| Sintoma | Causa provável | Solução |
|---------|----------------|---------|
| Nada gira | Sem terra comum | Ligue o GND da fonte no GND do ESP32 |
| Nada gira | RESET/SLEEP soltos | Faça a ponte entre RESET e SLEEP |
| Motor "canta" mas não gira | Velocidade alta demais na largada | Aumente `INTERVALO_MIN` / diminua `RAMPA_POR_PASSO` |
| Uma roda gira ao contrário | Sentido espelhado | Troque `L_INVERTE` ou `R_INVERTE` |
| Motor esquenta muito | Vref alto | Baixe o potenciômetro do A4988 |
| Perde passo ao inverter | (não deveria) | Confirme que está na versão com rampa; reduza `intervaloCruzeiro` |
| Carrinho para sozinho andando | WiFi fraco (falso timeout) | Aumente `TIMEOUT_MS` |
| Não acha a rede WiFi | Não gravou / senha < 8 caracteres | Regrave; senha precisa ter ≥ 8 caracteres |
| A4988 queimou | Mexeu no motor energizado, ou sem capacitor | Nunca conecte motor com fonte ligada; use o capacitor 100 µF |

---

## 8. Limitações conhecidas (honestas)

- **A rampa é linear**, não uma curva de aceleração perfeita. É ótima para um carrinho;
  para precisão de CNC, use a biblioteca `AccelStepper`.
- **Controle é HTTP por toque, não WebSocket.** Ideal para ligar/parar. Não faz controle
  proporcional/analógico (acelerador contínuo) — isso exigiria WebSocket.
- **A4988 é básico** (ruidoso, sem controle de corrente fino). Para silêncio e eficiência,
  os pinos STEP/DIR são compatíveis com **DRV8825** ou **TMC2208** — só ajuste a corrente.
- **Curva é só no eixo (tank).** Não faz curva em arco andando; para isso seria preciso
  girar as rodas em velocidades diferentes.
