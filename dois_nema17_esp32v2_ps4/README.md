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

Testado compilando com esp32_bluepad32 4.1.0 + ESP32Servo 3.2.1 (89% da flash com o status WiFi).
**Não foi testado no hardware.**

## Parear

1. Ligue o carrinho. Para ver o que acontece, abra o Monitor Serial em 115200.
2. No controle, segure **SHARE + PS** até a luz piscar rápido.
3. Conectou: o controle vibra e a luz acende na **cor do limite de velocidade** (azul = devagar, vermelho = rápido). Nas próximas vezes, basta apertar **PS**.

Se não conectar depois de ter pareado o controle com o PS4/celular: descomente
`BP32.forgetBluetoothKeys();` no `setup()`, grave, pareie, e comente de novo.

## Comandos

| Controle | Ação |
|---|---|
| Analógico **esquerdo** | anda: frente/trás/gira tank, com velocidade **proporcional** ao quanto você empurra. **Soltou, para na hora** |
| **D-pad** | anda no limite de velocidade (como os botões do app antigo) |
| **OPTIONS + L1 / R1** | segure OPTIONS e aperte L1/R1: diminui / aumenta o limite de velocidade em 10%. **L1/R1 sozinhos não fazem nada**, para quem só vai dirigir não desconfigurar. A luz vai de azul (devagar) a vermelho (rápido). **Fica gravado na flash** (ver abaixo) |
| Analógico **direito** | move o gimbal. Soltando, ele segura a posição |
| **R3** ou **Triângulo** | centraliza o gimbal |
| **X** | STOP (para na hora) |

Soltou tudo, o controle desligou ou caiu o Bluetooth: o carrinho para sozinho.

### Parada na hora

Soltou o analógico (ou X, ou caiu o Bluetooth): os passos param **na hora** (na simulação, no
pior caso 4,4 ms e 15 passos depois de soltar, na velocidade máxima).

- O driver **continua ligado por 300 ms** depois de parar, travando o motor. Sem isso o motor
  ficaria solto e o carrinho deslizaria no embalo. Depois desliga para não esquentar.
- Para **sair** de novo, continua tendo rampa (motor de passo não arranca direto em velocidade alta).
- Frente direto para ré: para, espera 100 ms e sai de ré devagar.
- Parar seco em velocidade alta dá tranco na mecânica e o motor pode pular alguns passos no
  tranco. Se quiser a frenagem suave da v2 de volta: `PARADA_IMEDIATA = false`.

## Limite de velocidade gravado na flash

O limite escolhido no OPTIONS + L1/R1 fica gravado: desligou e ligou de novo, volta no mesmo limite
(a página e o log mostram `limite carregado da flash: 70%`).

- Grava **2 s depois do último toque** em L1/R1 **e só com o carrinho parado**. Se você mexer
  andando, ele grava quando parar. O controle dá uma **vibração curtinha** quando gravou.
- Por que só parado: gravar na flash pausa o ESP32 por alguns milissegundos, o que faria o
  motor engasgar e atrasaria o Bluetooth.
- Apertar várias vezes seguidas grava uma vez só (a flash aguenta ~100 mil gravações por
  posição e a NVS ainda espalha o desgaste, então não é problema no uso normal).
- Cuidado: se você deixou em 100%, ele liga em 100%. A velocidade continua proporcional ao
  analógico, mas o D-pad já sai no máximo.

## Status sem fio (página no navegador)

Para ver o que está acontecendo sem cabo USB (o mesmo que sairia no Monitor Serial):

1. Conecte o celular/PC no WiFi **`Octabot-Log`** (senha `12345678`).
2. Abra **`http://192.168.4.1`** no navegador.

A página mostra, ao vivo (atualiza 2x por segundo): estado e comando, passos/s, limite,
controle conectado, bateria do controle (`n/d` se o clone não informar), gimbal, analógico
esquerdo e tempo ligado. Embaixo vem o **log desde o boot** (conexão do controle com
nome/VID/PID, trocas de estado e de limite), mesmo que você abra a página depois.

- É **só leitura**: o carrinho continua sendo controlado só pelo controle de PS4.
- **Se o ESP32 reiniciar sozinho** (queda de tensão, travamento, watchdog), o log de antes
  **não se perde**: fica numa memória (RTC) que sobrevive ao reinício. A página mostra
  `===== BOOT #n - motivo: ... =====` no log e um cartão vermelho **Último reinício**:
  - `BROWNOUT (tensao caiu)`: bateria fraca ou motores puxando demais. É o mais provável num carrinho.
  - `TRAVOU (panic / Guru Meditation)` ou `WATCHDOG`: bug no firmware. O detalhe (backtrace)
    só sai no serial de verdade; veja "Serial de verdade pelo celular" abaixo.
  - O log só apaga quando falta energia de vez (desligou a chave). Depois de um reinício, o
    relógio do log volta a 0 s.
- O histórico guarda os últimos 4 KB de log (umas 100 linhas).
- Também dá para ler por Telnet (`telnet 192.168.4.1`), que inclui uma linha de status
  periódica (`s` liga/desliga, `h` ajuda).
- O log **nunca espera o WiFi**. Cada atualização da página é curta (no máximo ~1,5 KB) e
  a rede só é atendida a cada 50 ms, mas cada resposta ainda segura o loop por alguns ms.
  Não medi isso no ESP32; se o motor engasgar só com a página aberta, feche a página.
- **WiFi e Bluetooth dividem o mesmo rádio do ESP32.** O pacote Bluepad32 tem a convivência
  ativada, mas o controle pode ficar com mais latência. Se o controle travar ou cair, coloque
  `LOG_WIFI = false` (o WiFi nem liga e fica só o Monitor Serial).
- O firmware fica com 89% da flash. Se crescer mais, use **Ferramentas > Partition Scheme >
  Huge APP**.

## Serial de verdade pelo celular (sem notebook)

A página mostra o que o firmware imprime, mas **não** mostra o que vai direto para a porta
serial: mensagens de boot do ESP32, mensagens internas da Bluepad32 sobre o pareamento e o
backtrace de um travamento. Para ver isso sem notebook:

- **Android:** cabo **USB OTG** (ou adaptador USB-C → USB-A) + cabo USB do ESP32 e o app
  **"Serial USB Terminal"** (Kai Morich). Escolha 115200 baud. Funciona com os chips CP2102 e
  CH340 dos DevKits.
- **iPhone:** não funciona (o iOS não aceita esses chips USB-serial). Fica só a página.



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
Bluepad32 trocados por versões falsas e um controle PS4 roteirizado (22 cenários: drift,
rampa, troca de direção, X, queda do Bluetooth, reconexão, 2º controle, clone sem dados,
gimbal, L1/R1, diagonal, Telnet com WiFi engasgado, página de status, reinício por queda de tensão, limite gravado na flash, parada na hora, L1/R1 sem OPTIONS). Rode `./simulacao/rodar.sh` (precisa de g++).

O que ela **não** cobre: parear o BGP 2016 de verdade, o motor perder passo, e o tempo
real que o `BP32.update()` trava o loop. Para esse último, `./rodar.sh 300` simula 300 µs
de trava a cada 5 ms (a velocidade máxima cai uns 7%).
