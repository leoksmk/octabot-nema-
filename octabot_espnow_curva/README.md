# Octabot ESP-NOW — versão CURVA ANDANDO

Igual ao `octabot_espnow/`, mas o carrinho faz **curva andando** (drive
diferencial com mistura): as duas rodas giram em velocidades independentes,
então dá pra ir pra frente **e** virar ao mesmo tempo, como um carro.

| Pasta | Placa | Função |
|-------|-------|--------|
| `receptor_carrinho_curva/` | ESP32 DevKit v1 (carrinho) | Recebe e mistura acelerador+esterço nas 2 rodas |
| `controle_mao_devkit_curva/` | ESP32 DevKit + MPU6050 | Inclinação da mão → acelerador + esterço |
| `controle_vr_c3_curva/` | ESP32-C3 + MPU6050 | Head-tracker → gimbal (idêntico ao normal) |

## O que muda em relação ao `octabot_espnow` normal
- **Controle da mão** manda dois valores com sinal: `acelerador` (-100..100, frente/ré)
  e `esterço` (-100..100, lados). Antes mandava só uma direção.
- **Receptor** faz a mistura: `rodaEsq = acelerador + esterço`, `rodaDir = acelerador − esterço`,
  e gira cada roda no seu próprio ritmo de passo (com rampa individual).
- Inclinar frente + lado ao mesmo tempo = **curva andando**. Só o lado = giro no eixo.
  Nivelada = parado.
- O **gimbal (VR) não muda** — é independente do modo de dirigir.

## Pareamento, canal, ligação, segurança
Iguais ao `octabot_espnow/` (veja o README de lá):
- Serial do carrinho imprime o MAC → cole nos dois controles.
- Os três no `WIFI_CANAL 1`.
- Dead-man: para se ficar >400 ms sem comando.
- Limites de servo no receptor; head-tracker calibra no boot + botão recentralizar.

## Ajustes do controle da mão
- `DEADZONE` (zona morta) e `MAXANG` (inclinação p/ 100%).
- `INV_PITCH` / `INV_ROLL` se frente/ré ou esquerda/direita vier invertido.

## Honestidade
- Não testado em hardware. Mapeamento de eixos do MPU e sentidos quase sempre
  precisam de um ajuste no primeiro teste.
- Curva andando puxa as duas rodas ao mesmo tempo: confira se a fonte 12V aguenta
  o pico dos dois motores juntos em velocidade.
