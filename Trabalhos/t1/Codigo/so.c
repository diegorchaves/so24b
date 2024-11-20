// so.c
// sistema operacional
// simulador de computador
// so24b

// INCLUDES {{{1
#include "so.h"
#include "dispositivos.h"
#include "irq.h"
#include "programa.h"
#include "instrucao.h"
#include "processo.h"

#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>

// METRICAS E DADOS
int QUANTIDADE_PROC_CRIADOS = 0;
int TEMPO_TOTAL_EXEC = 0;
int TEMPO_OCIOSO = 0;
int PREEMPCOES = 0;
int NUMERO_INTERRUPCOES_TIPO[N_IRQ] = {0};

// CONSTANTES E TIPOS {{{1
// intervalo entre interrupções do relógio
#define INTERVALO_INTERRUPCAO 20 // em instruções executadas

#define QUANTIDADE_PROCESSOS 4
#define QUANTUM 5

int PID_GLOBAL = 0;

struct so_t
{
  cpu_t *cpu;
  mem_t *mem;
  es_t *es;
  console_t *console;
  bool erro_interno;
  // t1: tabela de processos, processo corrente, pendências, etc
  processo_t tabela_processos[QUANTIDADE_PROCESSOS];
  processo_t *processo_corrente;

  porta_t tabela_portas[QUANTIDADE_PROCESSOS];

  processo_t *fila_processos;
  int quantum;

  int so_relogio;
};

// função de tratamento de interrupção (entrada no SO)
static int so_trata_interrupcao(void *argC, int reg_A);

// funções auxiliares
// carrega o programa contido no arquivo na memória do processador; retorna end. inicial
static int so_carrega_programa(so_t *self, char *nome_do_executavel);
// copia para str da memória do processador, até copiar um 0 (retorna true) ou tam bytes
static bool copia_str_da_mem(int tam, char str[tam], mem_t *mem, int ender);

// minhas funcoes
static void trata_pendencia_esc(so_t *self, processo_t *processo);
static void trata_pendencia_le(so_t *self, processo_t *processo);
static void trata_pendencia_espera(so_t *self, processo_t *processo);
static void inicializa_proc(so_t *self, processo_t *processo, int ender_carga);
static void desarma_relogio(so_t *self);
static void so_atualiza_metricas(so_t *self, int delta);
static void so_sincroniza(so_t *self);
static void muda_estado_proc(so_t *self, processo_t *processo, int novo_estado);
char *nome_estado(int estado);

// CRIAÇÃO {{{1

err_t inicializa_tabela_processos(so_t *self)
{
  for (int i = 0; i < QUANTIDADE_PROCESSOS; i++)
  {
    self->tabela_processos[i].estado_processo = ESTADO_PROC_MORTO;

    self->tabela_processos[i].porta_processo = NULL;
  }
  return ERR_OK;
}

err_t inicializa_tabela_portas(so_t *self)
{
  for (int i = 0; i < QUANTIDADE_PROCESSOS; i++)
  {
    self->tabela_portas[i].porta_ocupada = false;
  }
  return ERR_OK;
}

static void so_atualiza_metricas_proc(so_t *self, int delta)
{
  for(int i = 0; i < QUANTIDADE_PROC_CRIADOS; i++)
  {
    processo_t *p = &self->tabela_processos[i];

    p->metricas->estado_t_total[p->estado_processo] += delta;
  }
}

static void so_atualiza_metricas(so_t *self, int delta)
{
  TEMPO_TOTAL_EXEC += delta;
  
  if (self->processo_corrente == NULL) {
    TEMPO_OCIOSO += delta;
  }

  so_atualiza_metricas_proc(self, delta);
}

char *nome_estado(int estado)
{
  switch(estado)
  {
    case ESTADO_PROC_BLOQUEADO:
      return "bloqueado";
    case ESTADO_PROC_EXECUTANDO:
      return "executando";
    case ESTADO_PROC_MORTO:
      return "morto";
    case ESTADO_PROC_PRONTO:
      return "pronto";
    default:
      return "desconhecido";
  }
}

static void so_sincroniza(so_t *self)
{
  int so_relogio_anterior = self->so_relogio;

  if (es_le(self->es, D_RELOGIO_INSTRUCOES, &self->so_relogio) != ERR_OK)
  {
    console_printf("SO: problema na leitura do relógio");
    return;
  }

  if (so_relogio_anterior == -1)
  {
    return;
  }

  int delta = self->so_relogio - so_relogio_anterior;

  so_atualiza_metricas(self, delta);
}

