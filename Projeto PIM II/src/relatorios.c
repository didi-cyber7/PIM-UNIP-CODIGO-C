/* Relatorios sobre o estado dos CSVs, sem integrar sensores nem presumir precos de compra. */
#include "interno.h"
#include "relatorios.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define MAX_GRUPOS_RELATORIO 128
#define MAX_TIPOLOGIAS_RELATORIO 16
#define SITUACOES_RELATORIO 7
#define RESPONSAVEIS_RELATORIO 5
#define ARQ_PARAMETROS_ESTOQUE "dados/parametros_estoque.csv"

static const char *situacoes_relatorio[SITUACOES_RELATORIO] = {
    "em_estoque", "em_uso", "em_lavanderia_comum", "em_remocao_manchas",
    "em_relavagem", "pendente_avaliacao", "baixado"
};

static const char *responsaveis_relatorio[RESPONSAVEIS_RELATORIO] = {
    "hospede", "lavanderia", "equipe", "desgaste_natural", "nao_informado"
};

typedef struct {
    char tipo[31];
    char tamanho[21];
    int cadastradas;
    int entregas;
    int perdas;
    int avarias;
    int afetadas;
    int esgotadas;
    int utilizaveis;
    int sem_limite;
    int tags;
    int desconhecidas;
    long ciclos;
    long limites;
    double percentual_utilizado;
    int preco_informado;
    int id_movimento_preco;
    char data_preco[32];
    double preco;
    int minimo;
    int sugestao;
    int ajustada;
    int baixas_responsavel[RESPONSAVEIS_RELATORIO][2]; /* Colunas: perda e avaria confirmadas. */
    int faltas_uh_pendentes;
    int faltas_lavanderia_pendentes;
    int pendencias_lavanderia;
    int acompanhamento_situacao[SITUACOES_RELATORIO];
} GrupoRelatorio;

typedef struct {
    ItemEnxoval itens[MAX_ITENS_ENXOVAL];
    int grupo_item[MAX_ITENS_ENXOVAL];
    unsigned char perda_item[MAX_ITENS_ENXOVAL];
    unsigned char avaria_item[MAX_ITENS_ENXOVAL];
    unsigned char falta_uh[MAX_ITENS_ENXOVAL];
    unsigned char falta_lavanderia[MAX_ITENS_ENXOVAL];
    unsigned char pendencia_lavanderia[MAX_ITENS_ENXOVAL];
    int ultima_entrega[MAX_ITENS_ENXOVAL];
    int ultimo_lote[MAX_ITENS_ENXOVAL];
    GrupoRelatorio grupos[MAX_GRUPOS_RELATORIO];
    /* Matriz real: linha = tipo/tamanho; coluna = uma das sete situacoes do enxoval.
     * O laco percorre as duas dimensoes para cruzar categorias e estados no painel. */
    int quantidades[MAX_GRUPOS_RELATORIO][SITUACOES_RELATORIO];
    int quantidade_itens;
    int quantidade_grupos;
    char gerado_em[32];
} DadosRelatorio;

typedef struct {
    int id;
    char nome[64];
    char tamanho[21];
    int quantidade_uhs;
    int trocas;
    int configurada;
} ParametroEstoque;

/* Encontra tipo/tamanho no vetor de grupos e cria uma linha da matriz quando necessario. */
static int encontrar_grupo(DadosRelatorio *dados, const char *tipo, const char *tamanho) {
    for (int i = 0; i < dados->quantidade_grupos; ++i) {
        if (strcmp(dados->grupos[i].tipo, tipo) == 0 &&
            strcmp(dados->grupos[i].tamanho, tamanho) == 0) return i;
    }
    if (dados->quantidade_grupos == MAX_GRUPOS_RELATORIO) return -1;
    int indice = dados->quantidade_grupos++;
    snprintf(dados->grupos[indice].tipo, sizeof dados->grupos[indice].tipo, "%s", tipo);
    snprintf(dados->grupos[indice].tamanho, sizeof dados->grupos[indice].tamanho, "%s", tamanho);
    return indice;
}

/* Localiza o indice de uma peca pelo ID; retorna -1 quando o historico referencia item ausente. */
static int encontrar_item_relatorio(const DadosRelatorio *dados, int id) {
    for (int i = 0; i < dados->quantidade_itens; ++i) {
        if (dados->itens[i].id_item == id) return i;
    }
    return -1;
}

/* Carrega entregas fisicas por peca para uma razao historica, sem inventar um periodo medio. */
static int carregar_entregas_relatorio(DadosRelatorio *dados) {
    FILE *arquivo = fopen("dados/entrega_kit_item.csv", "r");
    char linha[1024], *campos[4];
    if (!arquivo) return 0;
    if (!fgets(linha, sizeof linha, arquivo)) {
        fclose(arquivo);
        return 0;
    }
    while (fgets(linha, sizeof linha, arquivo)) {
        if (separar_csv(linha, campos, 4) < 2) continue;
        int indice = encontrar_item_relatorio(dados, atoi(campos[1]));
        if (indice >= 0) {
            dados->grupos[dados->grupo_item[indice]].entregas++;
            if (atoi(campos[0]) > dados->ultima_entrega[indice]) dados->ultima_entrega[indice] = atoi(campos[0]);
        }
    }
    int ok = !ferror(arquivo);
    fclose(arquivo);
    return ok;
}

/* Sinaliza faltas de UH ainda pendentes; ocorrencia nao confirma perda financeira. */
static int carregar_ocorrencias_relatorio(DadosRelatorio *dados) {
    FILE *arquivo = fopen("dados/registro_ocorrencias.csv", "r");
    char linha[1024], *campos[8];
    if (!arquivo) return 0;
    if (!fgets(linha, sizeof linha, arquivo)) {
        fclose(arquivo);
        return 0;
    }
    while (fgets(linha, sizeof linha, arquivo)) {
        if (separar_csv(linha, campos, 8) < 5) continue;
        for (int i = 0; i < dados->quantidade_itens; ++i) {
            if (strcmp(dados->itens[i].tag_rfid, campos[3]) != 0) continue;
            dados->falta_uh[i] = strcmp(dados->itens[i].situacao, "pendente_avaliacao") == 0 &&
                                strcmp(campos[4], "faltante") == 0;
            break;
        }
    }
    int ok = !ferror(arquivo);
    fclose(arquivo);
    return ok;
}

