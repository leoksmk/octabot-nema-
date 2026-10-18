# Octabot (web) — versão CURVA ANDANDO

Igual ao `dois_nema17_esp32.ino`, mas dirige com **drive diferencial**: no lugar
do D-pad tem um **joystick de dirigir** (vertical = acelerador, horizontal = esterço),
então o carrinho faz **curva andando** (frente + virar ao mesmo tempo).

- Controle: WiFi `Octabot` (senha `12345678`) → `http://192.168.4.1`
- Esquerda = joystick de dirigir; direita = joystick do gimbal + botão LED.
- Bibliotecas: ESP32Servo, FastLED, WebSockets (Markus Sattler).
- Soltar o joystick de dirigir = para (mola + dead-man de 400 ms).
- Não mexe no `dois_nema17_esp32.ino` original.
