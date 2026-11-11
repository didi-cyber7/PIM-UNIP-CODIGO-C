/* Pos-atendimento: ocorrencias ficam em memoria ate a confirmacao conjunta. */
#include "interno.h"
#include <ctype.h>

/* Solicita HH:MM valido; devolve 0 em EOF sem modificar os CSVs. */
static int ler_horario(const char *mensagem, char horario[6]) {
    char entrada[64];
    for (;;) {
        if (!ler_texto_prompt(mensagem, entrada, sizeof entrada)) return 0;
        if (strlen(entrada) == 5 && entrada[2] == ':' &&
            isdigit((unsigned char)entrada[0]) && isdigit((unsigned char)entrada[1]) &&
            isdigit((unsigned char)entrada[3]) && isdigit((unsigned char)entrada[4])) {
            int h = (entrada[0] - '0') * 10 + entrada[1] - '0';
            int m = (entrada[3] - '0') * 10 + entrada[4] - '0';
            if (h <= 23 && m <= 59) {
                memcpy(horario, entrada, 6);
                return 1;
            }
        }
        log_puts("Horario invalido. Use HH:MM (exemplo: 08:30).");
    }
}

/* Converte horario previamente validado em minutos para conferir a ordem no mesmo dia. */
static int minutos(const char *horario) {
    return ((horario[0] - '0') * 10 + horario[1] - '0') * 60 +
           (horario[3] - '0') * 10 + horario[4] - '0';
}

/* Devolve 1 quando o ID ja foi selecionado na lista recebida. */
static int contem_id(const int ids[], int quantidade, int id) {
    for (int i = 0; i < quantidade; ++i) {
        if (ids[i] == id) return 1;
    }
    return 0;
}

/* Coleta um unico motivo por peca em memoria; devolve 0 em EOF sem salvar ocorrencias. */
static int coletar_ocorrencias(const ItemEnxoval itens[], int quantidade, int uh,
                               int manchados[], int *nm, int avariados[], int *na,
                               int faltantes[], int *nf) {
    int processados[MAX_ITENS_POR_ENTREGA], np = 0;
    log_puts("Avarias e faltas ficam pendentes de avaliacao; a baixa exige motivo e responsavel.");
    for (;;) {
        int escolha, problema, disponiveis = 0;
        int indices[MAX_ITENS_POR_ENTREGA];
        log_printf("\n--- Avarias e faltas | UH %d ---\n", uh);
        for (int i = 0; i < quantidade; ++i) {
            if (contem_id(processados, np, itens[i].id_item)) continue;
            indices[disponiveis] = i;
            log_printf("%d) ID %d | %s | RFID %s", ++disponiveis, itens[i].id_item,
                       itens[i].tipo, itens[i].tag_rfid);
            if (itens[i].tamanho_cama[0]) log_printf(" | %s", itens[i].tamanho_cama);
            log_printf("\n");
        }
        if (!disponiveis) return 1;
        log_puts("0) Finalizar selecao das ocorrencias");
        if (!ler_inteiro_prompt("Escolha uma peca: ", 0, disponiveis, &escolha)) return 0;
        if (!escolha) return 1;
        log_puts("Problema: 1) Manchado  2) Avaria  3) Faltante  0) Voltar");
        if (!ler_inteiro_prompt("Problema: ", 0, 3, &problema)) return 0;
        if (!problema) continue;
        int id = itens[indices[escolha - 1]].id_item;
        if (problema == 1) manchados[(*nm)++] = id;
        else if (problema == 2) avariados[(*na)++] = id;
        else faltantes[(*nf)++] = id;
        processados[np++] = id;
        log_printf("Peca %d selecionada como %s; sera salva ao confirmar o atendimento.\n", id,
                   problema == 1 ? "manchada" : (problema == 2 ? "avariada" : "faltante"));
    }
}

