#ifndef RELATORIOS_H
#define RELATORIOS_H

#include "projeto.h"

int exportar_produtividade_csv(Camareira camareiras[], int quantidade);
void gerar_relatorio_gerencial(void);
void gerar_painel_estoque(void);
void gerar_lista_compras(void);
void configurar_parametros_estoque(void);
void tela_relatorios(Camareira camareiras[], int quantidade);

#endif
