/* Montagem de enxoval: seleciona somente em_estoque e confirma todos os vinculos juntos. */
#include "interno.h"

/* Confere se o ID de uma entrega pertence ao conjunto a substituir. */
static int entrega_removida(int id, const Entrega entregas[], const int remover[], int quantidade) {
    for (int i = 0; i < quantidade; ++i) {
        if (entregas[i].id_entrega == id && remover[i]) return 1;
    }
    return 0;
}

/* Compara tipo/tamanho normalizados sem modificar os dados legados. */
static int item_compativel(const ItemEnxoval *item, const Composicao *composicao, const char *cama) {
    const char *tipo = tipo_item_normalizado(composicao->tipo_item);
    if (strcmp(tipo_item_normalizado(item->tipo), tipo) != 0) return 0;
    int exige_tamanho = strcmp(tipo, "lencol_baixo") == 0 || strcmp(tipo, "lencol_cima") == 0 ||
                        strcmp(tipo, "capa_duvet") == 0;
    if (!exige_tamanho) return 1;
    const char *tamanho = strcmp(cama, "king") == 0 ? "king" : "viuvo";
    return strcmp(tamanho_item_normalizado(item), tamanho) == 0;
}

/* Escreve entregas preservadas e novas; devolve 0 em erro de escrita. */
static int escrever_entregas(const Entrega entregas[], int ne, const int remover[],
                            const Atribuicao atribuicoes[], int na, int primeiro_id) {
    FILE *arquivo = fopen("dados/entrega_kit.tmp", "w");
    if (!arquivo) return 0;
    log_fprintf(arquivo, "id_entrega,id_camareira,id_responsavel_entrega,data_hora_entrega,conferido\n");
    for (int i = 0; i < ne; ++i) {
        if (remover[i]) continue;
        log_fprintf(arquivo, "%d,%d,%d,%s,%d\n", entregas[i].id_entrega, entregas[i].id_camareira,
                    entregas[i].id_responsavel, entregas[i].data_hora, entregas[i].conferido);
    }
    for (int i = 0; i < na; ++i) {
        log_fprintf(arquivo, "%d,%d,%d,,0\n", primeiro_id + i,
                    atribuicoes[i].id_camareira, atribuicoes[i].id_camareira);
    }
    int ok = !ferror(arquivo);
    if (fclose(arquivo) != 0) ok = 0;
    return ok;
}

/* Preserva ligacoes de outras entregas e acrescenta os itens selecionados em memoria. */
static int escrever_ligacoes(const Entrega entregas[], int ne, const int remover[],
                            int (*ids)[MAX_ITENS_POR_ENTREGA], const int quantidades[], int na, int primeiro_id) {
    FILE *entrada = fopen(ARQ_ENTREGA_ITENS, "r");
    FILE *saida = fopen("dados/entrega_kit_item.tmp", "w");
    if (!entrada || !saida) {
        if (entrada) fclose(entrada);
        if (saida) fclose(saida);
        return 0;
    }
    char linha[256];
    log_fprintf(saida, "id_entrega,id_item\n");
    if (fgets(linha, sizeof linha, entrada)) {
        while (fgets(linha, sizeof linha, entrada)) {
            int entrega, item;
            if (sscanf(linha, "%d,%d", &entrega, &item) != 2) {
                fclose(entrada);
                fclose(saida);
                return 0;
            }
            if (!entrega_removida(entrega, entregas, remover, ne)) fputs(linha, saida);
        }
    }
    for (int i = 0; i < na; ++i) {
        for (int j = 0; j < quantidades[i]; ++j) {
            log_fprintf(saida, "%d,%d\n", primeiro_id + i, ids[i][j]);
        }
    }
    int ok = !ferror(entrada) && !ferror(saida);
    fclose(entrada);
    if (fclose(saida) != 0) ok = 0;
    return ok;
}