/* Identifica faltas e retrabalhos de lavanderia sem confundir pendencia com perda confirmada. */
static int carregar_inspecoes_relatorio(DadosRelatorio *dados) {
    FILE *arquivo = fopen("dados/item_lote.csv", "r");
    char linha[512], *campos[6];
    if (!arquivo) return 0;
    if (!fgets(linha, sizeof linha, arquivo)) {
        fclose(arquivo);
        return 0;
    }
    while (fgets(linha, sizeof linha, arquivo)) {
        int n = separar_csv(linha, campos, 6);
        if (n != 4 && n != 5) { fclose(arquivo); return 0; }
        int indice = encontrar_item_relatorio(dados, atoi(campos[1]));
        if (indice >= 0 && atoi(campos[0]) > dados->ultimo_lote[indice]) dados->ultimo_lote[indice] = atoi(campos[0]);
        int encerrado = n == 5 ? atoi(campos[4]) : (atoi(campos[2]) && strcmp(campos[3], "conforme") == 0);
        if (indice < 0 || encerrado) continue;
        const char *situacao = dados->itens[indice].situacao;
        if (strcmp(situacao, "em_estoque") == 0 || strcmp(situacao, "baixado") == 0) continue;
        dados->pendencia_lavanderia[indice] = 1;
        if (!atoi(campos[2]) && strcmp(situacao, "pendente_avaliacao") == 0) dados->falta_lavanderia[indice] = 1;
    }
    int ok = !ferror(arquivo);
    fclose(arquivo);
    return ok;
}

/* Recupera o preco mais recente de entrada_compra por grupo; campo vazio permanece desconhecido. */
static int carregar_precos_relatorio(DadosRelatorio *dados) {
    FILE *arquivo = fopen("dados/movimentacao_estoque.csv", "r");
    char linha[1024], *campos[10];
    if (!arquivo) return 0;
    if (!fgets(linha, sizeof linha, arquivo)) {
        fclose(arquivo);
        return 0;
    }
    while (fgets(linha, sizeof linha, arquivo)) {
        if (separar_csv(linha, campos, 10) < 7 || strcmp(campos[2], "entrada_compra") != 0)
            continue;
        if (campos[6][0] == '\0') continue;
        char *fim;
        double preco = strtod(campos[6], &fim);
        if (*fim != '\0' || !isfinite(preco) || preco < 0.0) continue;
        int indice = encontrar_item_relatorio(dados, atoi(campos[1]));
        if (indice < 0) continue;
        GrupoRelatorio *grupo = &dados->grupos[dados->grupo_item[indice]];
        int id = atoi(campos[0]);
        int data_mais_recente = strcmp(campos[3], grupo->data_preco);
        if (!grupo->preco_informado || data_mais_recente > 0 ||
            (data_mais_recente == 0 && id > grupo->id_movimento_preco)) {
            grupo->preco = preco;
            grupo->preco_informado = 1;
            grupo->id_movimento_preco = id;
            snprintf(grupo->data_preco, sizeof grupo->data_preco, "%s", campos[3]);
        }
    }
    int ok = !ferror(arquivo);
    fclose(arquivo);
    return ok;
}

/* Monta um retrato atualizado da base em memoria e devolve NULL se alguma leitura falhar. */
static DadosRelatorio *carregar_dados_relatorio(void) {
    DadosRelatorio *dados = calloc(1, sizeof *dados);
    if (!dados) {
        log_puts("Memoria insuficiente para gerar o relatorio.");
        return NULL;
    }
    dados->quantidade_itens = carregar_itens_estoque(dados->itens, MAX_ITENS_ENXOVAL);
    if (dados->quantidade_itens < 0) goto falha;
    obter_data_hora(dados->gerado_em, sizeof dados->gerado_em);
    for (int i = 0; i < dados->quantidade_itens; ++i) {
        ItemEnxoval *item = &dados->itens[i];
        int indice = encontrar_grupo(dados, tipo_item_normalizado(item->tipo),
                                    tamanho_item_normalizado(item));
        if (indice < 0) goto falha;
        dados->grupo_item[i] = indice;
        GrupoRelatorio *grupo = &dados->grupos[indice];
        grupo->cadastradas++;
        grupo->ciclos += item->ciclos_lavagem;
        if (item->tag_rfid[0]) grupo->tags++;
        int situacao = -1;
        for (int j = 0; j < SITUACOES_RELATORIO; ++j) {
            if (strcmp(item->situacao, situacoes_relatorio[j]) == 0) situacao = j;
        }
        if (situacao >= 0) dados->quantidades[indice][situacao]++;
        else grupo->desconhecidas++;
        int esgotada = item->limite_ciclos > 0 && item->ciclos_lavagem >= item->limite_ciclos;
        if (esgotada) grupo->esgotadas++;
        if (item->limite_ciclos > 0) {
            grupo->limites += item->limite_ciclos;
            grupo->percentual_utilizado += 100.0 * item->ciclos_lavagem / item->limite_ciclos;
        } else grupo->sem_limite++;
        if (!esgotada && situacao >= 0 && situacao <= 4) grupo->utilizaveis++;
        int baixado = strcmp(item->situacao, "baixado") == 0;
        dados->perda_item[i] = baixado && strcmp(item->motivo_baixa, "perda") == 0;
        dados->avaria_item[i] = baixado && strcmp(item->motivo_baixa, "avaria") == 0;
    }
    if (!carregar_entregas_relatorio(dados) || !carregar_ocorrencias_relatorio(dados) ||
        !carregar_inspecoes_relatorio(dados) ||
        !carregar_precos_relatorio(dados)) goto falha;
    for (int i = 0; i < dados->quantidade_itens; ++i) {
        GrupoRelatorio *grupo = &dados->grupos[dados->grupo_item[i]];
        grupo->perdas += dados->perda_item[i];
        grupo->avarias += dados->avaria_item[i];
        grupo->afetadas += dados->perda_item[i] || dados->avaria_item[i];
        grupo->faltas_uh_pendentes += dados->falta_uh[i] && !dados->pendencia_lavanderia[i];
        grupo->faltas_lavanderia_pendentes += dados->falta_lavanderia[i];
        grupo->pendencias_lavanderia += dados->pendencia_lavanderia[i];
        if (dados->pendencia_lavanderia[i]) {
            for (int s = 0; s < SITUACOES_RELATORIO; ++s) {
                if (strcmp(dados->itens[i].situacao, situacoes_relatorio[s]) == 0) grupo->acompanhamento_situacao[s]++;
            }
        }
        if (dados->perda_item[i] || dados->avaria_item[i]) {
            int r = RESPONSAVEIS_RELATORIO - 1;
            for (int j = 0; j < RESPONSAVEIS_RELATORIO - 1; ++j) {
                if (strcmp(dados->itens[i].responsavel_baixa, responsaveis_relatorio[j]) == 0) r = j;
            }
            grupo->baixas_responsavel[r][dados->avaria_item[i] ? 1 : 0]++;
        }
    }
    return dados;
falha:
    log_puts("Nao foi possivel ler a base completa para o relatorio; nenhum arquivo foi publicado.");
    free(dados);
    return NULL;
}

