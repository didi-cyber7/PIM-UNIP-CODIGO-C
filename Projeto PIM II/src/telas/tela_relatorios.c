/* Menu de consultas e exportacoes; produtividade continua agregando todo o historico. */
#include "interno.h"
#include "relatorios.h"

/* Recebe as camareiras carregadas e encaminha a opcao, ate retornar ao menu principal. */
void tela_relatorios(Camareira camareiras[], int quantidade) {
    int opcao;
    for (;;) {
        log_puts("\n=== Relatorios ===");
        log_puts("1) Indicadores de produtividade");
        log_puts("2) Gerencial: giro, perdas, avarias, vida util e custos estimados");
        log_puts("3) Painel de estoque e cadastro RFID");
        log_puts("4) Lista de compras sugerida e ajuste da governanta");
        log_puts("5) Configurar UHs e trocas para estoque minimo");
        log_puts("0) Voltar");
        if (!ler_inteiro_prompt("Escolha: ", 0, 5, &opcao) || opcao == 0) return;
        switch (opcao) {
            case 1: tela_produtividade(camareiras, quantidade); break;
            case 2: gerar_relatorio_gerencial(); break;
            case 3: gerar_painel_estoque(); break;
            case 4: gerar_lista_compras(); break;
            case 5: configurar_parametros_estoque(); break;
        }
    }
}
