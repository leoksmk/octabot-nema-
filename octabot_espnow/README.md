# Octabot ESP-NOW — 100% sem fio (3 ESP32)

Sistema todo em **ESP-NOW** (sem WiFi/HTTP). Três ESP32:

| Pasta | Placa | Função |
|-------|-------|--------|
| `receptor_carrinho/` | ESP32 DevKit v1 (placa do carrinho) | Recebe tudo e aciona motores + gimbal + LEDs |
| `controle_vr_c3/` | ESP32-C3 Super Mini + MPU6050 | Head-tracker → move o **gimbal** |
| `controle_mao_devkit/` | ESP32 DevKit v1 + MPU6050 | Inclinação da mão → **dirige o carrinho** |

Os dois controles mandam pro mesmo carrinho. O pacote tem um **tipo**:
`G` = gimbal (pan/tilt), `M` = movimento (direção + velocidade). Assim um não atrapalha o outro.

## Bibliotecas
- **receptor_carrinho**: ESP32Servo + FastLED (ESP-NOW já vem no core).
- **controles**: nenhuma extra (MPU6050 lido direto por `Wire`).

## Ligação
- **Carrinho**: a placa Octabot, sem mudança.
- **Controle VR (C3)**: MPU `SDA→8, SCL→9`; LED `GPIO10→220Ω→LED+`, `LED-→GND`; botão recentralizar `GPIO3→GND`.
- **Controle mão (DevKit)**: MPU `SDA→21, SCL→22`; LED `GPIO2` (onboard) ou externo.

## Pareamento (uma vez)
1. Grave o `receptor_carrinho` e abra o **Serial (115200)** — ele imprime:
   `MAC deste carrinho (use nos controles): XX:XX:...`
2. Cole esse MAC no `RECEIVER_MAC` dos **dois** controles.
3. Grave cada controle na sua placa.

> **Canal:** os três usam `WIFI_CANAL 1`. Se mudar, mude nos três — se não bater, não conecta (LED pisca pra sempre).

## Como dirige (controle da mão)
- Inclinar pra **frente/trás** → anda/ré. Inclinar pros **lados** → gira (tank).
- **Mão nivelada = parado** (zona morta). Quanto mais inclina, mais rápido.
- Se frente/trás ou esquerda/direita vier invertido: `INV_PITCH` / `INV_ROLL` no controle da mão.
- Ajuste `DEADZONE` (zona morta, graus) e `MAXANG` (inclinação p/ velocidade máxima).

## Como mira (controle VR)
- Inclinar a cabeça → tilt (sem drift). Virar a cabeça → pan (giroscópio integrado, **deriva devagar**).
- Fique **parado ~1s ao ligar** (calibra o giroscópio) e use o **botão recentralizar** pra zerar o pan.

## Segurança
- **Dead-man**: se o carrinho ficar >400 ms sem pacote de movimento (controle desligado, fora de alcance, mão nivelada), ele **para sozinho**.
- Limites de ângulo do servo (`PAN_MIN/MAX`, `TILT_MIN/MAX`) ficam no **receptor** — calibre pra não forçar o batente.

## Observações honestas
- ESP-NOW + os três no mesmo canal é o ponto nº1 de falha — confira canal e MAC primeiro.
- O MAC a usar é o que o carrinho imprime (STA MAC). Não invente outro.
- Não testado em hardware; pinos I2C e mapeamento de eixos podem precisar de ajuste conforme a montagem de cada MPU.
