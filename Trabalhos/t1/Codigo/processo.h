typedef struct porta_t porta_t;
typedef struct processo_t processo_t;

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
  int reg_A;
  int reg_X;
  int reg_PC;
  int reg_erro;
  int reg_complemento;
  int modo;
  int estado_processo;
  porta_t *porta_processo;

  processo_t *prox_processo;

  double prioridade;
};

typedef enum
{
    ESTADO_PROC_MORTO       = 1,
    ESTADO_PROC_PRONTO      = 2,
    ESTADO_PROC_BLOQUEADO   = 3,
    ESTADO_PROC_EXECUTANDO  = 4
} estado_processo_id;