/* Abre dois temporarios, mantendo os relatorios oficiais intactos durante sua geracao. */
static int abrir_par_relatorio(const char *nome, FILE **txt, FILE **csv) {
    char caminho_txt[180], caminho_csv[180];
    if (!preparar_resultados()) return 0;
    snprintf(caminho_txt, sizeof caminho_txt, "resultados/%s.txt.tmp", nome);
    snprintf(caminho_csv, sizeof caminho_csv, "resultados/%s.csv.tmp", nome);
    *txt = fopen(caminho_txt, "w");
    *csv = fopen(caminho_csv, "w");
    if (*txt && *csv) return 1;
    if (*txt) fclose(*txt);
    if (*csv) fclose(*csv);
    remove(caminho_txt);
    remove(caminho_csv);
    log_puts("Nao foi possivel criar os arquivos temporarios do relatorio.");
    return 0;
}

/* Confere escrita, confirma TXT/CSV juntos e faz uma copia datada de cada formato em BKP. */
static int finalizar_par_relatorio(const char *nome, FILE *txt, FILE *csv) {
    char tmp_txt[180], tmp_csv[180], destino_txt[180], destino_csv[180];
    snprintf(tmp_txt, sizeof tmp_txt, "resultados/%s.txt.tmp", nome);
    snprintf(tmp_csv, sizeof tmp_csv, "resultados/%s.csv.tmp", nome);
    snprintf(destino_txt, sizeof destino_txt, "resultados/%s.txt", nome);
    snprintf(destino_csv, sizeof destino_csv, "resultados/%s.csv", nome);
    int ok = !ferror(txt) && !ferror(csv);
    if (fclose(txt) != 0) ok = 0;
    if (fclose(csv) != 0) ok = 0;
    const char *temporarios[] = {tmp_txt, tmp_csv};
    const char *destinos[] = {destino_txt, destino_csv};
    if (ok) ok = confirmar_arquivos(temporarios, destinos, 2);
    if (!ok) {
        remove(tmp_txt);
        remove(tmp_csv);
        log_puts("Falha ao salvar o relatorio TXT/CSV.");
        return 0;
    }
    int backup_txt = salvar_backup_resultado(destino_txt);
    int backup_csv = salvar_backup_resultado(destino_csv);
    if (!backup_txt || !backup_csv) log_puts("Aviso: um dos backups do relatorio nao foi criado.");
    log_printf("Arquivos compartilhaveis: %s e %s\n", destino_txt, destino_csv);
    return 1;
}

/* Imprime estimativa apenas quando ha preco conhecido para aquele tipo e tamanho. */
static void imprimir_estimativa(FILE *saida, const GrupoRelatorio *grupo, int quantidade) {
    if (grupo->preco_informado) log_fprintf(saida, "R$ %.2f", quantidade * grupo->preco);
    else log_fprintf(saida, "nao informado");
}

/* Ordena indices por numero de pecas afetadas, mantendo a estrutura dos grupos e da matriz. */
static void ordenar_grupos_por_perda(const DadosRelatorio *dados, int ordem[]) {
    for (int i = 0; i < dados->quantidade_grupos; ++i) ordem[i] = i;
    for (int i = 0; i < dados->quantidade_grupos; ++i) {
        for (int j = i + 1; j < dados->quantidade_grupos; ++j) {
            if (dados->grupos[ordem[j]].afetadas > dados->grupos[ordem[i]].afetadas) {
                int aux = ordem[i];
                ordem[i] = ordem[j];
                ordem[j] = aux;
            }
        }
    }
}

/* Escreve o gerencial com metrica de giro explicita, vida consumida e perdas sem duplicacao. */
static void escrever_gerencial_txt(FILE *saida, const DadosRelatorio *dados) {
    int ordem[MAX_GRUPOS_RELATORIO], sem_preco = 0;
    double subtotal = 0.0;
    ordenar_grupos_por_perda(dados, ordem);
    log_fprintf(saida, "GERENCIAL DE ENXOVAL | Gerado em %s\n", dados->gerado_em);
    log_fprintf(saida, "Giro = entregas fisicas registradas / pecas cadastradas; historico acumulado.\n");
    log_fprintf(saida, "Nao representa media de estoque nem taxa por periodo. Ordenacao: pecas afetadas.\n");
    log_fprintf(saida, "Perda/avaria: apenas baixas confirmadas; pendencias nao sao perdas financeiras confirmadas.\n");
    log_fprintf(saida, "Preco de referencia: ultima entrada_compra do tipo/tamanho, quando informada.\n\n");
    for (int i = 0; i < dados->quantidade_grupos; ++i) {
        const GrupoRelatorio *grupo = &dados->grupos[ordem[i]];
        int com_limite = grupo->cadastradas - grupo->sem_limite;
        log_fprintf(saida, "%s | %s | cadastradas: %d | entregas: %d | giro: %.3f\n",
                    grupo->tipo, grupo->tamanho[0] ? grupo->tamanho : "unico",
                    grupo->cadastradas, grupo->entregas,
                    grupo->cadastradas ? (double)grupo->entregas / grupo->cadastradas : 0.0);
        log_fprintf(saida, "  Perda: %d | avaria: %d | afetadas distintas: %d | esgotadas: %d\n",
                    grupo->perdas, grupo->avarias, grupo->afetadas, grupo->esgotadas);
        log_fprintf(saida, "  Ciclos medios: %.2f | limite medio informado: %.2f | vida consumida media: %.2f%%\n",
                    grupo->cadastradas ? (double)grupo->ciclos / grupo->cadastradas : 0.0,
                    com_limite ? (double)grupo->limites / com_limite : 0.0,
                    com_limite ? grupo->percentual_utilizado / com_limite : 0.0);
        log_fprintf(saida, "  Sem limite informado: %d | impacto estimado das perdas/avarias: ", grupo->sem_limite);
        imprimir_estimativa(saida, grupo, grupo->afetadas);
        log_fprintf(saida, "\n");
        log_fprintf(saida, "  Pendencias: faltas UH %d | faltas lavanderia %d | acompanhamento lavanderia %d\n",
                    grupo->faltas_uh_pendentes, grupo->faltas_lavanderia_pendentes, grupo->pendencias_lavanderia);
        for (int r = 0; r < RESPONSAVEIS_RELATORIO; ++r) {
            log_fprintf(saida, "  Responsavel %s: perdas %d | avarias %d | impacto ", responsaveis_relatorio[r],
                        grupo->baixas_responsavel[r][0], grupo->baixas_responsavel[r][1]);
            imprimir_estimativa(saida, grupo, grupo->baixas_responsavel[r][0] + grupo->baixas_responsavel[r][1]);
            log_fprintf(saida, "\n");
        }
        if (grupo->preco_informado) subtotal += grupo->afetadas * grupo->preco;
        else sem_preco += grupo->afetadas;
    }
    log_fprintf(saida, "\nSubtotal financeiro conhecido: R$ %.2f | pecas afetadas sem preco: %d\n", subtotal, sem_preco);
    log_fprintf(saida, "O subtotal nao e um total completo se houver pecas sem preco.\n");
}

