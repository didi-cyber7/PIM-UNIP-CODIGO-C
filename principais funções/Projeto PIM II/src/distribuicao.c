#include "interno.h"

/* Consulta o vetor da equipe e devolve 1 se houver pelo menos uma presente. */
int existe_camareira_presente(Camareira camareiras[], int quantidade) {
    for (int i = 0; i < quantidade; i++) {
        if (camareiras[i].presente) {
            return 1;
        }
    }

    return 0;
}

/* Acrescenta uma UH ao vetor da camareira; devolve 0 se exceder a capacidade. */
int adicionar_uh(Camareira *camareira, int uh) {
    if (camareira->quantidade_total >= MAX_UHS) {
        return 0;
    }

    camareira->uhs_atribuidas[camareira->quantidade_total] = uh;
    camareira->quantidade_total++;

    return 1;
}

/* Inicializa as cargas e atribui somente as UHs titulares das camareiras presentes. */
int atribuir_titulares(Camareira camareiras[], int quantidade) {
    for (int i = 0; i < quantidade; i++) {
        camareiras[i].quantidade_total = 0;

        if (!camareiras[i].presente) {
            continue;
        }

        for (int j = 0; j < camareiras[i].quantidade_titular; j++) {
            if (!adicionar_uh(&camareiras[i],
                              camareiras[i].uhs_titulares[j])) {
                log_printf("Erro: limite de UHs excedido.\n");
                return 0;
            }
        }
    }

    return 1;
}

/* Devolve o indice da presente com menor carga; empate pelo menor ID, ou -1 sem presentes. */
int encontrar_menor_carga(Camareira camareiras[], int quantidade) {
    int escolhida = -1;

    for (int i = 0; i < quantidade; i++) {
        if (!camareiras[i].presente) {
            continue;
        }

        if (escolhida == -1 ||
            camareiras[i].quantidade_total <
                camareiras[escolhida].quantidade_total ||
            (camareiras[i].quantidade_total ==
                camareiras[escolhida].quantidade_total &&
             camareiras[i].id < camareiras[escolhida].id)) {
            escolhida = i;
        }
    }

    return escolhida;
}

/* Redistribui as UHs das ausentes uma a uma; devolve 0 se nao houver destino valido. */
int realocar_ausencias(Camareira camareiras[], int quantidade) {
    for (int i = 0; i < quantidade; i++) {
        if (camareiras[i].presente) {
            continue;
        }

        for (int j = 0; j < camareiras[i].quantidade_titular; j++) {
            int destino = encontrar_menor_carga(camareiras, quantidade);

            if (destino == -1) {
                return 0;
            }

            if (!adicionar_uh(&camareiras[destino],
                              camareiras[i].uhs_titulares[j])) {
                log_printf("Erro: limite de UHs excedido.\n");
                return 0;
            }
        }
    }

    return 1;
}

/* Escreve o mapa da equipe no fluxo recebido, identificando titulares e substituicoes. */
void imprimir_mapa(Camareira camareiras[], int quantidade, FILE *saida) {
    log_fprintf(saida, "MAPA DIARIO DE DISTRIBUICAO\n");
    log_fprintf(saida, "===========================\n\n");

    for (int i = 0; i < quantidade; i++) {
        if (!camareiras[i].presente) {
            log_fprintf(saida, "Camareira %s (ID %d): AUSENTE\n\n",
                    camareiras[i].nome, camareiras[i].id);
            continue;
        }

        log_fprintf(saida, "Camareira: %s (ID %d)\n",
                camareiras[i].nome, camareiras[i].id);

        log_fprintf(saida, "Total de UHs: %d\n",
                camareiras[i].quantidade_total);

        for (int j = 0; j < camareiras[i].quantidade_total; j++) {
            int uh = camareiras[i].uhs_atribuidas[j];
            int titular = 0;

            for (int k = 0;
                 k < camareiras[i].quantidade_titular; k++) {
                if (camareiras[i].uhs_titulares[k] == uh) {
                    titular = 1;
                    break;
                }
            }

            log_fprintf(saida, "  UH %d - %s\n",
                    uh, titular ? "Titular" : "Substituicao");
        }

        log_fprintf(saida, "\n");
    }
}

