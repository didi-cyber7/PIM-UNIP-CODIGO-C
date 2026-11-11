#include "interno.h"

/* Agrega atendimentos por camareira no vetor de saida, separando titular e substituicao. */
void calcular_indicadores(
    RegistroAtendimento registros[],
    int quantidade_registros,
    Camareira camareiras[],
    int quantidade_camareiras,
    Indicador indicadores[]
) {
    for (int i = 0; i < quantidade_camareiras; i++) {
        indicadores[i].id_camareira = camareiras[i].id;
        strcpy(indicadores[i].nome, camareiras[i].nome);
        indicadores[i].total_uhs = 0;
        indicadores[i].titulares = 0;
        indicadores[i].substituicoes = 0;
        indicadores[i].tempo_total = 0;
    }

    for (int i = 0; i < quantidade_registros; i++) {
        int indice = encontrar_camareira(
            camareiras,
            quantidade_camareiras,
            registros[i].id_camareira
        );

        if (indice == -1 ||
            registros[i].tempo_minutos < 0 ||
            (registros[i].tipo != TIPO_TITULAR &&
             registros[i].tipo != TIPO_SUBSTITUICAO)) {
            log_printf("Aviso: registro invalido ignorado (indice %d).\n", i);
            continue;
        }

        indicadores[indice].total_uhs++;
        indicadores[indice].tempo_total += registros[i].tempo_minutos;

        if (registros[i].tipo == TIPO_TITULAR) {
            indicadores[indice].titulares++;
        } else {
            indicadores[indice].substituicoes++;
        }
    }
}

/* Recebe o agregado da camareira e devolve minutos por UH, ou zero sem atendimentos. */
double calcular_tempo_medio(Indicador indicador) {
    if (indicador.total_uhs == 0) {
        return 0.0;
    }

    return (double) indicador.tempo_total / indicador.total_uhs;
}

/* Ordena o vetor pela media crescente com bolha; camareiras sem registros ficam ao final. */
void ordenar_indicadores(Indicador indicadores[], int quantidade) {
    for (int i = 0; i < quantidade - 1; i++) {
        for (int j = 0; j < quantidade - i - 1; j++) {
            double media_a = calcular_tempo_medio(indicadores[j]);
            double media_b = calcular_tempo_medio(indicadores[j + 1]);

            int trocar = 0;

            if (indicadores[j].total_uhs == 0 &&
                indicadores[j + 1].total_uhs > 0) {
                trocar = 1;
            } else if (indicadores[j].total_uhs > 0 &&
                       indicadores[j + 1].total_uhs > 0 &&
                       media_a > media_b) {
                trocar = 1;
            } else if (indicadores[j].total_uhs == 0 &&
                       indicadores[j + 1].total_uhs == 0 &&
                       indicadores[j].id_camareira >
                           indicadores[j + 1].id_camareira) {
                trocar = 1;
            }

            if (trocar) {
                Indicador temporario = indicadores[j];
                indicadores[j] = indicadores[j + 1];
                indicadores[j + 1] = temporario;
            }
        }
    }
}

/* Escreve o vetor de indicadores no fluxo de tela ou de arquivo recebido. */
void imprimir_indicadores(
    Indicador indicadores[],
    int quantidade,
    FILE *saida
) {
    log_fprintf(saida, "INDICADORES DE PRODUTIVIDADE\n");
    log_fprintf(saida, "============================\n\n");

    for (int i = 0; i < quantidade; i++) {
        log_fprintf(saida, "Camareira: %s (ID %d)\n",
                indicadores[i].nome,
                indicadores[i].id_camareira);

        log_fprintf(saida, "Total de UHs: %d\n",
                indicadores[i].total_uhs);

        log_fprintf(saida, "Titulares: %d\n",
                indicadores[i].titulares);

        log_fprintf(saida, "Substituicoes: %d\n",
                indicadores[i].substituicoes);

        if (indicadores[i].total_uhs > 0) {
            log_fprintf(saida, "Tempo medio por UH: %.2f minutos\n",
                    calcular_tempo_medio(indicadores[i]));
        } else {
            log_fprintf(saida, "Tempo medio por UH: sem registros\n");
        }

        log_fprintf(saida, "\n");
    }
}

