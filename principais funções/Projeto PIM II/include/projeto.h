/*
 * CABECALHO COMPARTILHADO: define estruturas de dados, limites e assinaturas.
 * As telas incluem este arquivo para usar as mesmas regras e funcoes.
 */
#ifndef PROJETO_H
#define PROJETO_H

#include <stdio.h>

#define MAX_CAMAREIRAS 50
#define MAX_UHS 100
#define MAX_REGISTROS 500
#define TIPO_TITULAR 1
#define TIPO_SUBSTITUICAO 2

#define ARQ_CAMAREIRAS "dados/config_camareiras.csv"
#define ARQ_FALTAS "dados/faltas.csv"
#define ARQ_DISTRIBUICAO "dados/atribuicao_diaria.csv"

typedef struct {
    int id;
    char nome[50];
    int presente;
    int quantidade_titular;
    int uhs_titulares[MAX_UHS];
    int quantidade_total;
    int uhs_atribuidas[MAX_UHS];
} Camareira;

typedef struct {
    int id_camareira;
    int id_uh;
    int tipo;
    int tempo_minutos;
} RegistroAtendimento;

typedef struct {
    int id_item;
    char tag_rfid[31];
    char tipo[31];
    char tamanho_cama[21];
    char situacao[31];
    char data_aquisicao[11];
    int ciclos_lavagem;
    int limite_ciclos;
    char motivo_baixa[51];
    char responsavel_baixa[31];
} ItemEnxoval;

typedef struct {
    int id_camareira;
    char nome[50];
    int total_uhs;
    int titulares;
    int substituicoes;
    int tempo_total;
} Indicador;

int carregar_camareiras(Camareira camareiras[], int limite);
int salvar_faltas_do_dia(Camareira camareiras[], int quantidade, const char *data);
int carregar_faltas_do_dia(Camareira camareiras[], int quantidade, const char *data);
int carregar_atendimentos(RegistroAtendimento registros[], int limite);
int montar_kits_distribuicao(const char *data);
int tipo_atendimento_distribuido(int id_camareira, int id_uh, const char *data);
int buscar_entrega_uh(int id_uh, int id_camareira, const char *data, int *id_entrega);
int carregar_itens_entrega(int id_entrega, ItemEnxoval itens[], int limite);
int atualizar_situacao_item(int id_item, const char *nova_situacao);
int registrar_ocorrencia_csv(const char *data, int id_camareira, int id_uh, const char *rfid, const char *problema);
int registrar_consumo_atendimento(int id_entrega, int id_uh, const char *data,
                                  const char *inicio, const char *fim,
                                  const int ids_manchados[], int qtd_manchados);
int salvar_distribuicao_csv(Camareira camareiras[], int quantidade, const char *data);
int distribuicao_pode_ser_recalculada(const char *data);
int existe_camareira_presente(Camareira camareiras[], int quantidade);
int adicionar_uh(Camareira *camareira, int uh);
int atribuir_titulares(Camareira camareiras[], int quantidade);
int encontrar_menor_carga(Camareira camareiras[], int quantidade);
int realocar_ausencias(Camareira camareiras[], int quantidade);
void imprimir_mapa(Camareira camareiras[], int quantidade, FILE *saida);
int encontrar_camareira(Camareira camareiras[], int quantidade, int id);
void calcular_indicadores(RegistroAtendimento registros[], int quantidade_registros,
                         Camareira camareiras[], int quantidade_camareiras, Indicador indicadores[]);
double calcular_tempo_medio(Indicador indicador);
void ordenar_indicadores(Indicador indicadores[], int quantidade);
void imprimir_indicadores(Indicador indicadores[], int quantidade, FILE *saida);
void tela_registro_faltas(Camareira camareiras[], int quantidade);
void tela_distribuicao(Camareira camareiras[], int quantidade);
void tela_produtividade(Camareira camareiras[], int quantidade);
void tela_pos_atendimento(Camareira camareiras[], int quantidade);
int preparar_resultados(void);
int iniciar_log(void);
void finalizar_log(void);
void limpar_console(void);
int salvar_backup_resultado(const char *arquivo);
int log_printf(const char *formato, ...);
int log_fprintf(FILE *destino, const char *formato, ...);
int log_puts(const char *texto);
int log_scanf(const char *formato, void *destino);

void tela_estoque(Camareira camareiras[], int quantidade);
void tela_relatorios(Camareira camareiras[], int quantidade);
void tela_chamados(Camareira camareiras[], int quantidade);
void log_registrar_entrada(const char *texto);

#endif
