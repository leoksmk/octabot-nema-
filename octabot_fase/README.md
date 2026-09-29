# octabot_fase — sincronia de fase das pernas

Versão do firmware que **sabe em que ângulo está a manivela de cada lado** e mantém a
defasagem entre as pernas do lado esquerdo e do direito, inclusive depois de girar.
O firmware original (`../dois_nema17_esp32.ino`) continua intocado. Pinagem, app WiFi,
gimbal e segurança são os mesmos. Veja o [README principal](../README.md) para montagem,
Vref e ligação.

Código: [`octabot_fase.ino`](octabot_fase.ino). A pasta e o `.ino` têm o mesmo nome
porque a Arduino IDE exige.

---

## 1. Cinemática do robô

### Cada lado tem 1 grau de liberdade

O motor gira o pinhão, o pinhão gira a coroa, a coroa gira a manivela, e a manivela move
as barras e as pernas. Todas as pernas de um lado dependem de **um único ângulo, φ**,
o da manivela. Então o robô inteiro se descreve com dois números:

```
estado = (φL, φR)
```

Não existe cinemática inversa "de braço" aqui. A trajetória do pé é fixa pelo mecanismo
de barras, e o software só controla **quando** cada lado está em cada ponto dela.

### Transmissão: pinhão de 6 dentes e coroa de 20

```
1 volta da manivela = 200 passos × 20/6 = 666,67 passos   ← não é inteiro
1 passo do motor    = 360° × 6 / (200 × 20) = 0,54° de manivela
```

Um contador de passos inteiro acumularia erro a cada volta. Por isso o código conta em
**unidades**: 1 volta = 200 × 20 = **4000 unidades** e 1 passo = **6 unidades**. A conta
fica exata para sempre, e o erro residual de uma correção é de no máximo 0,27°.

Confirmado pelo montador e pelo vídeo: em cada lado, o **pinhão preto de 6 dentes** fica no
eixo do motor, entre as duas **engrenagens cinzas de 20 dentes** (a da perna da frente e a
da perna de trás), e move as duas. As cinzas giram no mesmo sentido e na mesma velocidade.
Por isso os 90° entre a perna da frente e a de trás dependem só de como os dentes das cinzas
foram encaixados no pinhão.

> ⚠️ Um commit anterior (`25c1644`) fala em redução de **6,67:1**. O valor real é
> 20/6 = **3,33:1**. As velocidades daquele commit (`INTERVALO_MIN = 150`) foram escolhidas
> achando que havia o dobro de redução, ou seja, o dobro de torque na manivela. Se o motor
> perder passo em velocidade alta, suba `INTERVALO_MIN`. Neste firmware, perder passo também
> estraga a fase.

### O que cada movimento faz com a fase

| Movimento | φL | φR | Δφ = φR − φL |
|-----------|:--:|:--:|:------------:|
| Frente N passos | +N | +N | **não muda** ✅ |
| Trás N passos | −N | −N | **não muda** ✅ |
| Giro no eixo N passos | ±N | ∓N | **muda 2N** ❌ |
| Curva em arco (não implementada) | a | b | muda o tempo todo ❌ |

É por isso que andar para frente e para trás mantém as pernas certas e o giro bagunça.
Curva em arco e fase travada são incompatíveis: numa curva a fase sempre deriva e
precisa ser corrigida depois.

### A defasagem alvo

- **No mesmo lado**, as pernas ficam a 90° umas das outras. Isso é **mecânico** (vem de
  como as engrenagens foram encaixadas). Se estiver errado, o conserto é reencaixar as
  engrenagens, não mexer no código.
- **Entre os lados**, o alvo é a relação que **você salvar** com o botão *Salvar posição
  atual*. O zero de cada manivela é arbitrário, então o "−90°" no painel é só um rótulo
  para essa relação. O que vale é a posição física em que você deixou as pernas.

---

## 2. O que o firmware faz

1. **Conta a fase.** Cada borda de subida do STEP soma ±6 unidades à fase do lado,
   conforme o sentido.
2. **Sincroniza sozinho depois de um giro.** Ao terminar um giro no eixo, só o lado
   atrasado avança devagar até Δφ voltar ao alvo. A correção é sempre para frente e de no
   máximo meia volta. Se o próximo comando já for "frente", o robô sincroniza e emenda no
   movimento.
