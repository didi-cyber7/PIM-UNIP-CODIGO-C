/* Ausencias diarias com inclusao e remocao; outras datas permanecem intactas. */
#include "interno.h"

/* Recebe a equipe e persiste o conjunto final de ausentes de hoje ao digitar zero. */
void tela_registro_faltas(Camareira camareiras[], int quantidade) {
    char data[11];
    obter_data_atual(data, sizeof data);
    if (!data[0]) return;
    carregar_faltas_do_dia(camareiras, quantidade, data);
    log_printf("\n=== Registro de faltas ===\nData: %s\n", data);
    for (int i = 0; i < quantidade; ++i) {
        log_printf("ID %d - %s [%s]\n", camareiras[i].id, camareiras[i].nome,
                   camareiras[i].presente ? "Presente" : "Ausente");
    }
    log_puts("ID marca falta; -ID remove; 0 salva.");
    for (;;) {
        int id;
        if (!ler_inteiro_prompt("ID: ", -INT_MAX, INT_MAX, &id)) {
            carregar_faltas_do_dia(camareiras, quantidade, data);
            return;
        }
        if (!id) break;
        int indice = encontrar_camareira(camareiras, quantidade, id < 0 ? -id : id);
        if (indice < 0) {
            log_puts("ID nao encontrado.");
            continue;
        }
        camareiras[indice].presente = id < 0;
        log_printf("%s para %s.\n", id < 0 ? "Falta removida" : "Falta marcada", camareiras[indice].nome);
    }
    if (salvar_faltas_do_dia(camareiras, quantidade, data)) log_puts("Faltas atualizadas.");
    else log_puts("Erro ao atualizar o CSV de faltas.");
}
