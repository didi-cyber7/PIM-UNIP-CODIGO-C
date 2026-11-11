/* Consumo e ocorrencias: valida em memoria e confirma os CSVs em conjunto. */
#include "interno.h"
#include <ctype.h>

#define ARQ_MOVIMENTOS "dados/movimentacao_estoque.csv"

/* Devolve 1 se o ID pertence ao vetor recebido, inclusive para vetores vazios. */
static int contem_id(const int ids[], int quantidade, int id) {
    for (int i = 0; i < quantidade; ++i) {
        if (ids[i] == id) return 1;
    }
    return 0;
}

/* Converte um campo positivo sem sobras; devolve 0 em overflow ou texto invalido. */
static int ler_id_positivo(const char *texto, int *id) {
    char *fim;
    errno = 0;
    long valor = strtol(texto, &fim, 10);
    if (fim == texto || *fim || errno || valor < 1 || valor > INT_MAX) return 0;
    *id = (int)valor;
    return 1;
}

/* Confere HH:MM rigorosamente; devolve minutos desde meia-noite ou -1 em erro. */
static int minutos_estritos(const char *horario) {
    if (!horario || strlen(horario) != 5 || horario[2] != ':' ||
        !isdigit((unsigned char)horario[0]) || !isdigit((unsigned char)horario[1]) ||
        !isdigit((unsigned char)horario[3]) || !isdigit((unsigned char)horario[4])) return -1;
    int hora = (horario[0] - '0') * 10 + horario[1] - '0';
    int minuto = (horario[3] - '0') * 10 + horario[4] - '0';
    return hora <= 23 && minuto <= 59 ? hora * 60 + minuto : -1;
}

/* Le linha inteira sem CR/LF; devolve -1 em truncamento, 0 em fim e 1 em sucesso. */
static int ler_linha_csv(FILE *arquivo, char *linha, size_t tamanho) {
    if (!fgets(linha, (int)tamanho, arquivo)) return ferror(arquivo) ? -1 : 0;
    if (!strchr(linha, '\n') && !feof(arquivo)) return -1;
    linha[strcspn(linha, "\r\n")] = '\0';
    return 1;
}

/* Copia historico validado para temporario aberto e opcionalmente devolve maior ID. */
static FILE *copiar_historico(const char *origem, const char *temporario, const char *cabecalho,
                             int colunas, int *maior_id) {
    FILE *entrada = fopen(origem, "r"), *saida;
    char linha[1024], copia[1024];
    int ok = 1, lido;
    if (maior_id) *maior_id = 0;
    if (!entrada && errno != ENOENT) return NULL;
    if (entrada && (ler_linha_csv(entrada, linha, sizeof linha) != 1 || strcmp(linha, cabecalho) != 0)) {
        fclose(entrada);
        return NULL;
    }
    saida = fopen(temporario, "w");
    if (!saida) {
        if (entrada) fclose(entrada);
        return NULL;
    }
    log_fprintf(saida, "%s\n", cabecalho);
    while (entrada && (lido = ler_linha_csv(entrada, linha, sizeof linha)) != 0) {
        char *campos[7];
        int id = 0;
        if (lido < 0) {
            ok = 0;
            break;
        }
        if (!linha[0]) continue;
        snprintf(copia, sizeof copia, "%s", linha);
        if (separar_csv(copia, campos, colunas) != colunas ||
            (maior_id && !ler_id_positivo(campos[0], &id))) {
            ok = 0;
            break;
        }
        if (maior_id && id > *maior_id) *maior_id = id;
        log_fprintf(saida, "%s\n", linha);
    }
    if (entrada && fclose(entrada) != 0) ok = 0;
    if (ferror(saida)) ok = 0;
    if (!ok) {
        fclose(saida);
        return NULL;
    }
    return saida;
}