/* Exporta grupos gerenciais em CSV; celulas de custo vazias indicam preco nao informado. */
static void escrever_gerencial_csv(FILE *saida, const DadosRelatorio *dados) {
    int ordem[MAX_GRUPOS_RELATORIO];
    ordenar_grupos_por_perda(dados, ordem);
    log_fprintf(saida, "gerado_em,tipo,tamanho,cadastradas,entregas_fisicas_historicas,giro_entregas_por_cadastrada,");
    log_fprintf(saida, "pecas_perda,pecas_avaria,pecas_afetadas_distintas,vida_esgotada,ciclos_medios,");
    log_fprintf(saida, "limite_medio_informado,vida_consumida_media_percentual,sem_limite,preco_referencia,impacto_estimado");
    log_fprintf(saida, ",faltas_uh_pendentes,faltas_lavanderia_pendentes,acompanhamento_lavanderia");
    for (int r = 0; r < RESPONSAVEIS_RELATORIO; ++r) {
        log_fprintf(saida, ",perdas_%s,avarias_%s,impacto_%s", responsaveis_relatorio[r],
                    responsaveis_relatorio[r], responsaveis_relatorio[r]);
    }
    log_fprintf(saida, "\n");
    for (int i = 0; i < dados->quantidade_grupos; ++i) {
        const GrupoRelatorio *grupo = &dados->grupos[ordem[i]];
        int com_limite = grupo->cadastradas - grupo->sem_limite;
        log_fprintf(saida, "%s,%s,%s,%d,%d,%.6f,%d,%d,%d,%d,%.3f,%.3f,%.3f,%d,",
                    dados->gerado_em, grupo->tipo, grupo->tamanho, grupo->cadastradas, grupo->entregas,
                    grupo->cadastradas ? (double)grupo->entregas / grupo->cadastradas : 0.0,
                    grupo->perdas, grupo->avarias, grupo->afetadas, grupo->esgotadas,
                    grupo->cadastradas ? (double)grupo->ciclos / grupo->cadastradas : 0.0,
                    com_limite ? (double)grupo->limites / com_limite : 0.0,
                    com_limite ? grupo->percentual_utilizado / com_limite : 0.0, grupo->sem_limite);
        if (grupo->preco_informado)
            log_fprintf(saida, "%.2f,%.2f", grupo->preco, grupo->preco * grupo->afetadas);
        else log_fprintf(saida, ",");
        log_fprintf(saida, ",%d,%d,%d", grupo->faltas_uh_pendentes, grupo->faltas_lavanderia_pendentes,
                    grupo->pendencias_lavanderia);
        for (int r = 0; r < RESPONSAVEIS_RELATORIO; ++r) {
            log_fprintf(saida, ",%d,%d,", grupo->baixas_responsavel[r][0], grupo->baixas_responsavel[r][1]);
            if (grupo->preco_informado) log_fprintf(saida, "%.2f",
                grupo->preco * (grupo->baixas_responsavel[r][0] + grupo->baixas_responsavel[r][1]));
        }
        log_fprintf(saida, "\n");
    }
}

