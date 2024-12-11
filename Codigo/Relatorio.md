# Relatório
---
## Objetivo

O presente trabalho tem como objetivo implementar um mini sistema operacional para permitir a execução de mais de um programa. Nesse SO, será implementado: suporte a processos com multiprogramação, três escalonadores diferentes, chamadas de sistema, bloqueio de processos por entrada e saída e para esperar a morte de outro, para melhorar o uso da CPU e preempção de processos, para melhorar a distribuição da CPU. Isso tudo para comparar os resultados obtidos nas três configurações de escalonadores: simples, circular e com prioridade.

---
## Metodologia

Para cumprir os objetivos desse trabalho, foi desenvolvido um programa em cima do simulador de computador disponibilizado pelo professor da disciplina. Esse programa implementa todos os requisitos e gera um relatório final com as estatísticas requeridas após a morte de todos os processos envolvidos. Para a implementação do suporte a processos, foi criada a estrutura ```processo_t```, que salva o estado dos registradores do processo, além de informações acerca de um eventual bloqueio. Essa estrutura também aponta para uma ```porta_t``` que contém informações sobre as portas que o teclado utiliza (tela e teclado). Ainda, a estrutura do processo aponta para uma ```proc_metricas```, a qual salva as métricas de cada processo.

Foram implementadas três estratégias de escalonamento:
1. **Escalonamento simples:** O escalonador escolhe o primeiro processo pronto da fila e o executa até o seu bloqueio ou morte. Essa estratégia não leva em conta a prioridade dos processos nem a preempção.

2. **Escalonamento circular:** O escalonador escolhe, inicialmente, o primeiro processo pronto da fila, que ao perder a CPU, ao estourar o *quantum*, um intervalo de instruções pré-definido, é movido para o final da fila. Essa estratégia distribui a CPU igualmente entre os processos.

3. **Escalonamento prioritário:** O escalonador escolhe o processo pronto com maior prioridade. Isso demanda um mecanismo que ajuste dinamicamente a prioridade de cada processo quando o mesmo perde a CPU, seja por bloqueio ou estouro do *quantum*. Isso faz com que processos que bloqueiam mais rapidamente, tenham uma maior prioridade, pois não estouram com frequência o *quantum*.
---
## Testes realizados

Realizou-se testes em diferentes cenários para medir o desempenho das estratégias de escalonamento. Para isso, variou-se o intervalo de interrupção do relógio bem como do valor do *quantum*. As métricas incluem:
- número de processos criados
- tempo total de execução
- tempo total em que o sistema ficou ocioso (todos os processos bloqueados)
- número de interrupções recebidas de cada tipo
- número de preempções
- tempo de retorno de cada processo (diferença entre data do término e da criação)
- número de preempções de cada processo
- número de vezes que cada processo entrou em cada estado (pronto, bloqueado, executando)
- tempo total de cada processo em cada estado (pronto, bloqueado, executando)
- tempo médio de resposta de cada processo (tempo médio em estado pronto)

---

### Cenários com o escalonador simples

#### INTERVALO_INTERRUPCAO = 50
Com o ```INTERVALO_INTERRUPCAO = 50```, a CPU recebe uma interrupção do relógio a cada 50 instruções executadas. Os resultados foram:

| $N_{Processos}$ | $T_{Execução}$ | $T_{Ocioso}$ | $N_{Preempções}$ |
|-----------------|----------------|--------------|------------------|
| 4               | 27961          | 8611         | 0                |

| Interrupção | $N_{Vezes}$ |
|-------------|-------------|
| Reset       | 1           |
| Erro de execução | 0      |
| Chamada de sistema | 460  |
| E/S: relógio | 558        |
| E/S: teclado | 0          |
| E/S: console | 0          |

| PID | $N_{Preempções}$ | $T_{Retorno}$ | $T_{Resposta}$ |
|-----|------------------|---------------|----------------|
| 0   | 0                | 68            | 10            | 
| 1   | 0                | 285           | 587             |
| 2   | 0                | 214           | 559            |
| 3   | 0                | 558           | 94             |

| PID | $N_{Morto}$ | $N_{Pronto}$ | $N_{Bloqueado}$ | $N_{Executando}$ |
|-----|-------------|--------------|-----------------|------------------|
| 0   | 1           | 4           | 3               | 61               |
| 1   | 1           | 7          | 6               | 272              |
| 2   | 1           | 19           | 18              | 177              |
| 3   | 1           | 132          | 131             | 295              |

| PID | $T_{Morto}$ | $T_{Pronto}$ | $T_{Bloqueado}$ | $T_{Executando}$ |
|-----|-------------|--------------|-----------------|------------------|
| 0   | 18          | 40         | 27160           | 743              |
| 1   | 13840        | 4109         | 470             | 9159             |
| 2   | 12037        | 10633         | 1170            | 3730             |
| 3   | 337         | 12483         | 9024            | 5718             |