3. **Guarda a fase na flash** quando o robô para. Ela sobrevive a desligar e ligar de novo,
   desde que ninguém gire as pernas com a mão enquanto o robô está desligado.
4. **Usa um sensor, se houver** (opcional, um por lado). Na primeira passada da marca,
   aprende em que fase ela está. Nas passadas seguintes, corrige passos perdidos.

### Novos controles no app

| Controle | O que faz |
|----------|-----------|
| Painel de fase | Mostra φL, φR, Δφ, o alvo e o erro em tempo real (verde = erro < 3°) |
| **Sincronizar** | Corrige Δφ agora |
| **Esq − / Esq + / Dir − / Dir +** | Move só aquele lado em ~4,9° (9 passos) |
| **Salvar posição atual** | "As pernas estão na posição correta agora": a relação atual vira a referência. **Não move nada** |
| Texto abaixo do slider | Passos/s e rpm da manivela na velocidade escolhida |
| **STOP** | Para **tudo**, inclusive uma sincronia em andamento. Soltar um botão de direção **não** cancela a sincronia pós-giro |

---

## 3. Calibração (uma vez só)

1. Deixe as pernas na **posição correta de andar**. Pode ser na mão, com o robô desligado,
   ou com os botões **Esq ±** e **Dir ±** do app (cada toque move ~4,9°).
2. Abra o app e aperte **Salvar posição atual**. Confirme. O robô **não se mexe**; o
   painel passa a mostrar erro 0.
3. Teste: ande para frente, gire, solte. Depois de cada giro, o firmware deve voltar as
   pernas para essa mesma relação sozinho.

Quer mudar a referência? Ajuste com **Esq ±/Dir ±** e salve de novo.

> Versões anteriores tinham um botão *Zerar* que esperava os dois lados **iguais** e
> depois aplicava −90° sozinho. Quem já deixava as pernas na posição correta acabava com
> um deslocamento a mais de 90°, e toda sincronia seguinte alinhava errado. Esse botão foi
> removido.

Enquanto não calibrar, o app mostra "NAO calibrado", e o firmware assume que o robô foi
montado já na posição correta.

---

## 4. Sensor de referência (recomendado)

Sem sensor, a fase é **estimada**, não medida. Um passo perdido (tropeço, travamento,
largada rápida demais) desloca a fase para sempre, até a próxima calibração.

- **Sensor:** hall digital (por exemplo, A3144, saída ativa em LOW) mais um ímã pequeno
  colado na coroa de 20 dentes, ou um sensor óptico com uma marca na coroa. Use um por lado.
- **Pinos:** ajuste `SENSOR_L_PIN` e `SENSOR_R_PIN` (o padrão `-1` significa sem sensor).
  O ideal é **GPIO34 ou 35**, que só servem de entrada e não têm strapping, mas precisam de
  um resistor de 10k para 3V3. Isso só vale se esses pinos estiverem acessíveis na placa.
  A alternativa é **GPIO5 (U5)**, que usa o pull-up interno.
- Depois de **Salvar posição atual**, o firmware reaprende a posição da marca sozinho.

---

## 5. Barulho em velocidade média (50% alto, 100% quieto)

| Slider | Passos/s (passo cheio) | Manivela |
|:------:|:----------------------:|:--------:|
| 0% | 200 | 18 rpm |
| 50% | 377 | 34 rpm |
| 75% | 678 | 61 rpm |
| 100% | 3333 | 300 rpm |

- **Causa provável: ressonância do motor de passo em passo cheio.** Em algumas centenas de
  passos/s, cada passo de 1,8° dá um "tranco" e o rotor oscila. O barulho vem daí, e nessa
  faixa o motor também **perde passo** com facilidade, o que estraga a fase. Em alta
  velocidade a inércia suaviza os trancos, por isso fica mais quieto.
- **Desconfie dos 100%.** 300 rpm na manivela equivale a 1000 rpm no motor, a 12 V, com
  metade da redução que se imaginava. "Quieto" pode significar que o motor não está
  acompanhando. Confira se a 100% o robô anda visivelmente mais rápido que a 75%. Se não
  andar, suba `INTERVALO_MIN` (por exemplo, para 300).
