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

#include <stdlib.h>
#include <stdbool.h>

// CONSTANTES E TIPOS {{{1
// intervalo entre interrupções do relógio
#define INTERVALO_INTERRUPCAO 20 // em instruções executadas
#define QUANTIDADE_PROCESSOS 4 
#define NUM_TERMINAIS 4
#define QUANTUM 5

int PID_GLOBAL = 0;

struct terminal_t
{
    int ocupado;
};

struct processo_t
{
    int PC;
    int A;
    int X;
    int erro;
    int complemento;
    int pid_processo;
    int modo;
    int vivo;
    int terminal_id;
    int bloqueado;
    int bloqueio_motivo;
    int pid_esperado;
    processo_t *proximo;
};

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
    terminal_t tabela_terminais[NUM_TERMINAIS];

    processo_t *fila_processos;

    int quantum;
};

// função de tratamento de interrupção (entrada no SO)
static int so_trata_interrupcao(void *argC, int reg_A);

// funções auxiliares
// carrega o programa contido no arquivo na memória do processador; retorna end. inicial
static int so_carrega_programa(so_t * self, char *nome_do_executavel);
// copia para str da memória do processador, até copiar um 0 (retorna true) ou tam bytes
static bool copia_str_da_mem(int tam, char str[tam], mem_t *mem, int ender);

static void pega_terminais(int terminal_id, int* teclado_estado, int* teclado, int* tela_estado, int* tela);

static int processo_esta_bloqueado(processo_t *processo);

static int processo_esta_vivo(processo_t *processo);

static void inicializa_proc(so_t *self, processo_t *processo, int ender_carga);

// CRIAÇÃO {{{1

so_t *so_cria(cpu_t * cpu, mem_t * mem, es_t * es, console_t * console)
{
    so_t *self = malloc(sizeof(*self));
    if (self == NULL)
        return NULL;

    self->cpu = cpu;
    self->mem = mem;
    self->es = es;
    self->console = console;
    self->erro_interno = false;
    self->processo_corrente = NULL;
    self->fila_processos = NULL;
    self->quantum = QUANTUM;

    for(int i = 0; i < NUM_TERMINAIS; i++) {
        self->tabela_terminais[i].ocupado = 0;
    }

    for(int i = 0; i < QUANTIDADE_PROCESSOS; i++) {
        self->tabela_processos[i].vivo = false;
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

void so_destroi(so_t * self)
{
    cpu_define_chamaC(self->cpu, NULL, NULL);
    free(self);
}

// TRATAMENTO DE INTERRUPÇÃO {{{1

// funções auxiliares para o tratamento de interrupção
static void so_salva_estado_da_cpu(so_t * self);
static void so_trata_irq(so_t * self, int irq);
static void so_trata_pendencias(so_t * self);
static void so_escalona(so_t * self);
static int so_despacha(so_t * self);

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
    //console_printf("SO: recebi IRQ %d (%s)", irq, irq_nome(irq));
    // salva o estado da cpu no descritor do processo que foi interrompido
    so_salva_estado_da_cpu(self);
    // faz o atendimento da interrupção
    so_trata_irq(self, irq);
    // faz o processamento independente da interrupção
    so_trata_pendencias(self);
    // escolhe o próximo processo a executar
    so_escalona(self);
    // recupera o estado do processo escolhido
    return so_despacha(self);
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
            andarilho = andarilho->proximo;
        }

        if(andarilho == NULL) {
            console_printf("Nao achei o processo para tirar da fila.\n");
        } else {
            // Remoção do primeiro processo
            if(anterior == NULL) {
                self->fila_processos = andarilho->proximo;
            } else {
                anterior->proximo = andarilho->proximo;
            }
            // Libera a memória, se necessário (apenas se o processo for dinamicamente alocado)
            // free(andarilho); // Descomente se for aplicável
        }
    }
}

