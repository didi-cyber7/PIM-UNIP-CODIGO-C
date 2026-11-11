/* Menu principal: encaminha as telas e controla o ciclo do log oficial. */
#include "interno.h"

/* Carrega a equipe e executa o menu ate sair ou terminar a entrada. */
int main(void) {
    if (!iniciar_log()) {
        log_fprintf(stderr, "Erro: nao foi possivel abrir o log em resultados/.\n");
        return 1;
    }
    Camareira camareiras[MAX_CAMAREIRAS];
    int quantidade = carregar_camareiras(camareiras, MAX_CAMAREIRAS);
    if (quantidade <= 0) {
        log_puts("Nenhuma camareira carregada. Confira dados/config_camareiras.csv.");
        finalizar_log();
        return 1;
    }
    int primeira_tela = 1;
    for (;;) {
        if (!primeira_tela) limpar_console();
        primeira_tela = 0;
        log_puts("\n====== Sistema Le Jardin ======");
        log_puts("1) Distribuicao e substituicao de UHs");
        log_puts("2) Registro de faltas");
        log_puts("3) Relatorios");
        log_puts("4) Registro pos-atendimento");
        log_puts("5) Estoque e lavanderia");
        log_puts("6) Chamados");
        log_puts("0) Sair");
        int opcao;
        if (!ler_inteiro_prompt("Opcao: ", 0, 6, &opcao) || opcao == 0) break;
        switch (opcao) {
            case 1: tela_distribuicao(camareiras, quantidade); break;
            case 2: tela_registro_faltas(camareiras, quantidade); break;
            case 3: tela_relatorios(camareiras, quantidade); break;
            case 4: tela_pos_atendimento(camareiras, quantidade); break;
            case 5: tela_estoque(camareiras, quantidade); break;
            case 6: tela_chamados(camareiras, quantidade); break;
        }
    }
    log_puts("Encerrando o sistema.");
    finalizar_log();
    return 0;
}
