# octabot-nema — Carrinho com 2 NEMA 17 (Mainboard Future Makers 2k26)

Carrinho de tração diferencial (2 rodas motrizes) controlado **sem fio pelo celular**.
O ESP32 cria a própria rede WiFi e serve uma página de controle (o "app") — você abre no
navegador, segura os botões pra andar e solta pra parar. Tem **rampa de aceleração**
(começa devagar e acelera sem forçar o motor), **curva estilo tank** (gira no próprio eixo)
e **corte de segurança automático** (se parar de chegar comando, o carrinho para sozinho).

Feito para a placa **Mainboard Future Makers 2k26** (MYND): ESP32 DevKit v1 (30 pinos)
+ 2 drivers **DRV8825**. Os pinos abaixo já vêm do esquemático oficial da placa.

Código-fonte: [`dois_nema17_esp32.ino`](dois_nema17_esp32.ino)

---

## 1. O que a placa já resolve por você

Diferente de uma montagem em protoboard, esta placa já traz as ligações prontas:

- **Capacitores** de bulk no VMOT de cada driver (C3 / C4) — proteção já incluída.
- **SLEEP e RESET** dos DRV8825 ligados no VCC — os drivers já ficam ativos.
- **M0/M1/M2** livres = **passo cheio** (200 passos/volta).
- Conectores de motor **X3** (driver DRV3) e **X1** (driver DRV1).
- Entradas de alimentação **J1 (5V)** e **J2 (12V)**.
- Saídas extras: **U1/U3/U4** (servos) e **U5** (fita de LED) — não usadas por este firmware.

Ou seja: você só encaixa o ESP32 e os 2 DRV8825, liga os motores em X3/X1 e alimenta.

---

## 2. Mapa de pinos (do esquemático)

| Função | Motor ESQUERDO — DRV3 / conector X3 | Motor DIREITO — DRV1 / conector X1 |
|--------|:----------------------------------:|:----------------------------------:|
| STEP   | GPIO **25**                        | GPIO **27**                        |
| DIR    | GPIO **33**                        | GPIO **14**                        |
| EN     | GPIO **26**                        | GPIO **12**                        |

> **O EN é separado por driver** nesta placa (não é compartilhado). O código habilita os
> dois (nível `LOW`) no `setup()`.

> **Qual é "esquerdo" e qual é "direito"?** É só uma convenção do código. Se ao mandar
> "frente" o carrinho virar, veja a seção 6 (inverter sentido) ou troque os motores de
> conector (X3 ⇄ X1).

### ⚠️ GPIO12 é pino de strapping (atenção no boot)

O EN do motor direito está no **GPIO12**, que no ESP32 é um pino de *strapping* (MTDI):
se estiver em nível ALTO no momento do boot, o ESP32 pode não ligar (ele tenta configurar
a flash pra 1.8V). Como o EN do DRV8825 é ativo em nível baixo, o normal é ficar em LOW/flutuante
e o boot funciona. **Se o ESP32 travar ao ligar / não subir o WiFi:** é provável que algo
esteja puxando o GPIO12 pra cima. Solução: gravar o *eFuse* de flash voltage (avançado) ou,
mais simples, testar sem os drivers encaixados pra confirmar. Na prática, na maioria das
placas com DRV8825 isso não dá problema — mas fica o aviso, porque é a única armadilha real
de boot dessa pinagem.

---

## 3. Componentes

| Item | Quantidade | Observação |
|------|:---------:|------------|
| Mainboard Future Makers 2k26 | 1 | A PCB deste projeto |
| ESP32 DevKit v1 (30 pinos) | 1 | Encaixa no soquete central |
| Driver DRV8825 | 2 | Encaixa em DRV3 e DRV1 (respeite a orientação do pino EN!) |
| Motor de passo NEMA 17 | 2 | Bipolar, 4 fios, 1.8° (200 passos/volta) |
| Fonte 12V | 1 | Em J2. Corrente ≥ soma dos 2 motores (ex.: 2-3A) |
| Fonte 5V | 1 | Em J1 (alimenta o ESP32 pelo VIN). Ou alimente o ESP32 pela USB. |

> **Orientação do DRV8825:** encaixar o driver invertido é o erro que mais queima módulo.
> Confira o pino **EN** do módulo com o silk **`ENABL`** da placa antes de energizar.

---

## 4. Ajuste da corrente (Vref) — faça ANTES de usar

Cada DRV8825 tem um potenciômetro que define a corrente do motor. Errado, o motor esquenta,
perde passo ou queima.

1. Alimente só a lógica (USB do ESP32), **sem** mandar girar.
2. Meça a tensão entre o **potenciômetro (parafuso metálico)** e o **GND**.
3. Fórmula do **DRV8825**: `corrente_limite ≈ Vref × 2` (para `Rsense = 0.1 Ω`)
   → para um motor de **1A**, `Vref ≈ 0.5V`. Comece em **~0.4V** e suba se faltar torque.
   - ⚠️ Isto é **diferente do A4988** (lá seria `Vref = I × 0.8`). Não use a conta do A4988 aqui.
   - Confira o valor do `Rsense` da sua placa DRV8825 (resistores marcados tipo `R100` = 0.1 Ω).
