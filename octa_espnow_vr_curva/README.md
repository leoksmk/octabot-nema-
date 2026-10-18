# Octa ESP-NOW VR — versão CURVA ANDANDO

Igual ao `octa_espnow_vr/`, mas o carro dirige com **curva andando** (joystick de
dirigir na web) e o **gimbal** continua pelo **giroscópio via ESP-NOW**.

- `receptor_octabot_curva/` — carro por WebSocket com mistura (curva andando) + gimbal por ESP-NOW.
- `transmissor_c3_giroscopio/` — head-tracker (idêntico ao do `octa_espnow_vr`).
- Pareamento: o receptor imprime o MAC no Serial → cole no transmissor. Canal 1 nos dois.
- Bibliotecas do receptor: ESP32Servo, FastLED, WebSockets. Transmissor: nenhuma extra.