so_t *so_cria(cpu_t *cpu, mem_t *mem, es_t *es, console_t *console)
{
  so_t *self = malloc(sizeof(*self));
  if (self == NULL)
    return NULL;

  self->cpu = cpu;
  self->mem = mem;
  self->es = es;
  self->console = console;
  self->erro_interno = false;

  self->so_relogio = -1;

  if (inicializa_tabela_processos(self) != ERR_OK)
  {
    console_printf("SO: falha ao inicializar a tabela de processos");
    self->erro_interno = true;
  }

  if (inicializa_tabela_portas(self) != ERR_OK)
  {
    console_printf("SO: falha ao inicializar a tabela de portas");
    self->erro_interno = true;
  }

  // quando a CPU executar uma instrução CHAMAC, deve chamar a função
  //   so_trata_interrupcao, com primeiro argumento um ptr para o SO
  cpu_define_chamaC(self->cpu, so_trata_interrupcao, self);

  // coloca o tratador de interrupção na memória
  // quando a CPU aceita uma interrupção, passa para modo supervisor,
  //   salva seu estado à partir do endereço 0, e desvia para o endereço
  //   IRQ_END_TRATADOR
  // colocamos no endereço IRQ_END_TRATADOR o programa de tratamento
  //   de interrupção (escrito em asm). esse programa deve conter a
  //   instrução CHAMAC, que vai chamar so_trata_interrupcao (como
  //   foi definido acima)
  int ender = so_carrega_programa(self, "trata_int.maq");
  if (ender != IRQ_END_TRATADOR)
  {
    console_printf("SO: problema na carga do programa de tratamento de interrupção");
    self->erro_interno = true;
  }

  // programa o relógio para gerar uma interrupção após INTERVALO_INTERRUPCAO
  if (es_escreve(self->es, D_RELOGIO_TIMER, INTERVALO_INTERRUPCAO) != ERR_OK)
  {
    console_printf("SO: problema na programação do timer");
    self->erro_interno = true;
  }

  return self;
}

void so_destroi(so_t *self)
{
  cpu_define_chamaC(self->cpu, NULL, NULL);
  free(self);
}

// TRATAMENTO DE INTERRUPÇÃO {{{1

// funções auxiliares para o tratamento de interrupção
static void so_salva_estado_da_cpu(so_t *self);
static void so_trata_irq(so_t *self, int irq);
static void so_trata_pendencias(so_t *self);
static void so_escalona(so_t *self);
static int so_despacha(so_t *self);

// função a ser chamada pela CPU quando executa a instrução CHAMAC, no tratador de
//   interrupção em assembly
// essa é a única forma de entrada no SO depois da inicialização
// na inicialização do SO, a CPU foi programada para chamar esta função para executar
//   a instrução CHAMAC
// a instrução CHAMAC só deve ser executada pelo tratador de interrupção
//
// o primeiro argumento é um ponteiro para o SO, o segundo é a identificação
//   da interrupção
// o valor retornado por esta função é colocado no registrador A, e pode ser
//   testado pelo código que está após o CHAMAC. No tratador de interrupção em
//   assembly esse valor é usado para decidir se a CPU deve retornar da interrupção
//   (e executar o código de usuário) ou executar PARA e ficar suspensa até receber
//   outra interrupção
static int so_trata_interrupcao(void *argC, int reg_A)
{
  so_t *self = argC;
  irq_t irq = reg_A;
  // esse print polui bastante, recomendo tirar quando estiver com mais confiança
  // console_printf("SO: recebi IRQ %d (%s)", irq, irq_nome(irq));
  // salva o estado da cpu no descritor do processo que foi interrompido
  so_salva_estado_da_cpu(self);
  // faz o atendimento da interrupção

  so_sincroniza(self);

  so_trata_irq(self, irq);
  // faz o processamento independente da interrupção
  so_trata_pendencias(self);
  // escolhe o próximo processo a executar
  so_escalona(self);
  // recupera o estado do processo escolhido
  return so_despacha(self);
}

static void so_salva_estado_da_cpu(so_t *self)
{
  // t1: salva os registradores que compõem o estado da cpu no descritor do
  //   processo corrente. os valores dos registradores foram colocados pela
  //   CPU na memória, nos endereços IRQ_END_*
  // se não houver processo corrente, não faz nada
  bool deu_erro;

  if (self->processo_corrente == NULL || self->processo_corrente->estado_processo != ESTADO_PROC_EXECUTANDO)
  {
    return;
  }
  else
  {
    if (mem_le(self->mem, IRQ_END_A, &self->processo_corrente->reg_A) != ERR_OK)
    {
      deu_erro = true;
    }
    if (mem_le(self->mem, IRQ_END_X, &self->processo_corrente->reg_X) != ERR_OK)
    {
      deu_erro = true;
    }
    if (mem_le(self->mem, IRQ_END_PC, &self->processo_corrente->reg_PC) != ERR_OK)
    {
      deu_erro = true;
    }
    if (mem_le(self->mem, IRQ_END_erro, &self->processo_corrente->reg_erro) != ERR_OK)
    {
      deu_erro = true;
    }
    if (mem_le(self->mem, IRQ_END_complemento, &self->processo_corrente->reg_complemento) != ERR_OK)
    {
      deu_erro = true;
    }
    if (mem_le(self->mem, IRQ_END_modo, &self->processo_corrente->modo) != ERR_OK)
    {
      deu_erro = true;
    }

    if (deu_erro)
    {
      console_printf("SO: erro ao salvar o estado da CPU");
      self->erro_interno = true;
    }
  }
}