static void coloca_processo_fila(so_t *self, processo_t *processo)
{
    if(self->fila_processos == NULL) {
        self->fila_processos = processo;
    } else {
        processo_t *andarilho = self->fila_processos;
        while(andarilho->proximo != NULL) {
            andarilho = andarilho->proximo;
        }
        andarilho->proximo = processo;
    }
    processo->proximo = NULL;
}

static void bloqueia_processo(so_t *self, processo_t *processo, bloqueio_id motivo, int pid_esperado) 
{
    if(!processo_esta_bloqueado(processo)) {
        processo->bloqueado = true;
        processo->bloqueio_motivo = motivo;

        if(motivo == BLOQUEIO_ESPERA) {
            processo->pid_esperado = pid_esperado;
        }
        tira_processo_fila(self, processo->pid_processo);
    }

}

static void so_salva_estado_da_cpu(so_t * self)
{
    if(self->processo_corrente != NULL && !processo_esta_bloqueado(self->processo_corrente)) {
        mem_le(self->mem, IRQ_END_PC, &self->processo_corrente->PC);
        mem_le(self->mem, IRQ_END_A, &self->processo_corrente->A);
        mem_le(self->mem, IRQ_END_X, &self->processo_corrente->X);
        mem_le(self->mem, IRQ_END_erro, &self->processo_corrente->erro);
        mem_le(self->mem, IRQ_END_complemento, &self->processo_corrente->complemento);
        mem_le(self->mem, IRQ_END_modo, &self->processo_corrente->modo);
    }
    else {
        console_printf("Nao ha processo corrente, entao nao vou salvar estado da cpu!");
    }
}

static void desbloqueia_processo(so_t *self, processo_t *processo) 
{
    if(processo_esta_bloqueado(processo)) {
        processo->bloqueado = false;
        coloca_processo_fila(self, processo);
    }
}

static void tenta_ler(so_t *self, processo_t *processo, int teclado_estado, int teclado, int chamada_sistema) 
{
    
    int estado;

    if(es_le(self->es, teclado_estado, &estado) != ERR_OK) {
        console_printf("SO: problema no acesso ao estado da tela");
        self->erro_interno = true;
        return;
    }

    if(estado == 0 && chamada_sistema == 1) {
        /* processo->bloqueado = true;
        processo->bloqueio_motivo = BLOQUEIO_LE; */
        bloqueia_processo(self, processo, BLOQUEIO_LE, 0);
    } else {
        int dado;
        if (es_le(self->es, teclado, &dado) != ERR_OK) {
            console_printf("SO: problema no acesso ao teclado");
            self->erro_interno = true;
            return;
        }
        processo->A = dado;
        
        desbloqueia_processo(self, processo);
    }
}

static void tenta_escrever(so_t *self, processo_t *processo, int tela_estado, int tela, int chamada_sistema) 
{
    int estado;

    if(es_le(self->es, tela_estado, &estado) != ERR_OK) {
        console_printf("SO: problema no acesso ao estado da tela");
        self->erro_interno = true;
        return;
    }

    if(estado == 0 && chamada_sistema == 1) {
        bloqueia_processo(self, processo, BLOQUEIO_ESC, 0);
    } else {
        int dado = processo->X;
        if(es_escreve(self->es, tela, dado) != ERR_OK) {
            console_printf("SO: problema no acesso à tela");
            self->erro_interno = true;
            return;
        }
        desbloqueia_processo(self, processo);

        if(chamada_sistema) {
            processo->A = 0;
        }
    }
}     

static void trata_pendencia_le(so_t *self, processo_t *processo) 
{
    int teclado_estado, teclado;

    pega_terminais(processo->terminal_id, &teclado_estado, &teclado, NULL, NULL);

    tenta_ler(self, processo, teclado_estado, teclado, false);

}

static void trata_pendencia_esc(so_t *self, processo_t *processo) 
{
    
    int tela_estado, tela;

    pega_terminais(processo->terminal_id, NULL, NULL, &tela_estado, &tela);

    tenta_escrever(self, processo, tela_estado, tela, false);
}

