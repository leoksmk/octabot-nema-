# Carrinho NEMA17 v2 — controle de PS4 (Bluetooth)

Mesmo firmware da [`old/dois_nema17_esp32v2`](../old/dois_nema17_esp32v2/dois_nema17_esp32v2.ino)
(micropasso, rampa corrigida, curva tank, gimbal pan/tilt), mas **sem WiFi/página HTML**:
quem controla agora é um **controle de PS4 via Bluetooth**. Foi feito para o controle
paralelo **BGP 2016**, e também serve para o DualShock 4 original.

Código: [`dois_nema17_esp32v2_ps4.ino`](dois_nema17_esp32v2_ps4.ino)

## Por que Bluepad32 (e não a lib "PS4Controller")

A lib `PS4Controller` só conecta se você gravar o MAC do ESP32 no controle (SixaxisPairTool)
e costuma falhar com controle paralelo. A **Bluepad32** pareia direto (SHARE + PS) e aceita
a maioria dos clones de DS4. Precisa de um ESP32 com Bluetooth clássico. O DevKit v1 da placa
tem, mas ESP32-C3/S3 **não** servem.

## Instalação (Arduino IDE)

1. **Arquivo > Preferências > URLs adicionais para Gerenciadores de Placas**, adicione:
   ```
   https://raw.githubusercontent.com/ricardoquesada/esp32-arduino-lib-builder/master/bluepad32_files/package_esp32_bluepad32_index.json
   ```
2. **Ferramentas > Placa > Gerenciador de Placas**: instale **esp32_bluepad32**.
3. **Placa:** `esp32_bluepad32` > **ESP32 Dev Module** (não use a placa "esp32" comum: lá não existe `Bluepad32.h`).
4. **Library Manager:** instale **ESP32Servo** (Kevin Harrington).
   **Não** instale a biblioteca "Bluepad32" do Library Manager: ela aparece como
   **"Bluepad32 for NINA-W10 boards"** e é para Nano RP2040 Connect, MKR WiFi 1010, Nano 33 IoT etc.,
   não para ESP32. Para o ESP32, a Bluepad32 já vem **dentro do pacote de placa** do passo 2
   (com o nome `Bluepad32_ESP32`). Se a NINA já estiver instalada, ela não atrapalha desde que a
   placa selecionada seja a do passo 3; mesmo assim, desinstale para evitar confusão.
5. Grave o código.

Testado compilando com esp32_bluepad32 4.1.0 + ESP32Servo 3.2.1 (86% da flash com Telnet, 55% sem).
**Não foi testado no hardware.**

## Parear

1. Ligue o carrinho. Para ver o que acontece, abra o Monitor Serial em 115200.
2. No controle, segure **SHARE + PS** até a luz piscar rápido.
3. Conectou: a luz fica **azul** e o controle vibra. Nas próximas vezes, basta apertar **PS**.

Se não conectar depois de ter pareado o controle com o PS4/celular: descomente
`BP32.forgetBluetoothKeys();` no `setup()`, grave, pareie, e comente de novo.

## Comandos

| Controle | Ação |
|---|---|
| Analógico **esquerdo** | anda: frente/trás/gira tank, com velocidade **proporcional** ao quanto você empurra |
| **D-pad** | anda no limite de velocidade (como os botões do app antigo) |
| **L1 / R1** | diminui / aumenta o limite de velocidade em 10% (substitui o slider). A luz vai de azul (devagar) a vermelho (rápido) |
| Analógico **direito** | move o gimbal. Soltando, ele segura a posição |
| **R3** ou **Triângulo** | centraliza o gimbal |
| **X** | STOP (freia na rampa) |

Soltou tudo, o controle desligou ou caiu o Bluetooth: o carrinho para sozinho, com rampa.

## Log sem fio (Telnet)

Além do Monitor Serial, o mesmo log sai por **Telnet**:

1. Conecte no WiFi **`Octabot-Log`** (senha `12345678`).
2. Abra `telnet 192.168.4.1` (porta 23). No Windows, ative "Cliente Telnet" nos recursos do
   Windows ou use o PuTTY em modo Telnet. No celular, use um app de terminal com Telnet.
3. Aparecem a conexão do controle (nome/VID/PID), as trocas de estado e de limite, e uma linha de
   status 2x por segundo:
   ```
   FRENTE cmd=FRENTE 377 passos/s limite=50% gimbal=90/90 LX=0 LY=-512 BT=ok perdidos=0
   ```
   Digite `s` para ligar/desligar o status e `h` para ajuda.

Detalhes:
- Só um cliente por vez; um novo derruba o anterior.
- O log **nunca espera o WiFi**: se a rede engasgar, a mensagem é descartada (`perdidos=`)
  para não atrasar os passos do motor.
- **WiFi e Bluetooth dividem o mesmo rádio do ESP32.** O pacote Bluepad32 tem a convivência
  ativada, mas o controle pode ficar com mais latência. Se o controle travar ou cair, coloque
  `TELNET_LOG = false` (o WiFi nem liga).
- O que acontece antes de você conectar o Telnet só aparece no Monitor Serial.

## Ajustes no topo do código

- `ZONA_MORTA` (90 de 512): se o carrinho anda sozinho com o analógico solto (drift, comum em clone), aumente.
- `TIMEOUT_MS` (1000): para se o controle parar de mandar dados. Se o carrinho "engasgar"
  com o analógico parado (clone que só manda dado quando algo muda), aumente.
- `GIMBAL_GRAUS_POR_S`, `PAN_INVERTE`, `TILT_INVERTE`, `L_INVERTE`, `R_INVERTE`, `MICROPASSO`:
  funcionam como na v2.

## Limitações

- A curva continua **tank** (as duas rodas no mesmo ritmo de passo, como na v2), para manter
  as pernas em fase. Não dá para fazer curva andando: o analógico escolhe o eixo dominante.
  Para curva andando, veja `dois_nema17_esp32_curva/`.
- Como o comportamento de um controle paralelo varia, confira no Monitor Serial o nome/VID/PID
  que aparece ao conectar. Se a Bluepad32 não reconhecer o BGP 2016 como DS4, mande esse log.

## Simulação (sem hardware)

[`simulacao/`](simulacao/) roda este `.ino` **sem modificação** no PC, com Arduino, servos e
Bluepad32 trocados por versões falsas e um controle PS4 roteirizado (18 cenários: drift,
rampa, troca de direção, X, queda do Bluetooth, reconexão, 2º controle, clone sem dados,
gimbal, L1/R1, diagonal, Telnet com WiFi engasgado). Rode `./simulacao/rodar.sh` (precisa de g++).

O que ela **não** cobre: parear o BGP 2016 de verdade, o motor perder passo, e o tempo
real que o `BP32.update()` trava o loop. Para esse último, `./rodar.sh 300` simula 300 µs
de trava a cada 5 ms (a velocidade máxima cai uns 7%).