static void so_trata_pendencias(so_t *self)
{

  for (int i = 0; i < QUANTIDADE_PROCESSOS; i++)
  {
    processo_t *processo = &self->tabela_processos[i];
    if (processo->estado_processo == ESTADO_PROC_BLOQUEADO)
    {
      switch (processo->bloqueio_motivo)
      {
      case BLOQUEIO_ESC:
        trata_pendencia_esc(self, processo);
        break;

      case BLOQUEIO_LE:
        trata_pendencia_le(self, processo);
        break;

      case BLOQUEIO_ESPERA:
        trata_pendencia_espera(self, processo);
        break;

      default:
        console_printf("Motivo de bloqueio desconhecido.");
        self->erro_interno = true;
        break;
      }
    }
  }
}

static void atualiza_prioridade(so_t *self, processo_t *processo)
{
  double t_exec = QUANTUM - self->quantum;
  double prio = (processo->prioridade + (t_exec / QUANTUM)) / 2;

  processo->prioridade = prio;
}

static int so_precisa_escalonar(so_t *self)
{
  if (self->processo_corrente == NULL)
    return 1;

  if (self->processo_corrente->estado_processo == ESTADO_PROC_MORTO)
    return 1;

  if (self->processo_corrente->estado_processo == ESTADO_PROC_BLOQUEADO)
  {
    console_printf("vou escalonar pq bloqueou");
    console_printf("prio: %lf -> ", self->processo_corrente->prioridade);
    atualiza_prioridade(self, self->processo_corrente);
    console_printf("%lf", self->processo_corrente->prioridade);
    return 1;
  }

  if (self->quantum <= 0)
  {
    PREEMPCOES++;
    self->processo_corrente->metricas->n_preempcoes += 1;
    console_printf("vou escalonar pq quantum < 0");
    //self->processo_corrente->estado_processo = ESTADO_PROC_PRONTO;
    muda_estado_proc(self, self->processo_corrente, ESTADO_PROC_PRONTO);
    console_printf("prio: %lf -> ", self->processo_corrente->prioridade);
    atualiza_prioridade(self, self->processo_corrente);
    console_printf("%lf", self->processo_corrente->prioridade);
    return 1;
  }

  return 0;
}

static void bloqueia_processo(so_t *self, processo_t *processo, bloqueio_id motivo, int pid_esperado)
{
  if (processo->estado_processo != ESTADO_PROC_BLOQUEADO && processo->estado_processo != ESTADO_PROC_MORTO)
  {
    //processo->estado_processo = ESTADO_PROC_BLOQUEADO;
    muda_estado_proc(self, processo, ESTADO_PROC_BLOQUEADO);
    processo->bloqueio_motivo = motivo;

    if (motivo == BLOQUEIO_ESPERA)
    {
      processo->pid_esperado = pid_esperado;
    }
  }
}

processo_t *pega_maior_prioridade(processo_t *fila_processos)
{
  double menor = 999;
  processo_t *andarilho = fila_processos;
  processo_t *retorno = NULL;
  while (andarilho != NULL)
  {
    if (andarilho->prioridade < menor)
    {
      retorno = andarilho;
      menor = andarilho->prioridade;
    }
    andarilho = andarilho->prox_processo;
  }
  return retorno;
}

static void desbloqueia_processo(so_t *self, processo_t *processo)
{
  if (processo->estado_processo == ESTADO_PROC_BLOQUEADO)
  {
    //processo->estado_processo = ESTADO_PROC_PRONTO;
    muda_estado_proc(self, processo, ESTADO_PROC_PRONTO);
  }
}

static void tenta_ler(so_t *self, processo_t *processo, int chamada_sistema)
{

  int estado;
  int teclado_estado = processo->porta_processo->teclado_estado;
  int teclado = processo->porta_processo->teclado;

  if (es_le(self->es, teclado_estado, &estado) != ERR_OK)
  {
    console_printf("SO: problema no acesso ao estado do teclado");
    self->erro_interno = true;
    return;
  }

  if (estado == 0 && chamada_sistema == 1)
  {
    bloqueia_processo(self, processo, BLOQUEIO_LE, 0);
  }
  else
  {
    int dado;
    if (es_le(self->es, teclado, &dado) != ERR_OK)
    {
      console_printf("SO: problema no acesso ao teclado");
      self->erro_interno = true;
      return;
    }
    processo->reg_A = dado;

    desbloqueia_processo(self, processo);
  }
}