#### INTERVALO_INTERRUPCAO = 20
Em um segundo momento, diminui-se o intervalo para a interrupção do relógio, objetivando liberar um processo do bloqueio para a escrita mais rapidamente, quando a tela ficasse livre, uma vez que um intervalo mais longo podia deixar o processo bloqueado mais tempo mesmo com a tela pronta. Os resultados foram:

| $N_{Processos}$ | $T_{Execução}$ | $T_{Ocioso}$ | $N_{Preempções}$ |
|-----------------|----------------|--------------|------------------|
| 4               | 27053          | 5930          | 0                |

| Interrupção | $N_{Vezes}$ |
|-------------|-------------|
| Reset       | 1           |
| Erro de execução | 0      |
| Chamada de sistema | 460  |
| E/S: relógio | 1338       |
| E/S: teclado | 0          |
| E/S: console | 0          |

| PID | $N_{Preempções}$ | $T_{Retorno}$ | $T_{Resposta}$ |
|-----|------------------|---------------|----------------|
| 0   | 0                | 94           | 10             | 
| 1   | 0                | 603          | 785             |
| 2   | 0                | 341           | 685             |
| 3   | 0                | 674           | 103             |

| PID | $N_{Morto}$ | $N_{Pronto}$ | $N_{Bloqueado}$ | $N_{Executando}$ |
|-----|-------------|--------------|-----------------|------------------|
| 0   | 1           | 4           | 3               | 87               |
| 1   | 1           | 6          | 5               | 592              |
| 2   | 1           | 18           | 17              | 306              |
| 3   | 1           | 132          | 131             | 411              |

| PID | $T_{Morto}$ | $T_{Pronto}$ | $T_{Bloqueado}$ | $T_{Executando}$ |
|-----|-------------|--------------|-----------------|------------------|
| 0   | 15           | 43         | 26174           | 821              |
| 1   | 11512         | 4713        | 287             | 10119            |
| 2   | 9703        | 11845         | 955             | 4117             |
| 3   | 370        | 13724         | 6452            | 6066             |

Notou-se uma melhora significativa no tempo ocioso da CPU, ao diminuir-se o intervalo de interrupção. Ainda, os tempos de resposta de todos os processos diminuíram. Isso demonstra que muito provavelmente, com um tempo maior, o processo que já poderia ser desbloqueado, acabava ficando bloqueado até que a interrupção do relógio viesse, deixando a CPU parada desnecessariamente.

---

### Cenários com o escalonador circular

No escalonador circular, com ```INTERVALO_INTERRUPCAO = 20``` e ```QUANTUM = 5```, obteve-se os resultados:

| $N_{Processos}$ | $T_{Execução}$ | $T_{Ocioso}$ | $N_{Preempções}$ |
|-----------------|----------------|--------------|------------------|
| 4               | 21886          | 412          | 286              |

| Interrupção | $N_{Vezes}$ |
|-------------|-------------|
| Reset       | 1           |
| Erro de execução | 0      |
| Chamada de sistema | 460  |
| E/S: relógio | 1080       |
| E/S: teclado | 0          |
| E/S: console | 0          |

| PID | $N_{Preempções}$ | $T_{Retorno}$ | $T_{Resposta}$ |
|-----|------------------|---------------|----------------|
| 0   | 0                | 155           | 1             | 
| 1   | 176                | 857          | 55            |
| 2   | 32                | 358           | 152           |
| 3   | 16                | 645           | 83            |

| PID | $N_{Morto}$ | $N_{Pronto}$ | $N_{Bloqueado}$ | $N_{Executando}$ |
|-----|-------------|--------------|-----------------|------------------|
| 0   | 1           | 66           | 3               | 86               |
| 1   | 1           | 185          | 8               | 664              |
| 2   | 1           | 45           | 12              | 301              |
| 3   | 1           | 100          | 83              | 462              |

| PID | $T_{Morto}$ | $T_{Pronto}$ | $T_{Bloqueado}$ | $T_{Executando}$ |
|-----|-------------|--------------|-----------------|------------------|
| 0   | 7           | 83           | 20978           | 818              |
| 1   | 376         | 10243        | 510             | 10335            |
| 2   | 9645        | 6846         | 780             | 4102             |
| 3   | 2404        | 8330         | 4412            | 6219             |

Nesse cenário, o tempo de execução ficou similar ao do escalonador simples, porém houve uma alternancia maior dos processos na CPU, com o uso da preempção. Como era de se esperar, o P1 sofreu o maior número de preempções, enquanto o P3 sofreu o menor. Porém, como aqui não levamos em conta a prioridade, o P1 não foi muito "punido" ainda.