static int processo_esta_vivo(processo_t *processo) 
{
    return (processo->vivo);
}

static int processo_esta_bloqueado(processo_t *processo) 
{
    return (processo->bloqueado);
}

static void trata_pendencia_espera(so_t *self, processo_t *processo) 
{
    for(int i = 0; i < QUANTIDADE_PROCESSOS; i++) {
        processo_t *processo_esperado = &self->tabela_processos[i];
         if(processo_esperado->pid_processo == processo->pid_esperado && !processo_esta_vivo(processo_esperado)) {
            desbloqueia_processo(self, processo);
            console_printf("Desbloquando processo porque o esperado de PID %d morreu.", processo_esperado->pid_processo);
         }
     }
}

static void so_trata_pendencias(so_t * self)
{

    for(int i = 0; i < QUANTIDADE_PROCESSOS; i++) {
        processo_t *processo = &self->tabela_processos[i];
        if(processo_esta_vivo(processo) && processo_esta_bloqueado(processo)) {
            
            int tela_estado;
            //int tela;
            int teclado_estado;
            //int teclado;

            pega_terminais(processo->terminal_id, &teclado_estado, NULL, &tela_estado, NULL);

            switch (processo->bloqueio_motivo) {
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

static int so_deve_escalonar(so_t *self)
{
    if(!processo_esta_vivo(self->processo_corrente))
        return 1;

    if(processo_esta_bloqueado(self->processo_corrente))
        return 1;

    if(self->quantum <= 0)
        return 1;

    return 0;
}

static void so_escalona(so_t * self)
{
    if(!so_deve_escalonar(self)) {
        return;
    }
    else {
       if(processo_esta_vivo(self->processo_corrente) && !processo_esta_bloqueado(self->processo_corrente) && self->quantum <= 0) {
            tira_processo_fila(self, self->processo_corrente->pid_processo);
            coloca_processo_fila(self, self->processo_corrente);
       }
       if(self->fila_processos != NULL) {
            self->processo_corrente = self->fila_processos;
            tira_processo_fila(self, self->processo_corrente->pid_processo);
       }
       self->quantum = QUANTUM;    
    }
}

static int so_despacha(so_t * self)
{

    if(processo_esta_vivo(self->processo_corrente) && !processo_esta_bloqueado(self->processo_corrente)) { 
        mem_escreve(self->mem, IRQ_END_PC, self->processo_corrente->PC);
        mem_escreve(self->mem, IRQ_END_A, self->processo_corrente->A);
        mem_escreve(self->mem, IRQ_END_X, self->processo_corrente->X);
        mem_escreve(self->mem, IRQ_END_erro, self->processo_corrente->erro);
        mem_escreve(self->mem, IRQ_END_complemento, self->processo_corrente->complemento);
        mem_escreve(self->mem, IRQ_END_modo, self->processo_corrente->modo);

        return 0;
    }
    else return 1;
}

// TRATAMENTO DE UMA IRQ {{{1

// funções auxiliares para tratar cada tipo de interrupção
static void so_trata_irq_reset(so_t * self);
static void so_trata_irq_chamada_sistema(so_t * self);
static void so_trata_irq_err_cpu(so_t * self);
static void so_trata_irq_relogio(so_t * self);
static void so_trata_irq_desconhecida(so_t * self, int irq);

static int atribuir_terminal(so_t *self);

static void so_trata_irq(so_t * self, int irq)
{
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

// interrupção gerada uma única vez, quando a CPU inicializa
static void so_trata_irq_reset(so_t * self)
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

    for(int i = 0; i < QUANTIDADE_PROCESSOS; i++) {
        processo_t *processo = &self->tabela_processos[i];
        if(processo->vivo == false) {

            inicializa_proc(self, processo, ender);

            if(processo->terminal_id == -1) {
                console_printf("Impossivel atribuir terminal ao processo.");
                self->erro_interno = true;
            }

            self->processo_corrente = processo;
            return;
        }
    }

/*         // altera o PC para o endereço de carga
    mem_escreve(self->mem, IRQ_END_PC, ender);
    // passa o processador para modo usuário
    mem_escreve(self->mem, IRQ_END_modo, usuario); */
}

static int atribuir_terminal(so_t *self) 
{
    for (int i = 0; i < NUM_TERMINAIS; i++) {
        if(self->tabela_terminais[i].ocupado == 0) {
            self->tabela_terminais[i].ocupado = 1;
            return i; // Retorna o id do terminal
        }
    }
    return -1; // Sem terminais disponíveis
}

// interrupção gerada quando a CPU identifica um erro
static void so_trata_irq_err_cpu(so_t * self)
{
    // Ocorreu um erro interno na CPU
    // O erro está codificado em IRQ_END_erro
    // Em geral, causa a morte do processo que causou o erro
    // Ainda não temos processos, causa a parada da CPU
    int err_int = self->processo_corrente->erro;
    // t1: com suporte a processos, deveria pegar o valor do registrador erro
    //   no descritor do processo corrente, e reagir de acordo com esse erro
    //   (em geral, matando o processo)
    // mem_le(self->mem, IRQ_END_erro, &err_int);
    err_t err = err_int;
    console_printf("SO: IRQ não tratada -- erro na CPU: %s", err_nome(err));
    // self->erro_interno = true;
    console_printf("Matando o processo...PID %d", self->processo_corrente->pid_processo);
    self->processo_corrente->vivo = false;
}

// interrupção gerada quando o timer expira
static void so_trata_irq_relogio(so_t * self)
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
    
    self->quantum--;
    // t1: deveria tratar a interrupção
    //   por exemplo, decrementa o quantum do processo corrente, quando se tem
    //   um escalonador com quantum
    //console_printf("SO: interrupção do relógio (não tratada)");
}

// foi gerada uma interrupção para a qual o SO não está preparado
static void so_trata_irq_desconhecida(so_t * self, int irq)
{
    console_printf("SO: não sei tratar IRQ %d (%s)", irq, irq_nome(irq));
    self->erro_interno = true;
}

// CHAMADAS DE SISTEMA {{{1

// funções auxiliares para cada chamada de sistema
static void so_chamada_le(so_t * self);
static void so_chamada_escr(so_t * self);
static void so_chamada_cria_proc(so_t * self);
static void so_chamada_mata_proc(so_t * self);
static void so_chamada_espera_proc(so_t * self);

static void so_trata_irq_chamada_sistema(so_t * self)
{
    // a identificação da chamada está no registrador A
    // t1: com processos, o reg A tá no descritor do processo corrente
    int id_chamada = self->processo_corrente->A;

    console_printf("SO: chamada de sistema %d", id_chamada);
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
        console_printf("Matando o processo...PID %d", self->processo_corrente->pid_processo);
        self->processo_corrente->vivo = false;
    }
}

static void pega_terminais(int terminal_id, int* teclado_estado, int* teclado, int* tela_estado, int* tela) {
    
    int deslocamento = 4 * terminal_id;
    if(teclado != NULL)
        *teclado = D_TERM_A_TECLADO + deslocamento;
    if(teclado_estado != NULL)
        *teclado_estado = D_TERM_A_TECLADO_OK + deslocamento;
    if(tela != NULL)
        *tela = D_TERM_A_TELA + deslocamento;
    if(tela_estado != NULL)
        *tela_estado = D_TERM_A_TELA_OK + deslocamento;
}
// implementação da chamada se sistema SO_LE
// faz a leitura de um dado da entrada corrente do processo, coloca o dado no reg A
static void so_chamada_le(so_t * self)
{

    int teclado_estado, teclado;
    pega_terminais(self->processo_corrente->terminal_id, &teclado_estado, &teclado, NULL, NULL);

    tenta_ler(self, self->processo_corrente, teclado_estado, teclado, true);
}

// implementação da chamada se sistema SO_ESCR
// escreve o valor do reg X na saída corrente do processo
static void so_chamada_escr(so_t * self)
{

    int tela_estado, tela;
    pega_terminais(self->processo_corrente->terminal_id, NULL, NULL, &tela_estado, &tela);

    tenta_escrever(self, self->processo_corrente, tela_estado, tela, true);

}

static void inicializa_proc(so_t *self, processo_t *processo, int ender_carga) 
{
    processo->PC = ender_carga;
    processo->A = 0;
    processo->X = 0;
    processo->erro = 0;
    processo->complemento = 0;
    processo->modo = usuario;
    processo->pid_processo = PID_GLOBAL++;
    processo->vivo = true;
    processo->terminal_id = atribuir_terminal(self);
    processo->bloqueado = false;
    processo->bloqueio_motivo = 0;
}

// implementação da chamada se sistema SO_CRIA_PROC
// cria um processo
static void so_chamada_cria_proc(so_t * self)
{
    // ainda sem suporte a processos, carrega programa e passa a executar ele
    // quem chamou o sistema não vai mais ser executado, coitado!
    // T1: deveria criar um novo processo

    // em X está o endereço onde está o nome do arquivo
    int ender_proc = self->processo_corrente->X;
    // t1: deveria ler o X do descritor do processo criador

    char nome[100];
    if (copia_str_da_mem(100, nome, self->mem, ender_proc))
    {
        int ender_carga = so_carrega_programa(self, nome);
        if (ender_carga > 0)
        {
            for(int i = 0; i < QUANTIDADE_PROCESSOS; i++) {
                processo_t *processo = &self->tabela_processos[i];
                if(processo->vivo == false) {

                    inicializa_proc(self, processo, ender_carga);

                    if(processo->terminal_id == -1) {
                        console_printf("Impossivel atribuir terminal, por isso nao cria o processo.");
                        self->processo_corrente->A = -1;
                        return;
                    }
                    
                    console_printf("Criando novo processo...com PID %d", processo->pid_processo);
                    coloca_processo_fila(self, processo);
                    self->processo_corrente->A = processo->pid_processo;
                    return;
                }
            }
        }
        else {
            console_printf("Impossivel criar processo, escrevendo -1 no reg A do criador.");
            self->processo_corrente->A = -1;
        }
    }
}

// implementação da chamada se sistema SO_MATA_PROC
// mata o processo com pid X (ou o processo corrente se X é 0)
static void so_chamada_mata_proc(so_t * self)
{
    if(self->processo_corrente->X != 0) {
        console_printf("Matando o processo de PID %d", self->processo_corrente->X);
        for(int i = 0; i < QUANTIDADE_PROCESSOS; i++) {
            if(self->tabela_processos[i].pid_processo == self->processo_corrente->X) {
                self->tabela_processos[i].vivo = false;
                self->tabela_terminais[self->tabela_processos[i].terminal_id].ocupado = 0;
                return;
            }
            console_printf("Nao achei o processo com esse PID para matar.");
        }
    }
    else if(self->processo_corrente-> X == 0) {
        console_printf("Matando o proprio processo de PID %d.", self->processo_corrente->pid_processo);
        self->processo_corrente->vivo = false;
        self->tabela_terminais[self->processo_corrente->terminal_id].ocupado = 0;
    }
}

// implementação da chamada se sistema SO_ESPERA_PROC
// espera o fim do processo com pid X
static void so_chamada_espera_proc(so_t *self)
{
    bloqueia_processo(self, self->processo_corrente, BLOQUEIO_ESPERA, self->processo_corrente->X);
}

// CARGA DE PROGRAMA {{{1

// carrega o programa na memória
// retorna o endereço de carga ou -1
static int so_carrega_programa(so_t * self, char *nome_do_executavel)
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