static void tenta_escrever(so_t *self, processo_t *processo, int chamada_sistema)
{
  int estado;
  int tela_estado = processo->porta_processo->tela_estado;
  int tela = processo->porta_processo->tela;

  if (es_le(self->es, tela_estado, &estado) != ERR_OK)
  {
    console_printf("SO: problema no acesso ao estado da tela");
    self->erro_interno = true;
    return;
  }

  if (estado == 0 && chamada_sistema == 1)
  {
    bloqueia_processo(self, processo, BLOQUEIO_ESC, 0);
  }
  else
  {
    int dado = processo->reg_X;
    if (es_escreve(self->es, tela, dado) != ERR_OK)
    {
      console_printf("SO: problema no acesso a tela");
      // self->erro_interno = true;
      return;
    }
    desbloqueia_processo(self, processo);

    if (chamada_sistema)
    {
      processo->reg_A = 0;
    }
  }
}

static void trata_pendencia_le(so_t *self, processo_t *processo)
{
  tenta_ler(self, processo, false);
}

static void trata_pendencia_esc(so_t *self, processo_t *processo)
{
  tenta_escrever(self, processo, false);
}

static void trata_pendencia_espera(so_t *self, processo_t *processo)
{
  for (int i = 0; i < QUANTIDADE_PROCESSOS; i++)
  {
    processo_t *processo_esperado = &self->tabela_processos[i];
    if (processo_esperado->pid_processo == processo->pid_esperado && processo_esperado->estado_processo == ESTADO_PROC_MORTO)
    {
      desbloqueia_processo(self, processo);
      console_printf("Desbloquando processo porque o esperado de PID %d morreu.", processo_esperado->pid_processo);
    }
  }
}

static void coloca_processo_fila(so_t *self, processo_t *processo)
{
  // insere no final da fila (circular)

  if (self->fila_processos == NULL)
  {
    self->fila_processos = processo;
  }
  else
  {
    processo_t *andarilho = self->fila_processos;
    while (andarilho->prox_processo != NULL)
    {
      andarilho = andarilho->prox_processo;
    }
    andarilho->prox_processo = processo;
  }
  processo->prox_processo = NULL;

  // insere no inicio da fila (simples ou prioritario)
  /* if (self->fila_processos == NULL)
    processo->prox_processo = NULL;
  else
    processo->prox_processo = self->fila_processos;

  self->fila_processos = processo; */
}

static void imprime_tabela_processos(so_t *self)
{
  for (int i = 0; i < QUANTIDADE_PROCESSOS; i++)
  {
    processo_t *p = &self->tabela_processos[i];
    console_printf("pid %d estado %s", p->pid_processo, nome_estado(p->estado_processo));
  }
}

/* static err_t atualiza_fila(so_t *self)
{
  self->fila_processos = NULL; // reseta a fila

  for (int i = 0; i < QUANTIDADE_PROCESSOS; i++)
  {
    processo_t *p = &self->tabela_processos[i];
    if (p->estado_processo == ESTADO_PROC_PRONTO)
    {
      coloca_processo_fila(self, p);
    }
  }

  return ERR_OK;
} */

static void so_escalona(so_t *self)
{
  imprime_tabela_processos(self);
  // escolhe o próximo processo a executar, que passa a ser o processo
  //   corrente; pode continuar sendo o mesmo de antes ou não
  // t1: na primeira versão, escolhe um processo caso o processo corrente não possa continuar
  //   executando. depois, implementar escalonador melhor
  processo_t *ant;

  if (!so_precisa_escalonar(self))
  {
    return;
  }
  else
  {
    ant = self->processo_corrente;

    /* if (atualiza_fila(self) != ERR_OK)
    {
      self->erro_interno = true;
      return;
    }
    else
    { */
      //self->processo_corrente = pega_maior_prioridade(self->fila_processos);    // escalonador prioritario
    self->processo_corrente = self->fila_processos;   // pega o primeiro da fila (escalonador circular / simples)
    /* } */
  }

  if (self->processo_corrente != ant)
  { // se mudou o processo em execucao, reseta o quantum
    self->quantum = QUANTUM;
  }
}