/* Exporta baixas individualizadas com responsavel decidido e ultima UH/lote conhecidos. */
static void gerar_detalhe_responsaveis(const DadosRelatorio *dados) {
    MapaEntregaUH *mapa = calloc(MAX_ITENS_ENXOVAL, sizeof *mapa);
    Entrega *entregas = calloc(MAX_ITENS_ENXOVAL, sizeof *entregas);
    FILE *txt, *csv;
    if (!mapa || !entregas) { free(mapa); free(entregas); return; }
    int nm = carregar_mapa_entregas(mapa, MAX_ITENS_ENXOVAL);
    int ne = carregar_entregas(entregas, MAX_ITENS_ENXOVAL);
    if (nm < 0 || ne < 0 || !abrir_par_relatorio("perdas_por_responsavel", &txt, &csv)) {
        free(mapa);
        free(entregas);
        log_puts("Nao foi possivel gerar o detalhamento por responsavel.");
        return;
    }
    log_fprintf(txt, "BAIXAS POR RESPONSAVEL | %s\n", dados->gerado_em);
    log_fprintf(txt, "A responsabilidade vem da avaliacao. Ultima UH/lote sao contexto, sem atribuicao automatica de culpa.\n");
    log_fprintf(csv, "gerado_em,id_item,rfid,tipo,tamanho,motivo,responsavel,ultima_uh,ultima_camareira,ultimo_lote,");
    log_fprintf(csv, "preco_referencia,impacto_estimado\n");
    for (int r = 0; r < RESPONSAVEIS_RELATORIO; ++r) {
        for (int i = 0; i < dados->quantidade_itens; ++i) {
            if (!dados->perda_item[i] && !dados->avaria_item[i]) continue;
            const ItemEnxoval *item = &dados->itens[i];
            const GrupoRelatorio *grupo = &dados->grupos[dados->grupo_item[i]];
            int responsavel = RESPONSAVEIS_RELATORIO - 1;
            for (int j = 0; j < RESPONSAVEIS_RELATORIO - 1; ++j) {
                if (strcmp(item->responsavel_baixa, responsaveis_relatorio[j]) == 0) responsavel = j;
            }
            if (responsavel != r) continue;
            int uh = 0, camareira = 0;
            for (int m = 0; m < nm; ++m) {
                if (mapa[m].id_entrega == dados->ultima_entrega[i]) uh = mapa[m].id_uh;
            }
            for (int e = 0; e < ne; ++e) {
                if (entregas[e].id_entrega == dados->ultima_entrega[i]) camareira = entregas[e].id_camareira;
            }
            char texto_uh[16] = "", texto_camareira[16] = "", texto_lote[16] = "";
            if (uh) snprintf(texto_uh, sizeof texto_uh, "%d", uh);
            if (camareira) snprintf(texto_camareira, sizeof texto_camareira, "%d", camareira);
            if (dados->ultimo_lote[i]) snprintf(texto_lote, sizeof texto_lote, "%d", dados->ultimo_lote[i]);
            log_fprintf(txt, "%s | ID %d | RFID %s | %s | motivo %s | ultima UH %s | camareira %s | lote %s | impacto ",
                        responsaveis_relatorio[r], item->id_item, item->tag_rfid, grupo->tipo,
                        item->motivo_baixa, texto_uh, texto_camareira, texto_lote);
            imprimir_estimativa(txt, grupo, 1);
            log_fprintf(txt, "\n");
            log_fprintf(csv, "%s,%d,%s,%s,%s,%s,%s,%s,%s,%s,", dados->gerado_em, item->id_item,
                        item->tag_rfid, grupo->tipo, grupo->tamanho, item->motivo_baixa,
                        responsaveis_relatorio[r], texto_uh, texto_camareira, texto_lote);
            if (grupo->preco_informado) log_fprintf(csv, "%.2f,%.2f", grupo->preco, grupo->preco);
            else log_fprintf(csv, ",");
            log_fprintf(csv, "\n");
        }
    }
    finalizar_par_relatorio("perdas_por_responsavel", txt, csv);
    free(mapa);
    free(entregas);
}

/* Gera o gerencial, a separacao por responsavel e o detalhamento individualizado. */
void gerar_relatorio_gerencial(void) {
    DadosRelatorio *dados = carregar_dados_relatorio();
    FILE *txt, *csv;
    if (!dados) return;
    if (abrir_par_relatorio("gerencial_enxoval", &txt, &csv)) {
        escrever_gerencial_txt(txt, dados);
        escrever_gerencial_csv(csv, dados);
        if (finalizar_par_relatorio("gerencial_enxoval", txt, csv)) {
            escrever_gerencial_txt(stdout, dados);
            gerar_detalhe_responsaveis(dados);
        }
    }
    free(dados);
}

/* Exibe a matriz de estados e seus totais; RFID informa somente cadastro, sem telemetria. */
static void escrever_painel_txt(FILE *saida, const DadosRelatorio *dados) {
    int totais[SITUACOES_RELATORIO] = {0};
    int tags = 0, desconhecidas = 0;
    log_fprintf(saida, "PAINEL DE ESTOQUE | Retrato dos CSVs em %s\n", dados->gerado_em);
    log_fprintf(saida, "Atualizado a cada geracao; sem atualizacao automatica ou leitores conectados.\n");
    log_fprintf(saida, "Estados de lavanderia incluem pecas marcadas aguardando envio; nao sao contagem de lotes.\n");
    log_fprintf(saida, "Tipo | tamanho | disponivel | uso | lavagem comum | manchas | relavagem | pendente | baixado\n");
    for (int i = 0; i < dados->quantidade_grupos; ++i) {
        log_fprintf(saida, "%s | %s", dados->grupos[i].tipo,
                    dados->grupos[i].tamanho[0] ? dados->grupos[i].tamanho : "unico");
        for (int j = 0; j < SITUACOES_RELATORIO; ++j) {
            log_fprintf(saida, " | %d", dados->quantidades[i][j]);
            totais[j] += dados->quantidades[i][j];
        }
        log_fprintf(saida, "\n");
        tags += dados->grupos[i].tags;
        desconhecidas += dados->grupos[i].desconhecidas;
    }
    log_fprintf(saida, "TOTAL");
    for (int j = 0; j < SITUACOES_RELATORIO; ++j) log_fprintf(saida, " | %d", totais[j]);
    log_fprintf(saida, "\nPecas cadastradas: %d | estados desconhecidos: %d\n", dados->quantidade_itens, desconhecidas);
    log_fprintf(saida, "RFID: infraestrutura NAO MONITORADA; %d tags preenchidas, %d sem tag.\n",
                tags, dados->quantidade_itens - tags);
    log_fprintf(saida, "Tag cadastrada nao comprova leitura nem disponibilidade de equipamento.\n");
    for (int g = 0; g < dados->quantidade_grupos; ++g) {
        if (dados->grupos[g].faltas_lavanderia_pendentes) {
            log_fprintf(saida, "ALERTA: %s/%s possui %d pecas nao retornadas na fila de avaliacao.\n",
                        dados->grupos[g].tipo, dados->grupos[g].tamanho,
                        dados->grupos[g].faltas_lavanderia_pendentes);
        }
    }
}

