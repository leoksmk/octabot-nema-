# Octa ESP-NOW VR — gimbal por giroscópio

Controle do gimbal do Octabot por **head-tracker**: você inclina um controle
(ESP32-C3 Super Mini + giroscópio MPU6050) e o gimbal segue, via **ESP-NOW**.
Um **LED vermelho** no controle mostra o status: **piscando** = procurando,
**aceso fixo** = conectado.

O carrinho, a velocidade e as fitas de LED continuam sendo controlados pelo
celular (WiFi `Octabot` → `http://192.168.4.1`), igual ao Octabot normal.

## Os dois códigos

| Pasta | Placa | Função |
|-------|-------|--------|
| `receptor_octabot/` | ESP32 DevKit v1 (a placa do carrinho) | Recebe os ângulos por ESP-NOW e move os servos. É o Octabot, só o gimbal mudou. |
| `transmissor_c3_giroscopio/` | ESP32-C3 Super Mini | Lê o giroscópio, envia os ângulos, e acende o LED de status. |

## Bibliotecas
- **Receptor**: ESP32Servo + FastLED. (ESP-NOW já vem no core ESP32 — **não** precisa da WebSockets aqui.)
- **Transmissor**: nenhuma extra (MPU6050 lido direto por `Wire`).

## Ligação

**Transmissor (ESP32-C3 Super Mini):**
- MPU6050: `VCC→3V3`, `GND→GND`, `SDA→GPIO8`, `SCL→GPIO9`
- LED vermelho (2 pinos): `GPIO10 → resistor ~220Ω → perna + do LED`, `perna − → GND`
- Botão recentralizar: `GPIO3 → botão → GND` (zera o pan quando você olha pra frente)

**Receptor:** é a placa Mainboard Future Makers 2k26 do Octabot, sem mudança de fiação.

## Pareamento (passo que FAZ funcionar)

O ESP-NOW precisa do MAC do receptor. Faça uma vez:

1. Grave o `receptor_octabot` no ESP32 do carrinho.
2. Abra o **Serial Monitor a 115200**. Ao ligar, ele imprime:
   `MAC desta placa (use no transmissor): XX:XX:XX:XX:XX:XX`
3. Copie esse MAC para a linha `RECEIVER_MAC` do `transmissor_c3_giroscopio`, no formato:
   `uint8_t RECEIVER_MAC[] = {0xXX,0xXX,0xXX,0xXX,0xXX,0xXX};`
4. Grave o transmissor no ESP32-C3.

> **Canal:** os dois usam `WIFI_CANAL 1` (igual ao SoftAP do Octabot). Se mudar num,
> mude no outro — se os canais não baterem, não conecta (LED fica piscando pra sempre).

## Movimento (head-tracking)
- **Inclinar a cabeça** (acenar) → **tilt**: vem do acelerômetro, **sem drift**.
- **Virar a cabeça** (yaw) → **pan**: vem do **giroscópio integrado** — segue a cabeça
  junto, mas **deriva devagar** (o MPU6050 não tem bússola). Por isso:
  - **Ao ligar, fique parado ~1s** (o código calibra o zero do giroscópio).
  - Aperte o **botão de recentralizar** (GPIO3) olhando pra frente pra zerar o pan.
- Ganho 1:1 (`PAN_GANHO`/`TILT_GANHO` no transmissor). Se um eixo vier invertido, use
  `PAN_INVERTE` / `TILT_INVERTE`. Os limites de ângulo do servo ficam no **receptor**.
- Se o eixo errado responder (inclina e o pan mexe, ou vice-versa), é questão de como
  o MPU está montado na cabeça — me avise que troco os eixos no código.

## Observações honestas
- ESP-NOW + SoftAP no mesmo ESP32 convivem **desde que no mesmo canal** (por isso o canal fixo). É o ponto que mais dá problema — confira primeiro se não funcionar.
- O MAC a usar é o do **SoftAP** (é o que o código imprime). Não invente outro.
- Não foi testado em hardware aqui; os pinos de I2C do C3 (8/9) e o LED (10) podem precisar de ajuste conforme a sua placa.
