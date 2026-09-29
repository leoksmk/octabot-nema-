# octabot_espnow — controle por inclinação (MPU6050) via ESP-NOW

Versão do firmware **normal** (`../dois_nema17_esp32.ino`, sem a cinemática de fase do
`octabot_fase`) trocada de **app WiFi** para **ESP-NOW**, com um **controle de mão**
feito de um segundo ESP32 + MPU6050.

| Inclinação do controle | Carrinho |
|---|---|
| Para frente | Anda para frente |
| Para trás | Anda para trás |
| Para a direita / esquerda | **Só gira** no próprio eixo (estilo tank) |
| Nivelado (dentro da zona morta) | Para |

A velocidade é **proporcional** à inclinação. A rampa de aceleração, a frenagem antes de
inverter o sentido e o desligamento dos drivers quando parado continuam iguais ao original.

Os dois sketches ficam em pastas próprias porque a Arduino IDE exige que a pasta tenha
o mesmo nome do `.ino`:

- [`carrinho_espnow/carrinho_espnow.ino`](carrinho_espnow/carrinho_espnow.ino): grave no ESP32 da **Mainboard** (robô).
- [`controle_mpu6050/controle_mpu6050.ino`](controle_mpu6050/controle_mpu6050.ino): grave no ESP32 do **controle**.

Nenhuma biblioteca extra é necessária (`WiFi`, `esp_now` e `Wire` já vêm no core ESP32).
Funciona no core ESP32 2.x e 3.x.

---

## 1. Ligação do controle

| MPU6050 | ESP32 DevKit |
|---|---|
| VCC | 3V3 |
| GND | GND |
| SDA | GPIO 21 |
| SCL | GPIO 22 |

Monte o módulo com a **seta X apontando para a frente** do controle e o chip virado para cima.
Alimente com power bank pela USB ou com bateria no VIN.

**Botão de homem-morto (opcional, recomendado):** um botão entre o GPIO 13 e o GND, com
`USAR_BOTAO = true`. Assim o carrinho só anda enquanto o botão estiver apertado.

## 2. Uso

1. Ligue o **carrinho**.
2. Ligue o **controle** e deixe-o **parado na posição neutra por ~1 s** (o LED azul pisca).
   Essa posição vira o zero. Para recalibrar, aperte o EN/reset do controle.
3. Incline. O LED azul do controle fica aceso enquanto ele manda o carrinho andar.

O Serial Monitor do controle (115200) mostra `pitch`, `roll`, `frente` e `giro` para
você ajustar os valores.

## 3. Como funciona

- O controle lê o **acelerômetro** do MPU6050, calcula pitch/roll, aplica filtro e zona
  morta e envia `frente` (−100..100) e `giro` (−100..100) **25 vezes por segundo**.
- O envio é em **broadcast** (`FF:FF:FF:FF:FF:FF`), então não é preciso copiar o MAC de
  nenhum dos lados. O carrinho descarta pacotes que não tenham o `MAGICO` certo.
- **Um eixo por vez:** o eixo mais inclinado vence. Com o controle na diagonal existe
  uma histerese (`HISTERESE_EIXO`) para o carrinho não ficar alternando entre andar e girar.
- **Dead-man switch:** se o carrinho passar `TIMEOUT_MS` (300 ms) sem receber pacote,
  porque o controle desligou, a bateria acabou ou saiu do alcance, ele freia e para.

## 4. Parâmetros

**Controle (`controle_mpu6050.ino`)**

| Constante | Padrão | O que faz |
|---|:---:|---|
| `ZONA_MORTA_GRAUS` | 8° | Inclinação mínima para mover |
| `ANGULO_MAX_GRAUS` | 35° | Inclinação que dá 100% |
| `FILTRO` | 0.2 | Suavização (menor = mais suave e mais lento para responder) |
| `INVERTE_FRENTE` / `INVERTE_GIRO` | false | Corrige a direção se o MPU foi montado em outra orientação |
| `USAR_BOTAO` / `PIN_BOTAO` | false / 13 | Botão de homem-morto |

**Carrinho (`carrinho_espnow.ino`)**

| Constante | Padrão | O que faz |
|---|:---:|---|
| `INTERVALO_MIN` | 150 µs | Velocidade a 100% (se o motor perder passo, suba) |
| `INTERVALO_MAX` | 2500 µs | Velocidade logo após a zona morta |
| `GIRO_MAX_PCT` | 60 % | Limita a velocidade do giro |
| `HISTERESE_EIXO` | 15 | Margem para trocar entre andar e girar |
| `TIMEOUT_MS` | 300 ms | Tempo sem pacote até parar |
| `L_INVERTE` / `R_INVERTE` | false / true | Sentido de cada roda |
| `MICROPASSO` | 1 | Tem que bater com os jumpers M0/M1/M2 do DRV8825 (ver seção 5) |