static int so_despacha(so_t *self)
{
  console_printf("quantum = %d", self->quantum);
  // t1: se houver processo corrente, coloca o estado desse processo onde ele
  //   será recuperado pela CPU (em IRQ_END_*) e retorna 0, senão retorna 1
  // o valor retornado será o valor de retorno de CHAMAC
  bool deu_erro;

  if (self->erro_interno || self->processo_corrente == NULL)
  {
    console_printf("deu ruim na 1 verificacao do despacha, erro interno %d", self->erro_interno);
    if (self->processo_corrente != NULL)
    {
      console_printf("proc corrente %d estado %d", self->processo_corrente->pid_processo, self->processo_corrente->estado_processo);
    }
    return 1;
  }
  else
  {
    if (mem_escreve(self->mem, IRQ_END_A, self->processo_corrente->reg_A) != ERR_OK)
    {
      deu_erro = true;
    }
    if (mem_escreve(self->mem, IRQ_END_X, self->processo_corrente->reg_X) != ERR_OK)
    {
      deu_erro = true;
    }
    if (mem_escreve(self->mem, IRQ_END_PC, self->processo_corrente->reg_PC) != ERR_OK)
    {
      deu_erro = true;
    }
    if (mem_escreve(self->mem, IRQ_END_erro, self->processo_corrente->reg_erro) != ERR_OK)
    {
      deu_erro = true;
    }
    if (mem_escreve(self->mem, IRQ_END_complemento, self->processo_corrente->reg_complemento) != ERR_OK)
    {
      deu_erro = true;
    }
    if (mem_escreve(self->mem, IRQ_END_modo, self->processo_corrente->modo) != ERR_OK)
    {
      deu_erro = true;
    }

    if (deu_erro)
    {
      return 1;
    }
    else
    {
      console_printf("proc corrente %d", self->processo_corrente->pid_processo);
      muda_estado_proc(self, self->processo_corrente, ESTADO_PROC_EXECUTANDO);
      return 0; // deu tudo certo
    }
  }
}

// TRATAMENTO DE UMA IRQ {{{1

// funções auxiliares para tratar cada tipo de interrupção
static void so_trata_irq_reset(so_t *self);
static void so_trata_irq_chamada_sistema(so_t *self);
static void so_trata_irq_err_cpu(so_t *self);
static void so_trata_irq_relogio(so_t *self);
static void so_trata_irq_desconhecida(so_t *self, int irq);

static void so_trata_irq(so_t *self, int irq)
{
  NUMERO_INTERRUPCOES_TIPO[irq] += 1;
  // verifica o tipo de interrupção que está acontecendo, e atende de acordo
  switch (irq)
  {
  case IRQ_RESET:
    so_trata_irq_reset(self);
    break;
  case IRQ_SISTEMA:
    so_trata_irq_chamada_sistema(self);
    break;
  case IRQ_ERR_CPU:
    so_trata_irq_err_cpu(self);
    break;
  case IRQ_RELOGIO:
    so_trata_irq_relogio(self);
    break;
  default:
    so_trata_irq_desconhecida(self, irq);
  }
}

static void so_trata_irq_reset(so_t *self)
{
  // t1: deveria criar um processo para o init, e inicializar o estado do
  //   processador para esse processo com os registradores zerados, exceto
  //   o PC e o modo.
  // como não tem suporte a processos, está carregando os valores dos
  //   registradores diretamente para a memória, de onde a CPU vai carregar
  //   para os seus registradores quando executar a instrução RETI

  // coloca o programa init na memória
  int ender = so_carrega_programa(self, "init.maq");
  if (ender != 100)
  {
    console_printf("SO: problema na carga do programa inicial");
    self->erro_interno = true;
    return;
  }

  for (int i = 0; i < QUANTIDADE_PROCESSOS; i++)
  {
    processo_t *processo = &self->tabela_processos[i];
    if (processo->estado_processo == ESTADO_PROC_MORTO)
    {

      inicializa_proc(self, processo, ender);

      if (processo->porta_processo == NULL)
      {
        console_printf("Impossivel atribuir terminal ao processo.");
        self->erro_interno = true;
      }
      // self->processo_corrente = processo;
      QUANTIDADE_PROC_CRIADOS++;
      return;
    }
  }
}

static void so_trata_irq_err_cpu(so_t *self)
{
  // Ocorreu um erro interno na CPU
  // O erro está codificado em IRQ_END_erro
  // Em geral, causa a morte do processo que causou o erro
  // Ainda não temos processos, causa a parada da CPU
  int err_int = self->processo_corrente->reg_erro;
  // t1: com suporte a processos, deveria pegar o valor do registrador erro
  //   no descritor do processo corrente, e reagir de acordo com esse erro
  //   (em geral, matando o processo)
  // mem_le(self->mem, IRQ_END_erro, &err_int);
  err_t err = err_int;
  console_printf("SO: IRQ não tratada -- erro na CPU: %s", err_nome(err));
  // self->erro_interno = true;
  console_printf("Matando o processo...PID %d", self->processo_corrente->pid_processo);
  //self->processo_corrente->estado_processo = ESTADO_PROC_MORTO;
  muda_estado_proc(self, self->processo_corrente, ESTADO_PROC_MORTO);
}