/* Exporta cada cruzamento da matriz em uma linha, incluindo declaracao do estado RFID. */
static void escrever_painel_csv(FILE *saida, const DadosRelatorio *dados) {
    log_fprintf(saida, "gerado_em,tipo,tamanho,situacao,quantidade,infraestrutura_rfid,tags_preenchidas,sem_tag,");
    log_fprintf(saida, "faltas_uh_pendentes,faltas_lavanderia_pendentes,acompanhamento_lavanderia\n");
    for (int i = 0; i < dados->quantidade_grupos; ++i) {
        for (int j = 0; j < SITUACOES_RELATORIO; ++j) {
            log_fprintf(saida, "%s,%s,%s,%s,%d,nao_monitorada,%d,%d,%d,%d,%d\n", dados->gerado_em,
                        dados->grupos[i].tipo, dados->grupos[i].tamanho, situacoes_relatorio[j],
                        dados->quantidades[i][j], dados->grupos[i].tags,
                        dados->grupos[i].cadastradas - dados->grupos[i].tags,
                        j == 5 ? dados->grupos[i].faltas_uh_pendentes : 0,
                        j == 5 ? dados->grupos[i].faltas_lavanderia_pendentes : 0,
                        dados->grupos[i].acompanhamento_situacao[j]);
        }
        if (dados->grupos[i].desconhecidas)
            log_fprintf(saida, "%s,%s,%s,desconhecida,%d,nao_monitorada,%d,%d,%d,%d,%d\n", dados->gerado_em,
                        dados->grupos[i].tipo, dados->grupos[i].tamanho, dados->grupos[i].desconhecidas,
                        dados->grupos[i].tags, dados->grupos[i].cadastradas - dados->grupos[i].tags,
                        0, 0, 0);
    }
}

/* Gera painel em TXT/CSV consultando novamente a base a cada chamada. */
void gerar_painel_estoque(void) {
    DadosRelatorio *dados = carregar_dados_relatorio();
    FILE *txt, *csv;
    if (!dados) return;
    if (abrir_par_relatorio("painel_estoque", &txt, &csv)) {
        escrever_painel_txt(txt, dados);
        escrever_painel_csv(csv, dados);
        if (finalizar_par_relatorio("painel_estoque", txt, csv)) escrever_painel_txt(stdout, dados);
    }
    free(dados);
}

/* Le tipologias reais, aplicando a referencia inicial de 80 King, 20 Dupla e tres trocas. */
static int carregar_tipologias_parametros(ParametroEstoque parametros[]) {
    FILE *arquivo = fopen("dados/tipologia_uh.csv", "r");
    char linha[1024], *campos[8];
    int quantidade = 0;
    if (!arquivo) return -1;
    if (!fgets(linha, sizeof linha, arquivo)) {
        fclose(arquivo);
        return -1;
    }
    while (fgets(linha, sizeof linha, arquivo)) {
        if (separar_csv(linha, campos, 8) < 5 || quantidade >= MAX_TIPOLOGIAS_RELATORIO) {
            fclose(arquivo);
            return -1;
        }
        ParametroEstoque *parametro = &parametros[quantidade++];
        parametro->id = atoi(campos[0]);
        snprintf(parametro->nome, sizeof parametro->nome, "%s", campos[1]);
        if (strstr(campos[2], "king")) {
            snprintf(parametro->tamanho, sizeof parametro->tamanho, "king");
            parametro->quantidade_uhs = 80;
        } else if (strstr(campos[2], "vi")) {
            snprintf(parametro->tamanho, sizeof parametro->tamanho, "viuvo");
            parametro->quantidade_uhs = 20;
        } else {
            snprintf(parametro->tamanho, sizeof parametro->tamanho, "%.20s", campos[2]);
            parametro->quantidade_uhs = 0;
        }
        parametro->trocas = 3;
        parametro->configurada = 0;
    }
    int ok = !ferror(arquivo);
    fclose(arquivo);
    return ok && quantidade > 0 ? quantidade : -1;
}

/* Converte um campo CSV inteiro sem aceitar texto, sobras ou valores fora do intervalo. */
static int inteiro_parametro(const char *texto, int minimo, int maximo, int *valor) {
    char *fim;
    errno = 0;
    long numero = strtol(texto, &fim, 10);
    if (fim == texto || *fim || errno || numero < minimo || numero > maximo) return 0;
    *valor = (int)numero;
    return 1;
}

/* Salva somente a configuracao de calculo de minimo, sem modificar tipologia ou composicao. */
static int salvar_parametros_estoque(const ParametroEstoque parametros[], int quantidade) {
    const char *temporarios[] = {ARQ_PARAMETROS_ESTOQUE ".tmp"};
    const char *destinos[] = {ARQ_PARAMETROS_ESTOQUE};
    FILE *arquivo = fopen(temporarios[0], "w");
    if (!arquivo) return 0;
    log_fprintf(arquivo, "id_tipologia,quantidade_uhs,numero_trocas\n");
    for (int i = 0; i < quantidade; ++i)
        log_fprintf(arquivo, "%d,%d,%d\n", parametros[i].id, parametros[i].quantidade_uhs, parametros[i].trocas);
    int ok = !ferror(arquivo);
    if (fclose(arquivo) != 0) ok = 0;
    if (ok) ok = confirmar_arquivos(temporarios, destinos, 1);
    if (!ok) remove(temporarios[0]);
    return ok;
}

/* Carrega configuracao persistida ou inicializa a referencia apenas ao gerar/configurar compras. */
static int carregar_parametros_estoque(ParametroEstoque parametros[]) {
    int quantidade = carregar_tipologias_parametros(parametros);
    char linha[512], *campos[5];
    if (quantidade < 0) return -1;
    FILE *arquivo = fopen(ARQ_PARAMETROS_ESTOQUE, "r");
    if (!arquivo) {
        if (errno != ENOENT) return -1;
        log_puts("Configuracao inicial do minimo: referencia de 80 UHs King, 20 Dupla e tres trocas.");
        log_puts("Essa referencia e diferente das 15 UHs da demonstracao operacional; pode ser ajustada no menu.");
        return salvar_parametros_estoque(parametros, quantidade) ? quantidade : -1;
    }
    if (!fgets(linha, sizeof linha, arquivo)) {
        fclose(arquivo);
        return -1;
    }
    while (fgets(linha, sizeof linha, arquivo)) {
        int id, uhs, trocas;
        if (separar_csv(linha, campos, 5) != 3 ||
            !inteiro_parametro(campos[0], 1, INT_MAX, &id) ||
            !inteiro_parametro(campos[1], 0, 1000, &uhs) ||
            !inteiro_parametro(campos[2], 1, 30, &trocas)) {
            fclose(arquivo);
            return -1;
        }
        int encontrada = 0;
        for (int i = 0; i < quantidade; ++i) {
            if (parametros[i].id != id) continue;
            if (parametros[i].configurada) {
                fclose(arquivo);
                return -1;
            }
            parametros[i].quantidade_uhs = uhs;
            parametros[i].trocas = trocas;
            parametros[i].configurada = 1;
            encontrada = 1;
        }
        if (!encontrada) {
            fclose(arquivo);
            return -1;
        }
    }
    int ok = !ferror(arquivo);
    fclose(arquivo);
    for (int i = 0; i < quantidade; ++i) if (!parametros[i].configurada) ok = 0;
    return ok ? quantidade : -1;
}