/* Regrava mapa entrega/UH preservando todas as outras datas e entregas. */
static int escrever_mapa_entregas(const MapaEntregaUH mapa[], int nm, const Entrega entregas[],
                                 int ne, const int remover[], const Atribuicao atribuicoes[], int na,
                                 int primeiro_id, const char *data) {
    FILE *arquivo = fopen("dados/entrega_kit_uh.tmp", "w");
    if (!arquivo) return 0;
    log_fprintf(arquivo, "id_entrega,id_uh,data_atribuicao,data_hora_fim\n");
    for (int i = 0; i < nm; ++i) {
        if (!entrega_removida(mapa[i].id_entrega, entregas, remover, ne)) {
            log_fprintf(arquivo, "%d,%d,%s,%s\n", mapa[i].id_entrega, mapa[i].id_uh,
                        mapa[i].data, mapa[i].data_hora_fim);
        }
    }
    for (int i = 0; i < na; ++i) {
        log_fprintf(arquivo, "%d,%d,%s,\n", primeiro_id + i, atribuicoes[i].id_uh, data);
    }
    int ok = !ferror(arquivo);
    if (fclose(arquivo) != 0) ok = 0;
    return ok;
}

/* Libera reservas antigas somente em memoria; qualquer atendimento impede remonte. */
static int preparar_remonte(const char *data, const MapaEntregaUH mapa[], int nm, const Entrega entregas[],
                           int ne, int remover[], ItemEnxoval itens[], int ni, int uh_manual) {
    int ids[MAX_ITENS_POR_ENTREGA];
    for (int m = 0; m < nm; ++m) {
        if (strcmp(mapa[m].data, data) != 0) continue;
        if (uh_manual) {
            if (mapa[m].id_uh == uh_manual) {
                log_puts("Ja existe uma entrega para esta UH na data. Nenhuma reserva foi criada.");
                return 0;
            }
            continue;
        }
        for (int e = 0; e < ne; ++e) {
            if (entregas[e].id_entrega != mapa[m].id_entrega) continue;
            if (entregas[e].data_hora[0] || entrega_tem_consumo(entregas[e].id_entrega)) {
                log_puts("Ja existe atendimento iniciado/concluido. Kits preservados.");
                return 0;
            }
            remover[e] = 1;
            int qtd = carregar_ligacoes_entrega(entregas[e].id_entrega, ids, MAX_ITENS_POR_ENTREGA);
            if (qtd < 0) return 0;
            for (int j = 0; j < qtd; ++j) {
                for (int i = 0; i < ni; ++i) {
                    if (itens[i].id_item == ids[j] && strcmp(itens[i].situacao, "em_uso") == 0) {
                        snprintf(itens[i].situacao, sizeof itens[i].situacao, "em_estoque");
                    }
                }
            }
        }
    }
    return 1;
}

/* Seleciona o kit inteiro; itens deixam em_estoque apenas no vetor ate a confirmacao final. */
static int selecionar_pecas(const Atribuicao *atribuicao, ItemEnxoval itens[], int ni,
                           int ids[], int *quantidade, int tipologia_exigida) {
    int tipologia;
    char cama[64];
    Composicao composicoes[MAX_COMPOSICOES];
    if (!buscar_tipologia_uh(atribuicao->id_uh, &tipologia, cama, sizeof cama) ||
        (tipologia_exigida && tipologia != tipologia_exigida)) {
        log_puts("UH inexistente ou tipologia diferente do kit escolhido.");
        return 0;
    }
    int nc = carregar_composicoes(tipologia, composicoes, MAX_COMPOSICOES);
    if (nc <= 0) return 0;
    *quantidade = 0;
    for (int c = 0; c < nc; ++c) {
        for (int q = 0; q < composicoes[c].quantidade; ++q) {
            int escolhido = -1;
            for (int i = 0; i < ni; ++i) {
                if (strcmp(itens[i].situacao, "em_estoque") == 0 &&
                    item_compativel(&itens[i], &composicoes[c], cama)) {
                    escolhido = i;
                    break;
                }
            }
            if (escolhido < 0 || *quantidade >= MAX_ITENS_POR_ENTREGA) {
                log_printf("Estoque insuficiente para UH %d: falta %s. Nenhum kit foi alterado.\n",
                           atribuicao->id_uh, composicoes[c].tipo_item);
                return 0;
            }
            ids[(*quantidade)++] = itens[escolhido].id_item;
            snprintf(itens[escolhido].situacao, sizeof itens[escolhido].situacao, "em_uso");
        }
    }
    return 1;
}