- **Correção de verdade: micropasso.** Divide cada passo de 1,8° em passos menores, e o
  movimento fica suave e bem mais silencioso. Precisa de hardware, porque nesta placa os
  pinos M0/M1/M2 do DRV8825 estão soltos:
  1. **Com tudo desligado**, ligue o pino **M1** de cada módulo DRV8825 ao VCC lógico. O
     pino **RST** do próprio módulo já está no VCC nesta placa (é o 2º abaixo do M1). Um fio
     fino soldado entre M1 e RST resolve.
     ⚠️ Confira com o multímetro que o RST está em 3,3/5 V, **não em 12 V** (VMOT fica do
     outro lado do módulo).
  2. No código, mude `MICROPASSO` para **4**. Velocidades, rampa e ajuste fino continuam
     iguais em rpm, porque o código compensa sozinho.
  3. Calibre de novo: a fase salva com outro micropasso é descartada de propósito.
  - Tabela do DRV8825 (M2 M1 M0): `000` cheio · `001` 1/2 · `010` **1/4** · `011` 1/8.
    Mais que 4 não é recomendado: os pulsos saem do `loop()` junto com o WiFi, e a 100%
    com 1/4 o pulso já é de 37 µs.
- **Sem mexer no hardware:** baixar um pouco o Vref também reduz o ruído, mas perde torque.
  Outra opção é evitar a faixa ruidosa com o slider (o texto abaixo dele mostra os passos/s
  para você achar essa faixa).

---

## 6. Parâmetros novos (topo do `.ino`)

| Constante | Padrão | O que é |
|-----------|:------:|---------|
| `DENTES_PINHAO` / `DENTES_COROA` | 6 / 20 | Transmissão até a manivela |
| `MICROPASSO` | 1 | Tem que bater com os jumpers M0/M1/M2 do DRV8825 (ver seção 5) |
| `defasagemGraus` | −90 | Só o rótulo da relação salva pelo botão *Salvar posição atual* |
| `PERIODO_MARCHA_GRAUS` | 360 | A cada quantos graus a marcha se repete. Só reduza se as pernas forem idênticas |
| `AUTO_SINCRONIZAR` | true | Sincroniza sozinho ao terminar um giro |
| `INTERVALO_AJUSTE` | 1200 µs | Velocidade da sincronia e do ajuste fino |
| `JOG_PASSOS` | 9 | Tamanho do passo dos botões de ajuste fino |
| `SENSOR_L_PIN` / `SENSOR_R_PIN` | −1 | Pinos dos sensores (−1 = sem sensor) |

---

## 7. Como isto foi verificado (e o que não foi)

- **Verificado:** o `.ino` compilou no PC contra stubs das bibliotecas do Arduino e rodou
  numa simulação que conta os pulsos STEP "físicos" com o DIR real de cada pino. Os
  cenários, rodados com `MICROPASSO` 1 e 4, foram: pernas giradas na mão e depois
  *Salvar posição atual* (sem mover o motor), frente, giro seguido de sincronia
  automática, giro emendando direto em frente, trás, ajuste fino, sincronia manual,
  slider baixo e alto, e STOP no meio de um giro. Em todos, as pernas voltaram à posição
  física salva (±0,2° em passo cheio, ±0,05° em 1/4), a largada nunca ficou mais rápida
  que o cruzeiro e nenhum pulso STEP ficou abaixo dos 1,9 µs exigidos pelo DRV8825.
- **Não verificado:** não foi compilado com o core ESP32 real e **não foi testado no
  robô**. Se o motor perder passo, a contagem não percebe (é malha aberta). Esse é o motivo
  do sensor.

## 8. Limitações

- **Malha aberta.** Sem sensor, a precisão depende de nenhum passo ser perdido.
- **A sincronia move o robô.** Avançar só um lado faz o robô pivotar um pouco sobre o outro.
- **Ao ligar**, o DRV8825 energiza a bobina na posição inicial e o rotor pode pular até ±2
  passos cheios (±1,1° de manivela).
- **A geração de passos continua no `loop()`,** junto com o WiFi, então pode haver jitter.
  Os dois lados recebem o mesmo pulso, o que preserva a fase, mas a velocidade pode oscilar.
