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

> ⚠️ O commit anterior fala em redução de **6,67:1**, mas 20/6 = **3,33:1**. Se existir
> outro estágio de engrenagens, ajuste `DENTES_PINHAO` e `DENTES_COROA` para a razão
> total. Se não existir, as velocidades daquele commit foram escolhidas com uma premissa
> errada e merecem ser recalibradas.

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
- **Entre os lados**, o alvo é `Δφ = φR − φL = −90°` (lado direito 90° atrás). Isso é o
  que o software controla. O valor pode ser trocado pelo app para testar +90°, caso o
  "−90" esteja invertido.

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
| **Zerar (lados iguais)** | Diz ao firmware que "as pernas dos dois lados estão na mesma posição agora". Em seguida ele aplica a defasagem alvo sozinho |
| **Defasagem** | Muda o alvo (−180 a 180°) e salva. Aperte Sincronizar depois |
| **STOP** | Para **tudo**, inclusive uma sincronia em andamento. Soltar um botão de direção **não** cancela a sincronia pós-giro |

---

## 3. Calibração (uma vez só)

1. Grave o firmware e abra o app.
2. Use **Esq ±** e **Dir ±** até que uma perna de referência (por exemplo, a dianteira de
   cada lado) esteja **na mesma posição nos dois lados**, como a perna esticada lá embaixo.
3. Aperte **Zerar** e confirme. O lado correspondente anda até ficar a −90°.
4. Ande para frente. Se a marcha ficou pior do que antes, troque a defasagem para **+90**
   e aperte **Sincronizar**.

Enquanto não calibrar, o app mostra "NAO calibrado", e o firmware assume que o robô foi
montado já na defasagem correta.

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
- Depois de **Zerar**, o firmware reaprende a posição da marca sozinho.

---

## 5. Parâmetros novos (topo do `.ino`)

| Constante | Padrão | O que é |
|-----------|:------:|---------|
| `DENTES_PINHAO` / `DENTES_COROA` | 6 / 20 | Transmissão até a manivela |
| `defasagemGraus` | −90 | Alvo inicial de φR − φL (depois fica o que foi salvo pelo app) |
| `PERIODO_MARCHA_GRAUS` | 360 | A cada quantos graus a marcha se repete. Só reduza se as pernas forem idênticas |
| `AUTO_SINCRONIZAR` | true | Sincroniza sozinho ao terminar um giro |
| `INTERVALO_AJUSTE` | 1200 µs | Velocidade da sincronia e do ajuste fino |
| `JOG_PASSOS` | 9 | Tamanho do passo dos botões de ajuste fino |
| `SENSOR_L_PIN` / `SENSOR_R_PIN` | −1 | Pinos dos sensores (−1 = sem sensor) |

---

## 6. Como isto foi verificado (e o que não foi)

- **Verificado:** o `.ino` compilou no PC contra stubs das bibliotecas do Arduino e rodou
  numa simulação que conta os pulsos STEP "físicos" com o DIR real de cada pino. Os
  cenários foram: zerar, frente, giro seguido de sincronia automática, giro emendando
  direto em frente, trás, ajuste fino, sincronia manual, troca da defasagem para +90 e
  STOP no meio da frenagem de um giro. Em todos, a fase contada bateu com os passos
  físicos, Δφ voltou ao alvo (±0,2°) e nenhum pulso STEP ficou abaixo dos 1,9 µs exigidos
  pelo DRV8825.
- **Não verificado:** não foi compilado com o core ESP32 real e **não foi testado no
  robô**. Se o motor perder passo, a contagem não percebe (é malha aberta). Esse é o motivo
  do sensor.

## 7. Limitações

- **Malha aberta.** Sem sensor, a precisão depende de nenhum passo ser perdido.
- **A sincronia move o robô.** Avançar só um lado faz o robô pivotar um pouco sobre o outro.
- **Ao ligar**, o DRV8825 energiza a bobina na posição inicial e o rotor pode pular até ±2
  passos cheios (±1,1° de manivela).
- **A geração de passos continua no `loop()`,** junto com o WiFi, então pode haver jitter.
  Os dois lados recebem o mesmo pulso, o que preserva a fase, mas a velocidade pode oscilar.