/* Busca o ID no vetor da equipe e devolve seu indice, ou -1 quando nao existe. */
int encontrar_camareira(Camareira camareiras[], int quantidade, int id) {
    for (int i = 0; i < quantidade; i++) {
        if (camareiras[i].id == id) {
            return i;
        }
    }

    return -1;
}

/* Le o mapa auxiliar de titulares para o vetor; devolve quantidade ou -1 se nao abrir. */
int carregar_camareiras(Camareira camareiras[], int limite) {
    FILE *arquivo = fopen(ARQ_CONFIG_CAMAREIRAS, "r");
    char linha[256];
    int quantidade = 0;
    if (!arquivo) {
        log_printf("Erro ao abrir %s. Execute o programa na pasta raiz.\n", ARQ_CONFIG_CAMAREIRAS);
        return -1;
    }
    if (!fgets(linha, sizeof linha, arquivo)) { fclose(arquivo); return 0; }
    while (fgets(linha, sizeof linha, arquivo) && quantidade < limite) {
        char *id_txt, *nome, *uhs;
        char *campos[3];
        if (separar_csv(linha, campos, 3) != 3) continue;
        id_txt = campos[0]; nome = campos[1]; uhs = campos[2];
        Camareira *c = &camareiras[quantidade];
        memset(c, 0, sizeof(*c));
        c->id = atoi(id_txt);
        snprintf(c->nome, sizeof c->nome, "%s", nome);
        c->presente = 1;
        char *uh = strtok(uhs, ";");
        while (uh && c->quantidade_titular < MAX_UHS) {
            c->uhs_titulares[c->quantidade_titular++] = atoi(uh);
            uh = strtok(NULL, ";");
        }
        quantidade++;
    }
    fclose(arquivo);
    return quantidade;
}

/* Atualiza presencas do vetor pela data pedida e devolve o numero de ausencias encontradas. */
int carregar_faltas_do_dia(Camareira camareiras[], int quantidade, const char *data) {
    FILE *arquivo = fopen(ARQ_FALTAS, "r");
    char linha[128];
    int lidas = 0;
    for (int i = 0; i < quantidade; i++) camareiras[i].presente = 1;
    if (!arquivo) return 0;
    fgets(linha, sizeof(linha), arquivo);
    while (fgets(linha, sizeof(linha), arquivo)) {
        char data_csv[16];
            int id;
        if (sscanf(linha, "%15[^,],%d", data_csv, &id) == 2 && strcmp(data_csv, data) == 0) {
            int idx = encontrar_camareira(camareiras, quantidade, id);
            if (idx >= 0) { camareiras[idx].presente = 0; lidas++; }
        }
    }
    fclose(arquivo);
    return lidas;
}

/* Substitui o conjunto de faltas da data e preserva as demais; devolve sucesso da gravacao. */
int salvar_faltas_do_dia(Camareira camareiras[], int quantidade, const char *data) {
    FILE *entrada = fopen(ARQ_FALTAS, "r");
    FILE *temporario = fopen("dados/faltas.tmp", "w");
    char linha[128];
    if (!temporario) { if (entrada) fclose(entrada); return 0; }
    log_fprintf(temporario, "data,id_camareira\n");
    if (entrada) {
        fgets(linha, sizeof(linha), entrada);
        while (fgets(linha, sizeof(linha), entrada)) {
            char data_csv[16];
            int id;
            if (sscanf(linha, "%15[^,],%d", data_csv, &id) == 2) {
                if (strcmp(data_csv, data) != 0)
                    log_fprintf(temporario, "%s,%d\n", data_csv, id);
            }
        }
        fclose(entrada);
    }
    for (int i = 0; i < quantidade; i++)
        if (!camareiras[i].presente) log_fprintf(temporario, "%s,%d\n", data, camareiras[i].id);
    fclose(temporario);
    const char *temporarios[] = {"dados/faltas.tmp"};
    const char *destinos[] = {ARQ_FALTAS};
    return confirmar_arquivos(temporarios, destinos, 1);
}

