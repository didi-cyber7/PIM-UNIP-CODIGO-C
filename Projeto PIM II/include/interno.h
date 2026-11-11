/* Contratos internos entre persistencia, distribuicao, enxoval e telas. */
#ifndef INTERNO_H
#define INTERNO_H

#include "projeto.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include <limits.h>

#define ARQ_COLABORADORES "dados/colaborador.csv"
#define ARQ_CONFIG_CAMAREIRAS "dados/config_camareiras.csv"
#define ARQ_TIPOLOGIAS "dados/tipologia_uh.csv"
#define ARQ_UHS "dados/uh.csv"
#define ARQ_COMPOSICAO "dados/item_composicao_padrao.csv"
#define ARQ_ENXOVAL "dados/item_enxoval.csv"
#define ARQ_ENTREGAS "dados/entrega_kit.csv"
#define ARQ_ENTREGA_ITENS "dados/entrega_kit_item.csv"
#define ARQ_CONSUMOS "dados/consumo_uh.csv"
#define ARQ_MAPA_ENTREGA_UH "dados/entrega_kit_uh.csv"
#define ARQ_OCORRENCIAS "dados/registro_ocorrencias.csv"
#define MAX_ITENS_ENXOVAL 10000
#define MAX_ATRIBUICOES_DIA 500
#define MAX_COMPOSICOES 32
#define MAX_ITENS_POR_ENTREGA 300

typedef struct {
    int id;
    int id_camareira;
    int id_uh;
    char data[11];
    char tipo[24];
} Atribuicao;

typedef struct {
    int id_tipologia;
    char tipo_item[32];
    int quantidade;
} Composicao;

typedef struct {
    int id_entrega;
    int id_camareira;
    int id_responsavel;
    char data_hora[20];
    int conferido;
} Entrega;

typedef struct {
    int id_entrega;
    int id_uh;
    char data[11];
    char data_hora_fim[20];
} MapaEntregaUH;

typedef struct {
    int id_consumo;
    int id_entrega;
    int id_uh;
    int id_item;
    char data_hora[20];
    int peca_manchada;
} Consumo;

int separar_csv(char *linha, char *campos[], int limite);
int ler_item_enxoval(char *linha, ItemEnxoval *item);
int carregar_itens_estoque(ItemEnxoval itens[], int limite);
int carregar_atribuicoes(const char *data, Atribuicao atribuicoes[], int limite);
int buscar_tipologia_uh(int id_uh, int *id_tipologia, char *tipo_cama, size_t tam_tipo);
int carregar_composicoes(int id_tipologia, Composicao comp[], int limite);
int carregar_entregas(Entrega entregas[], int limite);
int carregar_mapa_entregas(MapaEntregaUH mapa[], int limite);
int carregar_ligacoes_entrega(int id_entrega, int ids[], int limite);
int buscar_data_hora_entrega(int id_entrega, char *data_hora, size_t tamanho);
int entrega_tem_consumo(int id_entrega);
int entrega_tem_data_hora(int id_entrega);
int max_id_entrega(void);
int max_id_consumo(void);
int data_hora_valida(const char *valor);
int horario_para_minutos(const char *hora);
int salvar_itens_estoque(const ItemEnxoval itens[], int quantidade, const char *temporario);
int confirmar_arquivos(const char *temporarios[], const char *destinos[], int quantidade);
int ler_inteiro_prompt(const char *mensagem, int minimo, int maximo, int *valor);
int ler_decimal_prompt(const char *mensagem, double minimo, double maximo, double *valor);
int ler_texto_prompt(const char *mensagem, char *destino, size_t tamanho);
int ler_senha_supervisao(void);
void obter_data_atual(char *destino, size_t tamanho);
void obter_data_hora(char *destino, size_t tamanho);
int colaborador_existe(int id);
const char *tipo_item_normalizado(const char *tipo);
const char *tamanho_item_normalizado(const ItemEnxoval *item);
int montar_kit_uh(int id_uh, int id_camareira, const char *data, int id_tipologia);
int preparar_distribuicao_csv(Camareira camareiras[], int quantidade, const char *data);
int registrar_consumo_com_ocorrencias(int id_entrega, int id_uh, int id_camareira,
                                     const char *data, const char *inicio, const char *fim,
                                     const int manchados[], int nm, const int avariados[], int na,
                                     const int faltantes[], int nf);
int baixar_item_com_movimento(int id_item, const char *motivo,
                             const char *responsabilidade, int id_colaborador);

#endif
