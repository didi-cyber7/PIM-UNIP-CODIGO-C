/* Menu para abertura e acompanhamento de chamados de governanca. */
#include "chamados.h"
#include "interno.h"
#include <limits.h>

/* Solicita UH/origem e abre chamado; zero ou fim da entrada cancelam a operacao. */
static void abrir_chamado(Camareira camareiras[], int quantidade) {
    int id_uh, origem;
    if (!ler_inteiro_prompt("ID da UH (0 cancela): ", 0, INT_MAX, &id_uh) || !id_uh) return;
    log_puts("Origem: 1) Recepcao  2) Hack  0) Cancelar");
    if (!ler_inteiro_prompt("Origem: ", 0, 2, &origem) || !origem) return;
    chamado_criar(id_uh, origem == 1 ? "recepcao" : "hack", camareiras, quantidade);
}

/* Lista o estado escolhido e solicita o ID a avancar; nenhum registro e apagado. */
static void avancar_chamado(const char *atual, const char *proximo, Camareira camareiras[], int quantidade) {
    int id;
    if (chamado_listar(atual, camareiras, quantidade) <= 0) return;
    if (!ler_inteiro_prompt("ID do chamado para atualizar (0 cancela): ", 0, INT_MAX, &id) || !id) return;
    chamado_avancar(id, atual, proximo);
}

/* Exibe o menu de chamados; recebe o cadastro para validar e mostrar responsaveis. */
void tela_chamados(Camareira camareiras[], int quantidade) {
    int opcao;
    for (;;) {
        log_puts("\n====== Chamados ======");
        log_puts("1) Novo chamado");
        log_puts("2) Em aberto -> colocar em andamento");
        log_puts("3) Em andamento -> fechar chamado");
        log_puts("4) Chamados fechados");
        log_puts("0) Voltar");
        if (!ler_inteiro_prompt("Opcao: ", 0, 4, &opcao) || !opcao) return;
        if (opcao == 1) abrir_chamado(camareiras, quantidade);
        else if (opcao == 2) avancar_chamado("aberto", "em_atendimento", camareiras, quantidade);
        else if (opcao == 3) avancar_chamado("em_atendimento", "concluido", camareiras, quantidade);
        else chamado_listar("concluido", camareiras, quantidade);
    }
}