/* Le o primeiro fim registrado por entrega para manter compatibilidade com os CSVs antigos. */
static int carregar_fins_consumo(const Entrega entregas[], int quantidade, char (*fins)[20]) {
    FILE *arquivo = fopen(ARQ_CONSUMOS, "r");
    char linha[512];
    if (!arquivo) return 0;
    if (fgets(linha, sizeof linha, arquivo)) {
        while (fgets(linha, sizeof linha, arquivo)) {
            char *campos[6];
            if (separar_csv(linha, campos, 6) != 6) {
                fclose(arquivo);
                return 0;
            }
            int id = atoi(campos[1]);
            for (int i = 0; i < quantidade; ++i) {
                if (entregas[i].id_entrega == id && !fins[i][0] && data_hora_valida(campos[4])) {
                    snprintf(fins[i], 20, "%s", campos[4]);
                    break;
                }
            }
        }
    }
    int ok = !ferror(arquivo);
    fclose(arquivo);
    return ok;
}

/* Reconstroi um atendimento por entrega, incluindo UH concluida com todas as pecas faltantes. */
int carregar_atendimentos(RegistroAtendimento registros[], int limite) {
    Entrega *entregas = calloc(MAX_ITENS_ENXOVAL, sizeof *entregas);
    MapaEntregaUH *mapa = calloc(MAX_ITENS_ENXOVAL, sizeof *mapa);
    char (*fins)[20] = calloc(MAX_ITENS_ENXOVAL, sizeof *fins);
    if (!entregas || !mapa || !fins) {
        free(entregas);
        free(mapa);
        free(fins);
        return -1;
    }
    int ne = carregar_entregas(entregas, MAX_ITENS_ENXOVAL);
    int nm = carregar_mapa_entregas(mapa, MAX_ITENS_ENXOVAL);
    int ok = ne >= 0 && nm >= 0 && carregar_fins_consumo(entregas, ne, fins);
    int n = 0;
    for (int i = 0; ok && i < ne; ++i) {
        if (!data_hora_valida(entregas[i].data_hora)) continue;
        int indice_mapa = -1;
        char data[11];
        memcpy(data, entregas[i].data_hora, 10);
        data[10] = '\0';
        for (int m = 0; m < nm; ++m) {
            if (mapa[m].id_entrega == entregas[i].id_entrega && strcmp(mapa[m].data, data) == 0) {
                indice_mapa = m;
                break;
            }
        }
        if (indice_mapa < 0) continue;
        const MapaEntregaUH *vinculo = &mapa[indice_mapa];
        const char *fim_atendimento = vinculo->data_hora_fim[0] ? vinculo->data_hora_fim : fins[i];
        if (!data_hora_valida(fim_atendimento) || strncmp(fim_atendimento, data, 10) != 0) continue;
        int tipo = tipo_atendimento_distribuido(entregas[i].id_camareira, vinculo->id_uh, data);
        int inicio = horario_para_minutos(entregas[i].data_hora + 11);
        int fim = horario_para_minutos(fim_atendimento + 11);
        if (!tipo || inicio < 0 || fim <= inicio) continue;
        if (n >= limite) {
            log_puts("Limite de atendimentos excedido. Relatorio cancelado para evitar historico parcial.");
            ok = 0;
            break;
        }
        registros[n].id_camareira = entregas[i].id_camareira;
        registros[n].id_uh = vinculo->id_uh;
        registros[n].tipo = tipo;
        registros[n].tempo_minutos = fim - inicio;
        ++n;
    }
    free(entregas);
    free(mapa);
    free(fins);
    return ok ? n : -1;
}