`MAGICO` e `CANAL_WIFI` precisam ser **iguais nos dois códigos**. Se houver outro
carrinho com este firmware por perto, troque o `MAGICO` para que um controle não
comande o robô do outro.

## 5. Barulho, ressonância e micropasso

Os problemas de motor encontrados no `octabot_fase` também valem aqui, porque a rampa e o
passo cheio vieram do mesmo código original.

- **Ressonância na faixa média.** Em passo cheio, cada passo de 1,8° dá um tranco. Por volta
  de **~380 passos/s**, o que aqui dá mais ou menos **metade da inclinação**, o NEMA 17
  vibra, faz barulho e perde passo com facilidade. Como a velocidade é proporcional à
  inclinação, o robô passa por essa faixa toda vez que acelera.
- **A 100% o motor pode estar perdendo passo.** Se ele ficar silencioso a 100%, pode ser
  sinal de que não está acompanhando. Teste: com o controle bem inclinado, o robô anda
  visivelmente mais rápido do que com ~75%? Se não andar, suba `INTERVALO_MIN` para ~300.
  Aqui perder passo não estraga fase nenhuma (esta versão não rastreia fase), mas o robô
  perde força e velocidade.
- **Bug corrigido: largada mais rápida que o cruzeiro.** Com pouca inclinação, abaixo de
  ~55%, o robô largava a `INTERVALO_LENTO` (1200 µs) e depois *desacelerava* até o cruzeiro
  (até 2500 µs), um tranco à toa. Agora a largada e a parada usam o mais lento entre os dois.

**A correção de verdade é o micropasso.** Ele divide cada passo de 1,8° em passos menores,
e o movimento fica suave e bem mais silencioso. Precisa de hardware, porque nesta placa os
pinos M0/M1/M2 do DRV8825 estão soltos:

1. **Com tudo desligado**, ligue o pino **M1** de cada módulo DRV8825 ao VCC lógico. O pino
   **RST** do próprio módulo já está no VCC nesta placa (é o 2º abaixo do M1), então um fio
   fino soldado entre M1 e RST resolve.
   ⚠️ Confira antes, com o multímetro, que o RST está em 3,3/5 V e **não em 12 V**. O VMOT
   fica do outro lado do módulo. Se M0/M1/M2 estiverem ligados a alguma trilha na placa,
   esse fio pode dar curto, então confira isso também.
2. No `carrinho_espnow.ino`, mude `MICROPASSO` para **4**. As velocidades e a rampa
   continuam iguais em rpm, porque o código compensa sozinho.

Tabela do DRV8825 (M2 M1 M0): `000` cheio · `001` 1/2 · `010` **1/4** · `011` 1/8.

**Sem mexer no hardware:** baixar um pouco o Vref também reduz o ruído, mas perde torque.

Simulado no PC (MICROPASSO 1 e 4, inclinação de 10 a 100%): a largada nunca fica mais
rápida que o cruzeiro, a frenagem, a inversão de sentido e o timeout continuam funcionando
e, a 100% com 1/4, o pulso fica em 37 µs. Não foi testado no robô.

## 6. Limitações

- **Não foi compilado nem testado em hardware neste ambiente.** Passou só por uma checagem
  de sintaxe com headers simulados. O primeiro teste deve ser feito com as rodas no ar.
- **Não faz curva em arco.** Os dois motores compartilham o mesmo pulso de STEP (igual ao
  original), então o robô só anda reto ou gira parado. Foi o que você pediu, mas é uma
  limitação do código, não uma escolha livre: para fazer arco, cada motor precisa de
  temporização independente.
- **Só acelerômetro, sem giroscópio.** Com a mão parada funciona bem, mas um movimento
  brusco do controle (chacoalhar) aparece como inclinação por um instante. O filtro e a
  rampa do carrinho amortecem isso, mas não eliminam.
- **Broadcast sem criptografia.** Qualquer ESP32 que conheça o `MAGICO` consegue comandar
  o robô. Para uma feira ou sala de aula isso é aceitável; fora disso, use peer com MAC fixo.
- **Sem o botão de homem-morto**, se você largar o controle inclinado (por exemplo, apoiado
  na mesa torto), o carrinho continua andando. Deixe-o nivelado ou ligue `USAR_BOTAO`.