/* Preserva entregas e preenche inicio somente na entrega pendente da camareira correta. */
static int preparar_entrega(int id_entrega, int id_camareira, const char *inicio, const char *temporario) {
    FILE *entrada = fopen(ARQ_ENTREGAS, "r"), *saida = fopen(temporario, "w");
    char linha[1024];
    int ok = entrada && saida, encontrados = 0, lido;
    if (ok && ler_linha_csv(entrada, linha, sizeof linha) != 1) ok = 0;
    if (ok) log_fprintf(saida, "id_entrega,id_camareira,id_responsavel_entrega,data_hora_entrega,conferido\n");
    while (ok && (lido = ler_linha_csv(entrada, linha, sizeof linha)) != 0) {
        char *c[5];
        int id, camareira, n;
        if (lido < 0) {
            ok = 0;
            break;
        }
        if (!linha[0]) continue;
        n = separar_csv(linha, c, 5);
        if ((n != 3 && n != 5) || !ler_id_positivo(c[0], &id) || !ler_id_positivo(c[1], &camareira)) {
            ok = 0;
            break;
        }
        const char *hora = n == 5 ? c[3] : c[2];
        if (id == id_entrega) {
            if (camareira != id_camareira || hora[0] || ++encontrados != 1) {
                ok = 0;
                break;
            }
            hora = inicio;
        }
        if (n == 5) log_fprintf(saida, "%s,%s,%s,%s,%s\n", c[0], c[1], c[2], hora, c[4]);
        else log_fprintf(saida, "%s,%s,%s,%s,0\n", c[0], c[1], c[1], hora);
    }
    if (entrada && fclose(entrada) != 0) ok = 0;
    if (saida) {
        if (ferror(saida)) ok = 0;
        if (fclose(saida) != 0) ok = 0;
    }
    return ok && encontrados == 1;
}

/* Preenche fim no mapa 3/4 colunas para contar a UH mesmo com todas as pecas faltantes. */
static int preparar_fim_entrega(int id_entrega, int id_uh, const char *data, const char *fim,
                               const char *temporario) {
    FILE *entrada = fopen(ARQ_MAPA_ENTREGA_UH, "r"), *saida = fopen(temporario, "w");
    char linha[1024];
    int ok = entrada && saida, encontrados = 0, lido;
    if (ok && ler_linha_csv(entrada, linha, sizeof linha) != 1) ok = 0;
    if (ok) log_fprintf(saida, "id_entrega,id_uh,data_atribuicao,data_hora_fim\n");
    while (ok && (lido = ler_linha_csv(entrada, linha, sizeof linha)) != 0) {
        char *c[4];
        int entrega, uh, n;
        if (lido < 0) {
            ok = 0;
            break;
        }
        if (!linha[0]) continue;
        n = separar_csv(linha, c, 4);
        if ((n != 3 && n != 4) || !ler_id_positivo(c[0], &entrega) || !ler_id_positivo(c[1], &uh)) {
            ok = 0;
            break;
        }
        const char *hora_fim = n == 4 ? c[3] : "";
        if (entrega == id_entrega) {
            if (uh != id_uh || strcmp(c[2], data) != 0 || hora_fim[0] || ++encontrados != 1) {
                ok = 0;
                break;
            }
            hora_fim = fim;
        }
        log_fprintf(saida, "%s,%s,%s,%s\n", c[0], c[1], c[2], hora_fim);
    }
    if (entrada && fclose(entrada) != 0) ok = 0;
    if (saida) {
        if (ferror(saida)) ok = 0;
        if (fclose(saida) != 0) ok = 0;
    }
    return ok && encontrados == 1;
}

/* Confere IDs do kit sem repeticao e exige um unico motivo por peca. */
static int validar_ocorrencias(const int kit[], int nk, const int manchados[], int nm,
                              const int avariados[], int na, const int faltantes[], int nf) {
    if (nm < 0 || na < 0 || nf < 0 || nm > nk || na > nk || nf > nk || nm + na + nf > nk ||
        (nm && !manchados) || (na && !avariados) || (nf && !faltantes)) return 0;
    const int *grupos[] = {manchados, avariados, faltantes};
    int tamanhos[] = {nm, na, nf};
    for (int g = 0; g < 3; ++g) {
        for (int i = 0; i < tamanhos[g]; ++i) {
            int id = grupos[g][i];
            if (!contem_id(kit, nk, id) || contem_id(grupos[g], i, id)) return 0;
            for (int anterior = 0; anterior < g; ++anterior) {
                if (contem_id(grupos[anterior], tamanhos[anterior], id)) return 0;
            }
        }
    }
    return 1;
}