/* Monta todas as UHs do dia ou uma UH manual; devolve sucesso apos confirmar os quatro CSVs. */
static int montar_kits(const char *data, int uh_manual, int camareira_manual, int tipologia) {
    Atribuicao *atr = calloc(MAX_ATRIBUICOES_DIA, sizeof *atr);
    ItemEnxoval *itens = calloc(MAX_ITENS_ENXOVAL, sizeof *itens);
    MapaEntregaUH *mapa = calloc(MAX_ITENS_ENXOVAL, sizeof *mapa);
    Entrega *entregas = calloc(MAX_ITENS_ENXOVAL, sizeof *entregas);
    int *remover = calloc(MAX_ITENS_ENXOVAL, sizeof *remover);
    int (*ids)[MAX_ITENS_POR_ENTREGA] = calloc(MAX_ATRIBUICOES_DIA, sizeof *ids);
    int *quantidades = calloc(MAX_ATRIBUICOES_DIA, sizeof *quantidades);
    int ok = atr && itens && mapa && entregas && remover && ids && quantidades;
    int na = 0, ni = 0, nm = 0, ne = 0;
    if (ok) {
        na = carregar_atribuicoes(data, atr, MAX_ATRIBUICOES_DIA);
        ni = carregar_itens_estoque(itens, MAX_ITENS_ENXOVAL);
        nm = carregar_mapa_entregas(mapa, MAX_ITENS_ENXOVAL);
        ne = carregar_entregas(entregas, MAX_ITENS_ENXOVAL);
        ok = na > 0 && ni >= 0 && nm >= 0 && ne >= 0;
    }
    if (ok && uh_manual) {
        int encontrada = -1;
        for (int i = 0; i < na; ++i) {
            if (atr[i].id_uh == uh_manual && atr[i].id_camareira == camareira_manual) encontrada = i;
        }
        if (encontrada < 0) {
            log_puts("UH/camareira nao consta na distribuicao de hoje.");
            ok = 0;
        } else {
            atr[0] = atr[encontrada];
            na = 1;
        }
    }
    if (ok) ok = preparar_remonte(data, mapa, nm, entregas, ne, remover, itens, ni, uh_manual);
    for (int a = 0; ok && a < na; ++a) {
        ok = selecionar_pecas(&atr[a], itens, ni, ids[a], &quantidades[a], tipologia);
    }
    int primeiro_id = max_id_entrega() + 1;
    if (ok) {
        ok = salvar_itens_estoque(itens, ni, "dados/item_enxoval.tmp") &&
             escrever_entregas(entregas, ne, remover, atr, na, primeiro_id) &&
             escrever_ligacoes(entregas, ne, remover, ids, quantidades, na, primeiro_id) &&
             escrever_mapa_entregas(mapa, nm, entregas, ne, remover, atr, na, primeiro_id, data);
    }
    const char *temporarios[] = {"dados/item_enxoval.tmp", "dados/entrega_kit.tmp",
                                "dados/entrega_kit_item.tmp", "dados/entrega_kit_uh.tmp"};
    const char *destinos[] = {ARQ_ENXOVAL, ARQ_ENTREGAS, ARQ_ENTREGA_ITENS, ARQ_MAPA_ENTREGA_UH};
    if (ok) ok = confirmar_arquivos(temporarios, destinos, 4);
    if (ok) log_printf("Kits montados para %d UHs. Pecas reservadas e vinculadas as entregas.\n", na);
    else log_puts("Montagem nao concluida; confira estoque, distribuicao e arquivos.");
    for (int i = 0; i < 4; ++i) remove(temporarios[i]);
    free(atr);
    free(itens);
    free(mapa);
    free(entregas);
    free(remover);
    free(ids);
    free(quantidades);
    return ok;
}

