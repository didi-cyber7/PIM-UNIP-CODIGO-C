/* Operacoes de estoque e lavanderia usadas pelo menu principal e pelo consumo. */
#ifndef ESTOQUE_OPERACOES_H
#define ESTOQUE_OPERACOES_H

#include "projeto.h"

void tela_estoque(Camareira camareiras[], int quantidade);
void consultar_estoque(void);
void entrada_estoque(void);
void saida_estoque(void);
void criar_kit_estoque(Camareira camareiras[], int quantidade);
void enviar_lavanderia(void);
void receber_lavanderia(void);
void tela_avaliacao(void);
int resolver_avaliacao_item(int id_item, int baixar, const char *motivo,
                            const char *responsabilidade, int id_colaborador);
int baixar_item_com_movimento(int id_item, const char *motivo,
                             const char *responsabilidade, int id_colaborador);

#endif
