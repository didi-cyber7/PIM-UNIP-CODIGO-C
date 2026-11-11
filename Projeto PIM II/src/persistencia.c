/* Substituicao coordenada dos CSVs: preserva os originais ate concluir o grupo. */
#include "interno.h"

/* Recebe um arquivo; devolve 1 quando pode ser aberto para leitura. */
static int arquivo_existe(const char *caminho) {
    FILE *arquivo = fopen(caminho, "rb");
    if (!arquivo) return 0;
    fclose(arquivo);
    return 1;
}

/* Confirma temporarios com rollback em erro de I/O; nao e transacao contra queda de energia. */
int confirmar_arquivos(const char *temporarios[], const char *destinos[], int quantidade) {
    char (*anteriores)[512] = calloc((size_t)quantidade, sizeof *anteriores);
    int *existiam = calloc((size_t)quantidade, sizeof *existiam);
    int *instalados = calloc((size_t)quantidade, sizeof *instalados);
    int preparados = 0;
    int ok = anteriores && existiam && instalados && quantidade > 0;
    for (int i = 0; ok && i < quantidade; ++i) {
        snprintf(anteriores[i], sizeof anteriores[i], "%s.rollback", destinos[i]);
        if (!arquivo_existe(temporarios[i]) || arquivo_existe(anteriores[i])) ok = 0;
    }
    for (int i = 0; ok && i < quantidade; ++i) {
        existiam[i] = arquivo_existe(destinos[i]);
        if (existiam[i] && rename(destinos[i], anteriores[i]) != 0) {
            ok = 0;
            break;
        }
        preparados = i + 1;
        if (rename(temporarios[i], destinos[i]) != 0) {
            ok = 0;
            break;
        }
        instalados[i] = 1;
    }
    if (!ok && anteriores && existiam && instalados) {
        for (int i = preparados - 1; i >= 0; --i) {
            if (instalados[i]) remove(destinos[i]);
            if (existiam[i] && rename(anteriores[i], destinos[i]) != 0) {
                log_printf("Falha ao restaurar %s; original preservado em %s.\n", destinos[i], anteriores[i]);
            }
        }
    }
    if (ok) {
        for (int i = 0; i < quantidade; ++i) {
            if (existiam[i]) remove(anteriores[i]);
        }
    }
    free(anteriores);
    free(existiam);
    free(instalados);
    return ok;
}

/* Escreve o vetor no temporario indicado; devolve 0 em qualquer erro de escrita/fechamento. */
int salvar_itens_estoque(const ItemEnxoval itens[], int quantidade, const char *temporario) {
    FILE *arquivo = fopen(temporario, "w");
    if (!arquivo) return 0;
    log_fprintf(arquivo, "id_item,tag_rfid,tipo,tamanho_cama,situacao,data_aquisicao,"
                "ciclos_lavagem,limite_ciclos,motivo_baixa,responsavel_baixa\n");
    for (int i = 0; i < quantidade; ++i) {
        const ItemEnxoval *item = &itens[i];
        log_fprintf(arquivo, "%d,%s,%s,%s,%s,%s,%d,%d,%s,%s\n", item->id_item, item->tag_rfid,
                    item->tipo, item->tamanho_cama, item->situacao, item->data_aquisicao,
                    item->ciclos_lavagem, item->limite_ciclos, item->motivo_baixa, item->responsavel_baixa);
    }
    int ok = !ferror(arquivo);
    if (fclose(arquivo) != 0) ok = 0;
    return ok;
}

/* Recebe tipo legado ou atual; devolve o nome usado pelas composicoes novas. */
const char *tipo_item_normalizado(const char *tipo) {
    return strcmp(tipo, "edredom_colcha") == 0 ? "capa_duvet" : tipo;
}

/* Normaliza apenas para consulta, preservando os valores originais dos CSVs. */
const char *tamanho_item_normalizado(const ItemEnxoval *item) {
    const char *tipo = tipo_item_normalizado(item->tipo);
    int cama = strcmp(tipo, "lencol_baixo") == 0 || strcmp(tipo, "lencol_cima") == 0 ||
               strcmp(tipo, "capa_duvet") == 0;
    if (!cama) return "";
    if (!item->tamanho_cama[0] || strcmp(item->tamanho_cama, "viúvo") == 0 ||
        strcmp(item->tamanho_cama, "viuvo") == 0) return "viuvo";
    return item->tamanho_cama;
}

/* Confere um ID no cadastro minimo; devolve 1 se o colaborador existe. */
int colaborador_existe(int id) {
    FILE *arquivo = fopen(ARQ_COLABORADORES, "r");
    char linha[512];
    int encontrado = 0;
    if (!arquivo) return 0;
    while (fgets(linha, sizeof linha, arquivo)) {
        int atual;
        if (sscanf(linha, "%d,", &atual) == 1 && atual == id) encontrado = 1;
    }
    fclose(arquivo);
    return encontrado;
}