/* static void imprime_metricas(so_t *self)
{
  console_printf("QUANTIDADE PROC CRIADOS: %d", QUANTIDADE_PROC_CRIADOS);
  console_printf("TEMPO TOTAL EXEC: %d", TEMPO_TOTAL_EXEC);
  console_printf("TEMPO OCIOSO: %d", TEMPO_OCIOSO);
  for(int i = 0; i < N_IRQ; i++)
  {
    console_printf("%s: %d", irq_nome(i), NUMERO_INTERRUPCOES_TIPO[i]);
  }

  for(int i = 0; i < QUANTIDADE_PROC_CRIADOS; i++)
  {
    processo_t *p = &self->tabela_processos[i];
    proc_metricas *met = p->metricas;
    console_printf("PID: %d", p->pid_processo);

    for(int j = 1; j < QUANTIDADE_ESTADOS_PROC; j++)
    {
      console_printf("Estado %s: %d vezes", nome_estado(j), met->estado_n_vezes[j]);
      console_printf("Estado %s: %d inst", nome_estado(j), met->estado_t_total[j]);
    }
  }
} */

static void imprime_metricas(so_t *self)
{
    FILE *arquivo = fopen("metricas-circular-int20.txt", "w");
    if (arquivo == NULL) {
        console_printf("Erro ao abrir o arquivo para escrita.\n");
        return;
    }

    fprintf(arquivo, "QUANTIDADE PROC CRIADOS: %d\n", QUANTIDADE_PROC_CRIADOS);
    fprintf(arquivo, "TEMPO TOTAL EXEC: %d\n", TEMPO_TOTAL_EXEC);
    fprintf(arquivo, "TEMPO OCIOSO: %d\n", TEMPO_OCIOSO);
    fprintf(arquivo, "PREEMPCOES: %d\n", PREEMPCOES);

    
    for (int i = 0; i < N_IRQ; i++) {
        fprintf(arquivo, "%s: %d\n", irq_nome(i), NUMERO_INTERRUPCOES_TIPO[i]);
    }

    for (int i = 0; i < QUANTIDADE_PROC_CRIADOS; i++) {
      int soma_estados = 0;
        processo_t *p = &self->tabela_processos[i];
        proc_metricas *met = p->metricas;
        fprintf(arquivo, "--------------------------------------------\n");
        fprintf(arquivo, "PID: %d\n", p->pid_processo);

        for (int j = 1; j < QUANTIDADE_ESTADOS_PROC; j++) {
          if(j >= 2)
            soma_estados += met->estado_n_vezes[j];
          fprintf(arquivo, "Estado %s: %d vezes\n", nome_estado(j), met->estado_n_vezes[j]);
          fprintf(arquivo, "Estado %s: %d inst\n", nome_estado(j), met->estado_t_total[j]);
        }
      fprintf(arquivo, "Preempcoes: %d vezes\n", met->n_preempcoes);
      fprintf(arquivo, "Tempo medio resposta: %f\n", (double)met->estado_t_total[ESTADO_PROC_PRONTO]/met->estado_n_vezes[ESTADO_PROC_PRONTO]);
      fprintf(arquivo, "Tempo retorno: %d inst\n", soma_estados);
      
    }

    fclose(arquivo);
}

static void tira_processo_fila(so_t *self, int pid_para_tirar)
{
    if(self->fila_processos == NULL) {
        console_printf("Fila vazia\n");
    } else {
        processo_t *anterior = NULL;
        processo_t *andarilho = self->fila_processos;

        // Percorre até encontrar o processo com o pid solicitado
        while(andarilho != NULL && andarilho->pid_processo != pid_para_tirar) {
            anterior = andarilho;
            andarilho = andarilho->prox_processo;
        }

        if(andarilho == NULL) {
            console_printf("Nao achei o processo para tirar da fila.\n");
        } else {
            // Remoção do primeiro processo
            if(anterior == NULL) {
                self->fila_processos = andarilho->prox_processo;
            } else {
                anterior->prox_processo = andarilho->prox_processo;
            }
            // Libera a memória, se necessário (apenas se o processo for dinamicamente alocado)
            // free(andarilho); // Descomente se for aplicável
        }
    }
}

static void muda_estado_proc(so_t *self, processo_t *processo, int novo_estado)
{
  processo->estado_processo = novo_estado;
  processo->metricas->estado_n_vezes[novo_estado] += 1;

  if(novo_estado == ESTADO_PROC_PRONTO)
  {
    coloca_processo_fila(self, processo);
  }
  else
  {
    tira_processo_fila(self, processo->pid_processo);
  }
}

static void desarma_relogio(so_t *self)
{
  for (int i = 0; i < QUANTIDADE_PROCESSOS; i++)
  {
    processo_t *p = &self->tabela_processos[i];
    if (p->estado_processo != ESTADO_PROC_MORTO)
    {
      return;
    }
  }

  err_t e1 = es_escreve(self->es, D_RELOGIO_INTERRUPCAO, 0);
  err_t e2 = es_escreve(self->es, D_RELOGIO_TIMER, 0);
  if (e1 != ERR_OK || e2 != ERR_OK)
  {
    console_printf("SO: erro ao desarmar o relogio.");
    self->erro_interno = true;
  }
  else
  {
    console_printf("SO: sucesso ao desarmar o relogio.");
    imprime_metricas(self);
  }
}