/* Localiza IDs unicos no estoque e aplica ocorrencias apenas no vetor em memoria. */
static int preparar_itens(ItemEnxoval itens[], int ni, const int kit[], int nk,
                          const int manchados[], int nm, const int avariados[], int na,
                          const int faltantes[], int nf, int aplicar) {
    for (int k = 0; k < nk; ++k) {
        int indice = -1;
        if (contem_id(kit, k, kit[k])) return 0;
        for (int i = 0; i < ni; ++i) {
            if (itens[i].id_item != kit[k]) continue;
            if (indice >= 0) return 0;
            indice = i;
        }
        if (indice < 0) return 0;
        ItemEnxoval *item = &itens[indice];
        if (aplicar && strcmp(item->situacao, "em_uso") != 0) return 0;
        if (!aplicar) continue;
        if (contem_id(faltantes, nf, item->id_item)) {
            snprintf(item->situacao, sizeof item->situacao, "pendente_avaliacao");
            item->motivo_baixa[0] = '\0';
            item->responsavel_baixa[0] = '\0';
        } else if (contem_id(avariados, na, item->id_item)) {
            snprintf(item->situacao, sizeof item->situacao, "pendente_avaliacao");
        } else if (contem_id(manchados, nm, item->id_item)) {
            snprintf(item->situacao, sizeof item->situacao, "em_remocao_manchas");
        }
    }
    return 1;
}

/* Acrescenta consumo e ocorrencias; faltas geram pendencias, nunca baixas automaticas. */
static int escrever_novos_registros(FILE *consumo, FILE *ocorrencias, FILE *movimentos,
                                   const ItemEnxoval itens[], int ni, const int kit[], int nk,
                                   int id_entrega, int id_uh, int id_camareira, const char *data,
                                   const char *fim, const int manchados[], int nm,
                                   const int avariados[], int na, const int faltantes[], int nf,
                                   long long proximo_consumo, int registrar) {
    for (int k = 0; k < nk; ++k) {
        int faltante = contem_id(faltantes, nf, kit[k]);
        int manchado = contem_id(manchados, nm, kit[k]);
        int avariado = contem_id(avariados, na, kit[k]);
        if (!faltante) {
            log_fprintf(consumo, "%lld,%d,%d,%d,%s,%d\n", proximo_consumo++, id_entrega, id_uh,
                        kit[k], fim, manchado);
        }
        if (!registrar || (!faltante && !manchado && !avariado)) continue;
        const char *problema = faltante ? "faltante" : (manchado ? "manchado" : "avaria");
        for (int i = 0; i < ni; ++i) {
            if (itens[i].id_item == kit[k]) {
                log_fprintf(ocorrencias, "%s,%d,%d,%s,%s\n", data, id_camareira, id_uh,
                            itens[i].tag_rfid, problema);
                break;
            }
        }
    }
    return !ferror(consumo) && !ferror(ocorrencias) && !ferror(movimentos);
}