/* Permite alterar UHs e numero de trocas de todas as tipologias, salvando ao concluir. */
void configurar_parametros_estoque(void) {
    ParametroEstoque parametros[MAX_TIPOLOGIAS_RELATORIO] = {0};
    int quantidade = carregar_parametros_estoque(parametros);
    if (quantidade < 0) {
        log_puts("Nao foi possivel carregar os parametros de estoque.");
        return;
    }
    log_puts("Minimo = composicao de cada kit x UHs configuradas x numero de trocas.");
    log_puts("A configuracao representa a capacidade planejada e pode diferir das UHs da demonstracao.");
    for (int i = 0; i < quantidade; ++i) {
        log_printf("\n%s | atual: %d UHs, %d trocas\n", parametros[i].nome,
                   parametros[i].quantidade_uhs, parametros[i].trocas);
        if (!ler_inteiro_prompt("Quantidade de UHs (0 a 1000): ", 0, 1000, &parametros[i].quantidade_uhs) ||
            !ler_inteiro_prompt("Numero de trocas (1 a 30): ", 1, 30, &parametros[i].trocas)) {
            log_puts("Configuracao cancelada; valores anteriores preservados.");
            return;
        }
    }
    log_puts(salvar_parametros_estoque(parametros, quantidade) ?
             "Parametros salvos em dados/parametros_estoque.csv." : "Falha ao salvar os parametros.");
}

/* Calcula minimos pela composicao CSV e desconta pecas utilizaveis sem somar esgotadas duas vezes. */
static int calcular_compras(DadosRelatorio *dados, const ParametroEstoque parametros[], int quantidade) {
    FILE *arquivo = fopen("dados/item_composicao_padrao.csv", "r");
    char linha[512], *campos[5];
    int composicoes_tipologia[MAX_TIPOLOGIAS_RELATORIO] = {0};
    if (!arquivo) return 0;
    if (!fgets(linha, sizeof linha, arquivo)) {
        fclose(arquivo);
        return 0;
    }
    while (fgets(linha, sizeof linha, arquivo)) {
        int id, itens_kit;
        if (separar_csv(linha, campos, 5) != 3 ||
            !inteiro_parametro(campos[0], 1, INT_MAX, &id) ||
            !inteiro_parametro(campos[2], 1, 1000, &itens_kit)) {
            fclose(arquivo);
            return 0;
        }
        for (int i = 0; i < quantidade; ++i) {
            if (parametros[i].id != id) continue;
            composicoes_tipologia[i]++;
            ItemEnxoval modelo = {0};
            snprintf(modelo.tipo, sizeof modelo.tipo, "%s", tipo_item_normalizado(campos[1]));
            int cama = strcmp(modelo.tipo, "lencol_baixo") == 0 || strcmp(modelo.tipo, "lencol_cima") == 0 ||
                       strcmp(modelo.tipo, "capa_duvet") == 0;
            if (cama) snprintf(modelo.tamanho_cama, sizeof modelo.tamanho_cama, "%s", parametros[i].tamanho);
            int grupo = encontrar_grupo(dados, modelo.tipo, tamanho_item_normalizado(&modelo));
            if (grupo < 0) {
                fclose(arquivo);
                return 0;
            }
            dados->grupos[grupo].minimo += itens_kit * parametros[i].quantidade_uhs * parametros[i].trocas;
        }
    }
    int ok = !ferror(arquivo);
    fclose(arquivo);
    for (int i = 0; i < quantidade; ++i) if (!composicoes_tipologia[i]) ok = 0;
    for (int i = 0; i < dados->quantidade_grupos; ++i) {
        GrupoRelatorio *grupo = &dados->grupos[i];
        grupo->sugestao = grupo->minimo > grupo->utilizaveis ? grupo->minimo - grupo->utilizaveis : 0;
        grupo->ajustada = grupo->sugestao;
    }
    return ok;
}

/* Mostra sugestoes e permite que a governanta substitua quantidades antes de exportar a lista. */
static int ajustar_compras(DadosRelatorio *dados) {
    int ajustar, escolha;
    for (int i = 0; i < dados->quantidade_grupos; ++i) {
        GrupoRelatorio *grupo = &dados->grupos[i];
        log_printf("%d) %s | %s | minimo %d | utilizaveis %d | sugestao %d\n", i + 1, grupo->tipo,
                   grupo->tamanho[0] ? grupo->tamanho : "unico", grupo->minimo, grupo->utilizaveis, grupo->sugestao);
    }
    if (!ler_inteiro_prompt("Ajustar manualmente como governanta? (1 sim, 0 nao): ", 0, 1, &ajustar)) return 0;
    if (!ajustar) return 1;
    for (;;) {
        if (!ler_inteiro_prompt("Numero do grupo para ajustar (0 conclui): ", 0,
                                dados->quantidade_grupos, &escolha)) return 0;
        if (escolha == 0) return 1;
        if (!ler_inteiro_prompt("Quantidade final de compra (0 a 1000000): ", 0, 1000000,
                                &dados->grupos[escolha - 1].ajustada)) return 0;
        log_printf("Quantidade ajustada para %d.\n", dados->grupos[escolha - 1].ajustada);
    }
}