static void so_trata_irq_relogio(so_t *self)
{
  // rearma o interruptor do relógio e reinicializa o timer para a próxima interrupção
  err_t e1, e2;
  e1 = es_escreve(self->es, D_RELOGIO_INTERRUPCAO, 0); // desliga o sinalizador de interrupção
  e2 = es_escreve(self->es, D_RELOGIO_TIMER, INTERVALO_INTERRUPCAO);
  if (e1 != ERR_OK || e2 != ERR_OK)
  {
    console_printf("SO: problema da reinicialização do timer");
    self->erro_interno = true;
  }
  desarma_relogio(self);
  self->quantum--;
}

// foi gerada uma interrupção para a qual o SO não está preparado
static void so_trata_irq_desconhecida(so_t *self, int irq)
{
  console_printf("SO: não sei tratar IRQ %d (%s)", irq, irq_nome(irq));
  self->erro_interno = true;
}

// CHAMADAS DE SISTEMA {{{1

// funções auxiliares para cada chamada de sistema
static void so_chamada_le(so_t *self);
static void so_chamada_escr(so_t *self);
static void so_chamada_cria_proc(so_t *self);
static void so_chamada_mata_proc(so_t *self);
static void so_chamada_espera_proc(so_t *self);

static void so_trata_irq_chamada_sistema(so_t *self)
{
  // a identificação da chamada está no registrador A
  // t1: com processos, o reg A tá no descritor do processo corrente
  int id_chamada = self->processo_corrente->reg_A;

  // console_printf("SO: chamada de sistema %d", id_chamada);
  switch (id_chamada)
  {
  case SO_LE:
    so_chamada_le(self);
    break;
  case SO_ESCR:
    so_chamada_escr(self);
    break;
  case SO_CRIA_PROC:
    so_chamada_cria_proc(self);
    break;
  case SO_MATA_PROC:
    so_chamada_mata_proc(self);
    break;
  case SO_ESPERA_PROC:
    so_chamada_espera_proc(self);
    break;
  default:
    console_printf("SO: chamada de sistema desconhecida (%d)", id_chamada);
    // t1: deveria matar o processo
    //self->processo_corrente->estado_processo = ESTADO_PROC_MORTO;
    muda_estado_proc(self, self->processo_corrente, ESTADO_PROC_MORTO);
  }
}

// implementação da chamada se sistema SO_LE
// faz a leitura de um dado da entrada corrente do processo, coloca o dado no reg A
static void so_chamada_le(so_t *self)
{
  tenta_ler(self, self->processo_corrente, true);
}

// implementação da chamada se sistema SO_ESCR
// escreve o valor do reg X na saída corrente do processo
static void so_chamada_escr(so_t *self)
{
  tenta_escrever(self, self->processo_corrente, true);
}

porta_t *atribuir_porta(so_t *self)
{
  for (int i = 0; i < QUANTIDADE_PROCESSOS; i++)
  {
    porta_t *p = &self->tabela_portas[i];
    if (p->porta_ocupada == false)
    {
      int deslocamento = 4 * i;
      p->porta_ocupada = true;
      p->teclado = D_TERM_A_TECLADO + deslocamento;
      p->teclado_estado = D_TERM_A_TECLADO_OK + deslocamento;
      p->tela = D_TERM_A_TELA + deslocamento;
      p->tela_estado = D_TERM_A_TELA_OK + deslocamento;

      return p;
    }
  }
  return NULL;
}

static void inicializa_proc(so_t *self, processo_t *processo, int ender_carga)
{
  processo->reg_PC = ender_carga;
  processo->reg_A = 0;
  processo->reg_X = 0;
  processo->reg_erro = 0;
  processo->reg_complemento = 0;
  processo->modo = usuario;
  processo->pid_processo = PID_GLOBAL++;
  //processo->estado_processo = ESTADO_PROC_PRONTO;
  processo->porta_processo = atribuir_porta(self);
  processo->bloqueio_motivo = 0;

  processo->prioridade = 0.5;

  processo->metricas = (proc_metricas*)malloc(sizeof(proc_metricas));

  for(int i = 0; i < QUANTIDADE_ESTADOS_PROC; i++)
  {
    processo->metricas->estado_n_vezes[i] = 0;
    processo->metricas->estado_t_total[i] = 0;
    processo->metricas->n_preempcoes = 0;
  }

  muda_estado_proc(self, processo, ESTADO_PROC_PRONTO);
}

