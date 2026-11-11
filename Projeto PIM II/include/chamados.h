/* Operacoes dos chamados; a persistencia mantem os estados definidos no SQL. */
#ifndef CHAMADOS_H
#define CHAMADOS_H

#include "projeto.h"

int chamado_criar(int id_uh, const char *origem, Camareira camareiras[], int quantidade);
int chamado_listar(const char *status, Camareira camareiras[], int quantidade);
int chamado_avancar(int id_chamado, const char *status_atual, const char *novo_status);
void tela_chamados(Camareira camareiras[], int quantidade);

#endif