---

### Cenários com o escalonador prioritário

#### INTERVALO_INTERRUPCAO = 50 e QUANTUM = 5

Seguindo o padrão dos testes anteriores, definiu-se o ```INTERVALO_INTERRUPCAO = 50``` e ```QUANTUM = 5```. Obteve-se os resultados:

baixo nro preempcoes, tempo ocioso grande devido ao intervalo interrupcao
tempo de retorno diminuiu dos procs, mas t resposta aumentou

| $N_{Processos}$ | $T_{Execução}$ | $T_{Ocioso}$ | $N_{Preempções}$ |
|-----------------|----------------|--------------|------------------|
| 4               | 22063          | 2632         | 84               |

| Interrupção | $N_{Vezes}$ |
|-------------|-------------|
| Reset       | 1           |
| Erro de execução | 0      |
| Chamada de sistema | 460  |
| E/S: relógio | 440       |
| E/S: teclado | 0          |
| E/S: console | 0          |

| PID | $N_{Preempções}$ | $T_{Retorno}$ | $T_{Resposta}$ |
|-----|------------------|---------------|----------------|
| 0   | 0               | 85            | 17             | 
| 1   | 49               | 365           | 142            |
| 2   | 13               | 225           | 170            |
| 3   | 5                | 480           | 100            |

| PID | $N_{Morto}$ | $N_{Pronto}$ | $N_{Bloqueado}$ | $N_{Executando}$ |
|-----|-------------|--------------|-----------------|------------------|
| 0   | 1           | 21           | 3               | 61               |
| 1   | 1           | 58           | 8               | 290              |
| 2   | 1           | 26           | 12              | 187              |
| 3   | 1           | 96           | 90               | 294              |

| PID | $T_{Morto}$ | $T_{Pronto}$ | $T_{Bloqueado}$ | $T_{Executando}$ |
|-----|-------------|--------------|-----------------|------------------|
| 0   | 18          | 356          | 20946           | 743              |
| 1   | 3587        | 8256         | 624             | 9213             |
| 2   | 12245       | 4436         | 875             | 3760             |
| 3   | 337         | 9673         | 5583            | 5715             |

Nota-se uma diminuição no número de preempções devido ao intervalo grande de interrupção do relógio, evidenciado pela diminuição do número de IRQ do relógio. Isso leva a um grande aumento na ociosidade da CPU. O tempo de retorno dos processos diminuiu, porém o tempo de retorno aumentou. 

#### INTERVALO_INTERRUPCAO = 25 e QUANTUM = 5

Aqui, diminui-se pela metade o intervalo de interrupção, obteve-se as métricas:

| $N_{Processos}$ | $T_{Execução}$ | $T_{Ocioso}$ | $N_{Preempções}$ |
|-----------------|----------------|--------------|------------------|
| 4               | 21304          | 526          | 273               |

| Interrupção | $N_{Vezes}$ |
|-------------|-------------|
| Reset       | 1           |
| Erro de execução | 0      |
| Chamada de sistema | 460  |
| E/S: relógio | 848       |
| E/S: teclado | 0          |
| E/S: console | 0          |

| PID | $N_{Preempções}$ | $T_{Retorno}$ | $T_{Resposta}$ |
|-----|------------------|---------------|----------------|
| 0   | 0                | 136           | 7              | 
| 1   | 182              | 705           | 53             |
| 2   | 28               | 326           | 98             |
| 3   | 12               | 611           | 68             |

| PID | $N_{Morto}$ | $N_{Pronto}$ | $N_{Bloqueado}$ | $N_{Executando}$ |
|-----|-------------|--------------|-----------------|------------------|
| 0   | 1           | 55           | 3               | 78               |
| 1   | 1           | 190          | 7               | 508              |
| 2   | 1           | 41           | 12              | 273              |
| 3   | 1           | 101           | 88              | 422              |

| PID | $T_{Morto}$ | $T_{Pronto}$ | $T_{Bloqueado}$ | $T_{Executando}$ |
|-----|-------------|--------------|-----------------|------------------|
| 0   | 9           | 415          | 20086           | 794              |
| 1   | 366         | 10101        | 560             | 9867             |
| 2   | 11605       | 4055         | 793             | 4018             |
| 3   | 3247        | 6939         | 4175            | 6099             |

Nota-se uma diminuição de 80% na ociosidade, devido à diminuição do intervalo de interrupção. Os tempos de resposta também sofreram uma significativa diminuição, mostrando que um processo não fica muito tempo parado estando pronto. Isso indica uma configuração mais eficiente que, levando em conta as preempções, ajusta dinamicamente as prioridades, deixando os processos que bloqueiam sem estourar o quantum executarem novamente primeiro, como o P3.