/* Monta o enxoval de toda a distribuicao da data recebida. */
int montar_kits_distribuicao(const char *data) {
    return montar_kits(data, 0, 0, 0);
}

/* Reserva uma entrega manual para UH/camareira/tipologia, respeitando a distribuicao. */
int montar_kit_uh(int id_uh, int id_camareira, const char *data, int id_tipologia) {
    return montar_kits(data, id_uh, id_camareira, id_tipologia);
}

/* Localiza entrega pendente da UH/camareira/data; devolve seu ID por parametro. */
int buscar_entrega_uh(int id_uh, int id_camareira, const char *data, int *id_entrega) {
    MapaEntregaUH *mapa = calloc(MAX_ITENS_ENXOVAL, sizeof *mapa);
    Entrega *entregas = calloc(MAX_ITENS_ENXOVAL, sizeof *entregas);
    if (!mapa || !entregas) {
        free(mapa);
        free(entregas);
        return 0;
    }
    int nm = carregar_mapa_entregas(mapa, MAX_ITENS_ENXOVAL);
    int ne = carregar_entregas(entregas, MAX_ITENS_ENXOVAL);
    int ok = 0;
    for (int i = 0; i < nm; ++i) {
        if (mapa[i].id_uh != id_uh || strcmp(mapa[i].data, data) != 0) continue;
        for (int j = 0; j < ne; ++j) {
            if (entregas[j].id_entrega == mapa[i].id_entrega && entregas[j].id_camareira == id_camareira &&
                !entregas[j].data_hora[0] && !entrega_tem_consumo(entregas[j].id_entrega)) {
                *id_entrega = entregas[j].id_entrega;
                ok = 1;
            }
        }
    }
    free(mapa);
    free(entregas);
    return ok;
}

/* Carrega pecas fisicas da entrega; devolve quantidade ou -1 se houver vinculo invalido. */
int carregar_itens_entrega(int id_entrega, ItemEnxoval itens_saida[], int limite) {
    int ids[MAX_ITENS_POR_ENTREGA];
    ItemEnxoval *estoque = calloc(MAX_ITENS_ENXOVAL, sizeof *estoque);
    if (!estoque) return -1;
    int ni = carregar_ligacoes_entrega(id_entrega, ids, MAX_ITENS_POR_ENTREGA);
    int ne = carregar_itens_estoque(estoque, MAX_ITENS_ENXOVAL);
    int n = 0;
    for (int i = 0; i < ni && n < limite; ++i) {
        for (int j = 0; j < ne; ++j) {
            if (estoque[j].id_item == ids[i]) {
                itens_saida[n++] = estoque[j];
                break;
            }
        }
    }
    free(estoque);
    return ni < 0 || ne < 0 || n != ni ? -1 : n;
}

/* Atualiza uma situacao por ID usando temporario com rollback. */
int atualizar_situacao_item(int id_item, const char *nova_situacao) {
    ItemEnxoval *itens = calloc(MAX_ITENS_ENXOVAL, sizeof *itens);
    if (!itens) return 0;
    int n = carregar_itens_estoque(itens, MAX_ITENS_ENXOVAL);
    int encontrado = 0;
    for (int i = 0; i < n; ++i) {
        if (itens[i].id_item == id_item) {
            snprintf(itens[i].situacao, sizeof itens[i].situacao, "%s", nova_situacao);
            encontrado = 1;
        }
    }
    int ok = encontrado && salvar_itens_estoque(itens, n, "dados/item_enxoval.tmp");
    const char *temporarios[] = {"dados/item_enxoval.tmp"};
    const char *destinos[] = {ARQ_ENXOVAL};
    if (ok) ok = confirmar_arquivos(temporarios, destinos, 1);
    free(itens);
    return ok;
}
