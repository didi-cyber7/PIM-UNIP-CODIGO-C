/* Avaliacao explicita de divergencias: localizar a peca ou confirmar baixa com autoria. */
#include "interno.h"
#include "estoque_operacoes.h"

/* Lista as pecas pendentes e solicita uma decisao por ID; cancelamento nao grava. */
void tela_avaliacao(void) {
    const char *motivos[] = {"", "perda", "avaria"};
    const char *responsaveis[] = {"", "hospede", "lavanderia", "equipe", "desgaste_natural"};
    for (;;) {
        ItemEnxoval *itens = calloc(MAX_ITENS_ENXOVAL, sizeof *itens);
        if (!itens) return;
        int n = carregar_itens_estoque(itens, MAX_ITENS_ENXOVAL);
        int pendentes = 0;
        if (n < 0) { free(itens); log_puts("Falha ao carregar a fila."); return; }
        log_puts("\n=== Fila de avaliacao ===");
        for (int i = 0; i < n; ++i) {
            if (strcmp(itens[i].situacao, "pendente_avaliacao") != 0) continue;
            log_printf("ID %d | RFID %s | %s | %s | ciclos %d/%d\n", itens[i].id_item,
                       itens[i].tag_rfid, itens[i].tipo, itens[i].tamanho_cama,
                       itens[i].ciclos_lavagem, itens[i].limite_ciclos);
            ++pendentes;
        }
        if (!pendentes) { free(itens); log_puts("Nenhuma peca pendente de avaliacao."); return; }
        int id, acao, motivo = 0, responsabilidade = 0, autor, confirmar;
        if (!ler_inteiro_prompt("ID da peca (0 volta): ", 0, INT_MAX, &id) || !id) { free(itens); return; }
        int valida = 0;
        for (int i = 0; i < n; ++i) {
            if (itens[i].id_item == id && strcmp(itens[i].situacao, "pendente_avaliacao") == 0) valida = 1;
        }
        free(itens);
        if (!valida) { log_puts("O ID informado nao esta na fila de avaliacao."); continue; }
        if (!ler_inteiro_prompt("1-Localizada / voltar ao estoque  2-Confirmar baixa  0-Cancelar: ", 0, 2, &acao)) return;
        if (!acao) continue;
        if (acao == 2) {
            if (!ler_inteiro_prompt("Motivo: 1-Perda 2-Avaria 0-Cancelar: ", 0, 2, &motivo)) return;
            if (!motivo) continue;
            if (!ler_inteiro_prompt("Responsavel: 1-Hospede 2-Lavanderia 3-Equipe 4-Desgaste natural 0-Cancelar: ",
                                    0, 4, &responsabilidade)) return;
            if (!responsabilidade) continue;
        }
        if (!ler_inteiro_prompt("ID do colaborador que avalia (0 cancela): ", 0, INT_MAX, &autor)) return;
        if (!autor) continue;
        if (!colaborador_existe(autor)) { log_puts("Colaborador nao cadastrado."); continue; }
        if (!ler_inteiro_prompt("Confirmar avaliacao? 1-Sim 0-Cancelar: ", 0, 1, &confirmar)) return;
        if (!confirmar) continue;
        if (resolver_avaliacao_item(id, acao == 2, motivos[motivo], responsaveis[responsabilidade], autor)) {
            log_puts(acao == 2 ? "Baixa confirmada com motivo e responsavel." : "Peca localizada e devolvida ao estoque.");
        } else log_puts("Avaliacao nao salva; dados anteriores preservados.");
    }
}