/* Prepara seis arquivos e confirma juntos; falhas preservam todos os CSVs oficiais. */
static int confirmar_atendimento(int id_entrega, int id_uh, int id_camareira,
                                 const char *data, const char *inicio, const char *fim,
                                 const int manchados[], int nm, const int avariados[], int na,
                                 const int faltantes[], int nf, int registrar) {
    const char *temporarios[] = {"dados/entrega_kit.consumo.tmp", "dados/consumo_uh.consumo.tmp",
        "dados/item_enxoval.consumo.tmp", "dados/registro_ocorrencias.consumo.tmp",
        "dados/movimentacao_estoque.consumo.tmp", "dados/entrega_kit_uh.consumo.tmp"};
    const char *destinos[] = {ARQ_ENTREGAS, ARQ_CONSUMOS, ARQ_ENXOVAL, ARQ_OCORRENCIAS,
                            ARQ_MOVIMENTOS, ARQ_MAPA_ENTREGA_UH};
    int kit[MAX_ITENS_POR_ENTREGA], id_pendente, ok = 0;
    int nk = carregar_ligacoes_entrega(id_entrega, kit, MAX_ITENS_POR_ENTREGA);
    int ini = minutos_estritos(inicio), final = minutos_estritos(fim);
    char hora_inicio[20], hora_fim[20];
    FILE *consumo = NULL, *ocorrencias = NULL, *movimentos = NULL;
    ItemEnxoval *itens = NULL;
    int max_consumo = 0, max_movimento = 0;
    if (!data || strlen(data) != 10 || ini < 0 || final <= ini || nk <= 0 ||
        !validar_ocorrencias(kit, nk, manchados, nm, avariados, na, faltantes, nf) ||
        !colaborador_existe(id_camareira) || tipo_atendimento_distribuido(id_camareira, id_uh, data) == 0 ||
        !buscar_entrega_uh(id_uh, id_camareira, data, &id_pendente) || id_pendente != id_entrega) return 0;
    snprintf(hora_inicio, sizeof hora_inicio, "%s %s:00", data, inicio);
    snprintf(hora_fim, sizeof hora_fim, "%s %s:00", data, fim);
    itens = calloc(MAX_ITENS_ENXOVAL, sizeof *itens);
    if (!itens) return 0;
    int ni = carregar_itens_estoque(itens, MAX_ITENS_ENXOVAL);
    if (ni <= 0 ||
        !preparar_itens(itens, ni, kit, nk, manchados, nm, avariados, na, faltantes, nf, registrar)) goto fim;
    if (!preparar_entrega(id_entrega, id_camareira, hora_inicio, temporarios[0]) ||
        !preparar_fim_entrega(id_entrega, id_uh, data, hora_fim, temporarios[5]) ||
        !salvar_itens_estoque(itens, ni, temporarios[2])) goto fim;
    consumo = copiar_historico(ARQ_CONSUMOS, temporarios[1],
        "id_consumo,id_entrega,id_uh,id_item,data_hora,peca_manchada", 6, &max_consumo);
    ocorrencias = copiar_historico(ARQ_OCORRENCIAS, temporarios[3],
        "data,id_camareira,id_uh,tag_rfid,problema", 5, NULL);
    movimentos = copiar_historico(ARQ_MOVIMENTOS, temporarios[4],
        "id_movimentacao,id_item,tipo_movimentacao,data_hora,id_colaborador,fornecedor,custo_unitario",
        7, &max_movimento);
    if (!consumo || !ocorrencias || !movimentos || max_consumo > INT_MAX - (nk - nf)) goto fim;
    ok = escrever_novos_registros(consumo, ocorrencias, movimentos, itens, ni, kit, nk,
        id_entrega, id_uh, id_camareira, data, hora_fim, manchados, nm, avariados, na,
        faltantes, nf, (long long)max_consumo + 1, registrar);
fim:
    if (consumo && fclose(consumo) != 0) ok = 0;
    if (ocorrencias && fclose(ocorrencias) != 0) ok = 0;
    if (movimentos && fclose(movimentos) != 0) ok = 0;
    if (ok) ok = confirmar_arquivos(temporarios, destinos, 6);
    if (!ok) {
        for (int i = 0; i < 6; ++i) remove(temporarios[i]);
    }
    free(itens);
    return ok;
}

/* Grava ocorrencias e pendencias junto com consumo e fim do atendimento. */
int registrar_consumo_com_ocorrencias(int id_entrega, int id_uh, int id_camareira,
                                     const char *data, const char *inicio, const char *fim,
                                     const int manchados[], int nm, const int avariados[], int na,
                                     const int faltantes[], int nf) {
    return confirmar_atendimento(id_entrega, id_uh, id_camareira, data, inicio, fim,
                                 manchados, nm, avariados, na, faltantes, nf, 1);
}

/* Mantem API antiga: grava flags de mancha sem gerar novas ocorrencias isoladas. */
int registrar_consumo_atendimento(int id_entrega, int id_uh, const char *data, const char *inicio,
                                  const char *fim, const int ids_manchados[], int qtd_manchados) {
    Entrega *entregas = calloc(MAX_ITENS_ENXOVAL, sizeof *entregas);
    int id_camareira = 0;
    if (!entregas) return 0;
    int ne = carregar_entregas(entregas, MAX_ITENS_ENXOVAL);
    for (int i = 0; i < ne; ++i) {
        if (entregas[i].id_entrega == id_entrega) id_camareira = entregas[i].id_camareira;
    }
    free(entregas);
    if (!id_camareira) return 0;
    return confirmar_atendimento(id_entrega, id_uh, id_camareira, data, inicio, fim,
                                 ids_manchados, qtd_manchados, NULL, 0, NULL, 0, 0);
}

/* Auxiliar legado: acrescenta ocorrencia isolada e informa erros de escrita/fechamento. */
int registrar_ocorrencia_csv(const char *data, int id_camareira, int id_uh, const char *rfid, const char *problema) {
    FILE *arquivo = fopen(ARQ_OCORRENCIAS, "a");
    if (!arquivo) return 0;
    log_fprintf(arquivo, "%s,%d,%d,%s,%s\n", data, id_camareira, id_uh, rfid, problema);
    int ok = !ferror(arquivo);
    if (fclose(arquivo) != 0) ok = 0;
    return ok;
}
