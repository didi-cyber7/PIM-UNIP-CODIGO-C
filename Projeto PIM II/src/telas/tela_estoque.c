/* Menu de consulta, movimentacao, reserva de kits e ciclo de lavanderia. */
#include "interno.h"
#include "estoque_operacoes.h"

/* Agrupa envio e retorno; o retorno permite classificar as pecas por ID. */
static void tela_lavanderia(void) {
    for (;;) {
        int opcao;
        log_puts("\n=== Lavanderia ===");
        log_puts("1 - Ida / registrar envio");
        log_puts("2 - Volta / conferir pecas");
        log_puts("0 - Voltar");
        if (!ler_inteiro_prompt("Opcao: ", 0, 2, &opcao) || !opcao) return;
        if (opcao == 1) enviar_lavanderia();
        else receber_lavanderia();
    }
}

/* Recebe as camareiras carregadas e mantem o menu ate voltar ou encerrar a entrada. */
void tela_estoque(Camareira camareiras[], int quantidade) {
    int opcao;

    for (;;) {
        log_puts("\nESTOQUE E LAVANDERIA");
        log_puts("1 - Consultar estoque");
        log_puts("2 - Entrada de itens");
        log_puts("3 - Saida / baixa de item");
        log_puts("4 - Criar novo kit");
        log_puts("5 - Lavanderia");
        log_puts("6 - Fila de avaliacao");
        log_puts("0 - Voltar");

        if (!ler_inteiro_prompt("Opcao: ", 0, 6, &opcao) || opcao == 0) {
            return;
        }
        switch (opcao) {
            case 1: consultar_estoque(); break;
            case 2: entrada_estoque(); break;
            case 3: saida_estoque(); break;
            case 4: criar_kit_estoque(camareiras, quantidade); break;
            case 5: tela_lavanderia(); break;
            case 6: tela_avaliacao(); break;
        }
    }
}