/* Documenta formula, parametros e valores conhecidos para permitir revisao da compra sugerida. */
static void escrever_compras_txt(FILE *saida, const DadosRelatorio *dados,
                                 const ParametroEstoque parametros[], int quantidade) {
    double subtotal = 0.0;
    int sem_preco = 0;
    log_fprintf(saida, "LISTA DE COMPRAS | Gerada em %s\n", dados->gerado_em);
    log_fprintf(saida, "Minimo = composicao do kit x UHs planejadas x trocas configuradas:\n");
    for (int i = 0; i < quantidade; ++i)
        log_fprintf(saida, "  %s: %d UHs x %d trocas\n", parametros[i].nome,
                    parametros[i].quantidade_uhs, parametros[i].trocas);
    log_fprintf(saida, "Capacidade planejada independente das 15 UHs da demonstracao operacional.\n");
    log_fprintf(saida, "Utilizaveis = estoque + uso + lavanderia, com vida util restante.\n");
    log_fprintf(saida, "Baixadas, pendentes, estados desconhecidos e vida esgotada sao excluidos.\n");
    log_fprintf(saida, "Sem limite informado: consideradas utilizaveis, com essa limitacao registrada no gerencial.\n");
    log_fprintf(saida, "Sugestao = max(0; minimo - utilizaveis); esgotadas ja excluidas, sem soma duplicada.\n");
    log_fprintf(saida, "Preco: ultima entrada_compra do grupo. Ajustes manuais se aplicam apenas a esta lista.\n\n");
    for (int i = 0; i < dados->quantidade_grupos; ++i) {
        const GrupoRelatorio *grupo = &dados->grupos[i];
        log_fprintf(saida, "%s | %s | minimo %d | utilizaveis %d | esgotadas %d | sugerida %d | final %d | estimativa ",
                    grupo->tipo, grupo->tamanho[0] ? grupo->tamanho : "unico", grupo->minimo,
                    grupo->utilizaveis, grupo->esgotadas, grupo->sugestao, grupo->ajustada);
        imprimir_estimativa(saida, grupo, grupo->ajustada);
        log_fprintf(saida, "\n");
        if (grupo->preco_informado) subtotal += grupo->ajustada * grupo->preco;
        else sem_preco += grupo->ajustada;
    }
    log_fprintf(saida, "\nSubtotal financeiro conhecido: R$ %.2f | unidades de compra sem preco: %d\n", subtotal, sem_preco);
    log_fprintf(saida, "Custos desconhecidos permanecem nao informados; o subtotal pode ser incompleto.\n");
}

/* Exporta a sugestao original e a quantidade ajustada, mantendo campos financeiros desconhecidos vazios. */
static void escrever_compras_csv(FILE *saida, const DadosRelatorio *dados) {
    log_fprintf(saida, "gerado_em,tipo,tamanho,minimo_planejado,utilizaveis,vida_esgotada,");
    log_fprintf(saida, "quantidade_sugerida,quantidade_ajustada,preco_referencia,custo_estimado\n");
    for (int i = 0; i < dados->quantidade_grupos; ++i) {
        const GrupoRelatorio *grupo = &dados->grupos[i];
        log_fprintf(saida, "%s,%s,%s,%d,%d,%d,%d,%d,", dados->gerado_em, grupo->tipo, grupo->tamanho,
                    grupo->minimo, grupo->utilizaveis, grupo->esgotadas, grupo->sugestao, grupo->ajustada);
        if (grupo->preco_informado) log_fprintf(saida, "%.2f,%.2f", grupo->preco, grupo->preco * grupo->ajustada);
        else log_fprintf(saida, ",");
        log_fprintf(saida, "\n");
    }
}

/* Calcula a reposicao, permite revisao manual e publica TXT/CSV com copias em BKP. */
void gerar_lista_compras(void) {
    ParametroEstoque parametros[MAX_TIPOLOGIAS_RELATORIO] = {0};
    int quantidade = carregar_parametros_estoque(parametros);
    FILE *txt, *csv;
    if (quantidade < 0) {
        log_puts("Parametros de estoque invalidos; confira dados/parametros_estoque.csv e as tipologias.");
        return;
    }
    DadosRelatorio *dados = carregar_dados_relatorio();
    if (!dados) return;
    if (!calcular_compras(dados, parametros, quantidade)) {
        log_puts("Nao foi possivel calcular os minimos a partir da composicao dos kits.");
    } else if (!ajustar_compras(dados)) {
        log_puts("Lista de compras cancelada; arquivos anteriores preservados.");
    } else if (abrir_par_relatorio("lista_compras", &txt, &csv)) {
        escrever_compras_txt(txt, dados, parametros, quantidade);
        escrever_compras_csv(csv, dados);
        if (finalizar_par_relatorio("lista_compras", txt, csv))
            escrever_compras_txt(stdout, dados, parametros, quantidade);
    }
    free(dados);
}

/* Exporta a mesma agregacao historica de produtividade, sem introduzir filtros de turno ou periodo. */
int exportar_produtividade_csv(Camareira camareiras[], int quantidade) {
    RegistroAtendimento registros[MAX_REGISTROS];
    Indicador indicadores[MAX_CAMAREIRAS];
    int quantidade_registros = carregar_atendimentos(registros, MAX_REGISTROS);
    const char *temporarios[] = {"resultados/produtividade.csv.tmp"};
    const char *destinos[] = {"resultados/produtividade.csv"};
    if (quantidade_registros < 0 || quantidade < 0 || quantidade > MAX_CAMAREIRAS) return 0;
    calcular_indicadores(registros, quantidade_registros, camareiras, quantidade, indicadores);
    ordenar_indicadores(indicadores, quantidade);
    if (!preparar_resultados()) return 0;
    FILE *arquivo = fopen(temporarios[0], "w");
    if (!arquivo) return 0;
    log_fprintf(arquivo, "id_camareira,nome,total_uhs,titulares,substituicoes,tempo_total_minutos,tempo_medio_minutos\n");
    for (int i = 0; i < quantidade; ++i) {
        log_fprintf(arquivo, "%d,%s,%d,%d,%d,%d,%.2f\n", indicadores[i].id_camareira,
                    indicadores[i].nome, indicadores[i].total_uhs, indicadores[i].titulares,
                    indicadores[i].substituicoes, indicadores[i].tempo_total, calcular_tempo_medio(indicadores[i]));
    }
    int ok = !ferror(arquivo);
    if (fclose(arquivo) != 0) ok = 0;
    if (ok) ok = confirmar_arquivos(temporarios, destinos, 1);
    if (!ok) {
        remove(temporarios[0]);
        return 0;
    }
    if (!salvar_backup_resultado(destinos[0])) log_puts("Aviso: backup de produtividade.csv nao foi criado.");
    return 1;
}
