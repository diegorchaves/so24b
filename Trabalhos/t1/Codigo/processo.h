typedef struct porta_t porta_t;
typedef struct processo_t processo_t;
typedef struct proc_metricas proc_metricas;

struct porta_t
{
  bool porta_ocupada;
  int teclado_estado;
  int teclado;
  int tela_estado;
  int tela;
};

struct processo_t
{
  int pid_processo;
  int reg_A;
  int reg_X;
  int reg_PC;
  int reg_erro;
  int reg_complemento;
  int modo;
  int estado_processo;
  int bloqueio_motivo;
  int pid_esperado;
  porta_t *porta_processo;
  processo_t *prox_processo;
  proc_metricas *metricas;

  double prioridade;
};

typedef enum
{
    ESTADO_PROC_MORTO       = 1,
    ESTADO_PROC_PRONTO      = 2,
    ESTADO_PROC_BLOQUEADO   = 3,
    ESTADO_PROC_EXECUTANDO  = 4,
    QUANTIDADE_ESTADOS_PROC
} estado_processo_id;

typedef enum 
{
  BLOQUEIO_ESC        = 1,
  BLOQUEIO_LE         = 2,
  BLOQUEIO_ESPERA     = 3
} bloqueio_id;

struct proc_metricas
{
  int estado_n_vezes[QUANTIDADE_ESTADOS_PROC];
  int estado_t_total[QUANTIDADE_ESTADOS_PROC];
  int n_preempcoes;
};