/* Valida distribuicao, coleta horarios/ocorrencias e confirma os arquivos de uma vez. */
void tela_pos_atendimento(Camareira camareiras[], int quantidade) {
    char data[11], inicio[6], fim[6];
    int id_camareira, uh, id_entrega, tipo, houve_ocorrencia, confirmar, atualizar;
    int manchados[MAX_ITENS_POR_ENTREGA], avariados[MAX_ITENS_POR_ENTREGA];
    int faltantes[MAX_ITENS_POR_ENTREGA], nm = 0, na = 0, nf = 0;
    ItemEnxoval itens[MAX_ITENS_POR_ENTREGA];
    obter_data_atual(data, sizeof data);
    if (!data[0]) {
        log_puts("Nao foi possivel obter a data atual.");
        return;
    }
    log_printf("\n=== Registro pos-atendimento ===\nData: %s\n", data);
    for (int i = 0; i < quantidade; ++i) log_printf("ID %d - %s\n", camareiras[i].id, camareiras[i].nome);
    if (!ler_inteiro_prompt("ID da camareira (0 cancela): ", 0, INT_MAX, &id_camareira) || !id_camareira) return;
    int indice = encontrar_camareira(camareiras, quantidade, id_camareira);
    if (indice < 0) {
        log_puts("Camareira nao encontrada.");
        return;
    }
    if (!ler_inteiro_prompt("Numero da UH (0 cancela): ", 0, INT_MAX, &uh) || !uh) return;
    tipo = tipo_atendimento_distribuido(id_camareira, uh, data);
    if (!tipo) {
        log_printf("A UH %d nao esta atribuida a %s na distribuicao de hoje.\n", uh, camareiras[indice].nome);
        return;
    }
    if (!buscar_entrega_uh(uh, id_camareira, data, &id_entrega)) {
        log_puts("Nenhum kit pendente para essa UH/camareira. Confira a distribuicao.");
        return;
    }
    int ni = carregar_itens_entrega(id_entrega, itens, MAX_ITENS_POR_ENTREGA);
    if (ni <= 0) {
        log_puts("A entrega nao possui itens validos. Nenhum atendimento foi registrado.");
        return;
    }
    if (!ler_horario("Horario inicial (HH:MM): ", inicio)) return;
    for (;;) {
        if (!ler_horario("Horario final (HH:MM): ", fim)) return;
        if (minutos(fim) > minutos(inicio)) break;
        log_puts("O horario final deve ser posterior ao inicial no mesmo dia.");
    }
    if (!ler_inteiro_prompt("Houve manchas, avarias ou faltas? (1 sim, 0 nao): ", 0, 1, &houve_ocorrencia)) return;
    if (houve_ocorrencia && !coletar_ocorrencias(itens, ni, uh, manchados, &nm, avariados, &na, faltantes, &nf)) return;
    log_printf("Atendimento: UH %d | %d min | %d manchas, %d avarias, %d faltantes.\n",
               uh, minutos(fim) - minutos(inicio), nm, na, nf);
    if (!ler_inteiro_prompt("Confirmar e salvar atendimento? (1 sim, 0 cancela): ", 0, 1, &confirmar) || !confirmar) {
        log_puts("Atendimento cancelado. Nenhum dado foi alterado.");
        return;
    }
    if (!registrar_consumo_com_ocorrencias(id_entrega, uh, id_camareira, data, inicio, fim,
                                           manchados, nm, avariados, na, faltantes, nf)) {
        log_puts("Nao foi possivel salvar o atendimento completo. Confira os CSVs e tente novamente.");
        return;
    }
    log_printf("Atendimento salvo: camareira %d, UH %d, tipo %s, duracao %d minutos.\n",
               id_camareira, uh, tipo == TIPO_TITULAR ? "titular" : "substituicao", minutos(fim) - minutos(inicio));
    if (!ler_inteiro_prompt("Atualizar indicadores agora? (1 sim, 0 nao): ", 0, 1, &atualizar)) return;
    if (atualizar) tela_produtividade(camareiras, quantidade);
}