// implementação da chamada se sistema SO_CRIA_PROC
// cria um processo
static void so_chamada_cria_proc(so_t *self)
{
  // ainda sem suporte a processos, carrega programa e passa a executar ele
  // quem chamou o sistema não vai mais ser executado, coitado!
  // T1: deveria criar um novo processo

  // em X está o endereço onde está o nome do arquivo
  int ender_proc = self->processo_corrente->reg_X;
  // t1: deveria ler o X do descritor do processo criador
  char nome[100];
  if (copia_str_da_mem(100, nome, self->mem, ender_proc))
  {
    int ender_carga = so_carrega_programa(self, nome);
    if (ender_carga > 0)
    {
      for (int i = 0; i < QUANTIDADE_PROCESSOS; i++)
      {
        processo_t *processo = &self->tabela_processos[i];
        if (processo->estado_processo == ESTADO_PROC_MORTO)
        {

          inicializa_proc(self, processo, ender_carga);

          if (processo->porta_processo == NULL)
          {
            console_printf("Impossivel atribuir terminal, por isso nao cria o processo.");
            self->processo_corrente->reg_A = -1;
            return;
          }

          console_printf("Criando novo processo...com PID %d", processo->pid_processo);
          self->processo_corrente->reg_A = processo->pid_processo;
          QUANTIDADE_PROC_CRIADOS++;
          return;
        }
      }
    }
    else
    {
      console_printf("Impossivel criar processo, escrevendo -1 no reg A do criador.");
      self->processo_corrente->reg_A = -1;
    }
  }
}

// implementação da chamada se sistema SO_MATA_PROC
// mata o processo com pid X (ou o processo corrente se X é 0)
static void so_chamada_mata_proc(so_t *self)
{
  if (self->processo_corrente->reg_X != 0)
  {
    console_printf("Matando o processo de PID %d", self->processo_corrente->reg_X);
    for (int i = 0; i < QUANTIDADE_PROCESSOS; i++)
    {
      if (self->tabela_processos[i].pid_processo == self->processo_corrente->reg_X)
      {
        //self->tabela_processos[i].estado_processo = ESTADO_PROC_MORTO;
        muda_estado_proc(self, &self->tabela_processos[i], ESTADO_PROC_MORTO);
        self->tabela_processos[i].porta_processo->porta_ocupada = false;
        return;
      }
      console_printf("Nao achei o processo com esse PID para matar.");
    }
  }
  else if (self->processo_corrente->reg_X == 0)
  {
    console_printf("Matando o proprio processo de PID %d.", self->processo_corrente->pid_processo);
    //self->processo_corrente->estado_processo = ESTADO_PROC_MORTO;
    muda_estado_proc(self, self->processo_corrente, ESTADO_PROC_MORTO);
    self->processo_corrente->porta_processo->porta_ocupada = false;
  }
}

// implementação da chamada se sistema SO_ESPERA_PROC
// espera o fim do processo com pid X
static void so_chamada_espera_proc(so_t *self)
{
  bloqueia_processo(self, self->processo_corrente, BLOQUEIO_ESPERA, self->processo_corrente->reg_X);
}

// CARGA DE PROGRAMA {{{1

// carrega o programa na memória
// retorna o endereço de carga ou -1
static int so_carrega_programa(so_t *self, char *nome_do_executavel)
{
  // programa para executar na nossa CPU
  programa_t *prog = prog_cria(nome_do_executavel);
  if (prog == NULL)
  {
    console_printf("Erro na leitura do programa '%s'\n", nome_do_executavel);
    return -1;
  }

  int end_ini = prog_end_carga(prog);
  int end_fim = end_ini + prog_tamanho(prog);

  for (int end = end_ini; end < end_fim; end++)
  {
    if (mem_escreve(self->mem, end, prog_dado(prog, end)) != ERR_OK)
    {
      console_printf("Erro na carga da memória, endereco %d\n", end);
      return -1;
    }
  }

  prog_destroi(prog);
  console_printf("SO: carga de '%s' em %d-%d", nome_do_executavel, end_ini, end_fim);
  return end_ini;
}

// ACESSO À MEMÓRIA DOS PROCESSOS {{{1

// copia uma string da memória do simulador para o vetor str.
// retorna false se erro (string maior que vetor, valor não char na memória,
//   erro de acesso à memória)
// T1: deveria verificar se a memória pertence ao processo
static bool copia_str_da_mem(int tam, char str[tam], mem_t *mem, int ender)
{
  for (int indice_str = 0; indice_str < tam; indice_str++)
  {
    int caractere;
    if (mem_le(mem, ender + indice_str, &caractere) != ERR_OK)
    {
      return false;
    }
    if (caractere < 0 || caractere > 255)
    {
      return false;
    }
    str[indice_str] = caractere;
    if (caractere == 0)
    {
      return true;
    }
  }
  // estourou o tamanho de str
  return false;
}

// vim: foldmethod=marker