/* Devolve 0 se alguma entrega da data ja tiver inicio ou consumo registrado. */
int distribuicao_pode_ser_recalculada(const char *data) {
    MapaEntregaUH *mapa = calloc(MAX_ITENS_ENXOVAL, sizeof *mapa);
    if (!mapa) return 0;
    int n = carregar_mapa_entregas(mapa, MAX_ITENS_ENXOVAL);
    if (n < 0) { free(mapa); return 0; }
    for (int i = 0; i < n; ++i) {
        if (strcmp(mapa[i].data, data) == 0 &&
            (entrega_tem_data_hora(mapa[i].id_entrega) || entrega_tem_consumo(mapa[i].id_entrega))) {
            free(mapa);
            return 0;
        }
    }
    free(mapa);
    return 1;
}

/* Prepara o CSV da data sem confirmar, permitindo gravar o mapa TXT no mesmo grupo. */
int preparar_distribuicao_csv(Camareira camareiras[], int quantidade, const char *data) {
    if (!distribuicao_pode_ser_recalculada(data)) {
        log_printf("Distribuicao bloqueada: ha atendimento iniciado/concluido nesta data.\n");
        return 0;
    }

    FILE *entrada = fopen(ARQ_DISTRIBUICAO, "r");
    FILE *saida = fopen("dados/atribuicao_diaria.tmp", "w");
    char linha[256];
    int proximo_id = 1;
    if (!saida) { if (entrada) fclose(entrada); return 0; }

    if (entrada) {
        if (fgets(linha, sizeof linha, entrada)) {
            while (fgets(linha, sizeof linha, entrada)) {
                int id;
                if (sscanf(linha, "%d,", &id) == 1 && id >= proximo_id)
                    proximo_id = id + 1;
            }
        }
        rewind(entrada);
    }

    log_fprintf(saida, "id_atribuicao,id_camareira,id_uh,data_atribuicao,tipo_atendimento\n");
    if (entrada && fgets(linha, sizeof linha, entrada)) {
        while (fgets(linha, sizeof linha, entrada)) {
            char copia[256], *c[5];
            snprintf(copia, sizeof copia, "%s", linha);
            if (separar_csv(copia, c, 5) == 5 && strcmp(c[3], data) != 0)
                fputs(linha, saida);
        }
        fclose(entrada);
    }

    for (int i = 0; i < quantidade; i++) {
        if (!camareiras[i].presente) continue;
        for (int j = 0; j < camareiras[i].quantidade_total; j++) {
            int uh = camareiras[i].uhs_atribuidas[j], titular = 0;
            for (int k = 0; k < camareiras[i].quantidade_titular; k++)
                if (camareiras[i].uhs_titulares[k] == uh) titular = 1;
            log_fprintf(saida, "%d,%d,%d,%s,%s\n", proximo_id++, camareiras[i].id, uh, data,
                    titular ? "TITULAR" : "SUBSTITUICAO");
        }
    }
    int ok = !ferror(saida);
    if (fclose(saida) != 0) ok = 0;
    return ok;
}

/* Persiste a distribuicao preparada; a tela pode confirmar junto com o mapa TXT. */
int salvar_distribuicao_csv(Camareira camareiras[], int quantidade, const char *data) {
    if (!preparar_distribuicao_csv(camareiras, quantidade, data)) return 0;
    const char *temporarios[] = {"dados/atribuicao_diaria.tmp"};
    const char *destinos[] = {ARQ_DISTRIBUICAO};
    return confirmar_arquivos(temporarios, destinos, 1);
}

/* Consulta UH/camareira/data e devolve TITULAR, SUBSTITUICAO ou zero sem atribuicao. */
int tipo_atendimento_distribuido(int id_camareira, int id_uh, const char *data) {
    Atribuicao *atribuicoes = calloc(MAX_ATRIBUICOES_DIA, sizeof *atribuicoes);
    if (!atribuicoes) return 0;
    int n = carregar_atribuicoes(data, atribuicoes, MAX_ATRIBUICOES_DIA), tipo = 0;
    for (int i = 0; i < n; ++i) {
        if (atribuicoes[i].id_camareira == id_camareira && atribuicoes[i].id_uh == id_uh) {
            if (strcmp(atribuicoes[i].tipo, "TITULAR") == 0) tipo = TIPO_TITULAR;
            else if (strcmp(atribuicoes[i].tipo, "SUBSTITUICAO") == 0) tipo = TIPO_SUBSTITUICAO;
            break;
        }
    }
    free(atribuicoes);
    return tipo;
}