4. O motor deve ter torque sem esquentar demais (morno é normal; quente demais = baixe o Vref).

---

## 5. Gravando e usando

### Gravar
1. Instale o suporte **ESP32** na Arduino IDE (Boards Manager → "esp32" by Espressif).
2. Selecione **ESP32 Dev Module** e a porta COM.
3. Abra `dois_nema17_esp32.ino` e clique em **Upload**. Só usa `WiFi.h` e `WebServer.h` (nativas).

### Usar o app
1. No celular, conecte no WiFi: rede **`Carrinho-NEMA`**, senha **`12345678`**.
2. Abra o navegador em **`http://192.168.4.1`**.

   | Botão | Ação |
   |:-----:|------|
   | ▲ | Frente |
   | ▼ | Trás |
   | ◀ | Gira à esquerda (no eixo) |
   | ▶ | Gira à direita (no eixo) |
   | STOP | Para na hora |
   | Slider | Velocidade de 0 a 100% (arraste para ajustar em tempo real) |

3. **Segure o botão pra andar; solte pra parar.** "Adicionar à tela inicial" cria um ícone de app.
   O **slider de velocidade** vale imediatamente para o próximo movimento (respeitando a rampa).

### Segurança automática (dead-man switch)
A página reenvia o comando a cada 150 ms; se o ESP32 ficar **> 500 ms sem receber comando**
(botão solto, dedo escorregou, trocou de app, WiFi caiu, saiu do alcance), o carrinho
**desacelera e para sozinho**. Só anda enquanto um botão está ativamente pressionado.

---

## 6. Parâmetros ajustáveis (topo do `.ino`)

| Constante | Valor padrão | O que faz |
|-----------|:-----------:|-----------|
| `INTERVALO_LENTO` | 2000 µs | Velocidade da largada e da parada. |
| `INTERVALO_MIN` | 350 µs | Teto de velocidade (menor = mais rápido). Se travar antes, aumente. |
| `INTERVALO_MAX` | 2500 µs | Piso de velocidade. |
| `RAMPA_POR_PASSO` | 8 | Agressividade da rampa. Maior = acelera mais rápido (arrisca travar). |
| `intervaloCruzeiro` | 500 µs | Velocidade alvo ao andar (ajustável ao vivo com −/+). |
| `TIMEOUT_MS` | 500 ms | Tempo sem comando até parar sozinho. WiFi ruim? Aumente p/ 800-1000. |
| `L_INVERTE` / `R_INVERTE` | false / true | Inverte o sentido de uma roda se ela girar ao contrário. |
| `AP_SSID` / `AP_PASS` | Carrinho-NEMA / 12345678 | Nome e senha da rede WiFi criada. |

---

## 7. Solução de problemas

| Sintoma | Causa provável | Solução |
|---------|----------------|---------|
| ESP32 não liga / não cria WiFi | GPIO12 em HIGH no boot | Ver seção 2 (strapping); teste sem drivers encaixados |
| Nada gira | Sem terra comum entre fonte 12V e ESP32 | Confirme o GND comum (J2 e ESP32) |
| Nada gira | DRV8825 encaixado invertido | Confira EN do módulo x silk `ENABL` |
| Motor "canta" mas não gira | Velocidade alta demais na largada | Aumente `INTERVALO_MIN` / diminua `RAMPA_POR_PASSO` |
| Uma roda gira ao contrário | Sentido espelhado | Troque `L_INVERTE` ou `R_INVERTE` |
| Motor esquenta muito | Vref alto | Baixe o potenciômetro do DRV8825 (lembre: I ≈ Vref × 2) |
| Carrinho para sozinho andando | WiFi fraco (falso timeout) | Aumente `TIMEOUT_MS` |
| Não acha a rede WiFi | Não gravou / senha < 8 caracteres | Regrave; senha ≥ 8 caracteres |
| DRV8825 queimou | Mexeu no motor energizado, ou driver invertido | Nunca conecte motor com fonte ligada; confira orientação |

---

## 8. Limitações conhecidas (honestas)

- **A rampa é linear**, não uma curva de aceleração perfeita. Ótima para carrinho; para
  precisão de CNC, use `AccelStepper`.
- **Controle é HTTP por toque, não WebSocket.** Ideal para ligar/parar; não faz controle
  proporcional/analógico (acelerador contínuo).
- **Curva é só no eixo (tank).** Não faz curva em arco andando.
- **Não foi testado em hardware neste ambiente.** Os pinos vêm do esquemático (corretos),
  mas os parâmetros de velocidade/rampa e o Vref devem ser calibrados no primeiro teste.
- **As saídas U1/U3/U4/U5** (servos e LED) da placa **não** são usadas por este firmware.
