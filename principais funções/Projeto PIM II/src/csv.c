#include "interno.h"

/* Separa campos simples no proprio buffer; devolve quantidade ou -1 ao exceder o limite. */
int separar_csv(char *linha, char *campos[], int limite) {
    int n = 0;
    char *inicio = linha;
    for (char *p = linha;; ++p) {
        if (*p == ',' || *p == '\n' || *p == '\r' || *p == '\0') {
            char sep = *p;
            *p = '\0';
            if (n >= limite) return -1;
            campos[n++] = inicio;
            if (sep == '\0' || sep == '\n' || sep == '\r') break;
            inicio = p + 1;
        }
    }
    return n;
}

/* Converte uma linha CSV em peca; devolve 0 se faltarem campos ou identificacao. */
int ler_item_enxoval(char *linha, ItemEnxoval *item) {
    char *c[10];
    if (separar_csv(linha, c, 10) != 10) return 0;
    item->id_item = atoi(c[0]);
    snprintf(item->tag_rfid, sizeof item->tag_rfid, "%s", c[1]);
    snprintf(item->tipo, sizeof item->tipo, "%s", c[2]);
    snprintf(item->tamanho_cama, sizeof item->tamanho_cama, "%s", c[3]);
    snprintf(item->situacao, sizeof item->situacao, "%s", c[4]);
    snprintf(item->data_aquisicao, sizeof item->data_aquisicao, "%s", c[5]);
    item->ciclos_lavagem = atoi(c[6]);
    item->limite_ciclos = atoi(c[7]);
    snprintf(item->motivo_baixa, sizeof item->motivo_baixa, "%s", c[8]);
    snprintf(item->responsavel_baixa, sizeof item->responsavel_baixa, "%s", c[9]);
    return item->id_item > 0 && item->tag_rfid[0] != '\0';
}

/* Carrega todas as pecas no vetor; devolve -1 em linha invalida, excesso ou falha de leitura. */
int carregar_itens_estoque(ItemEnxoval itens[], int limite) {
    FILE *arq = fopen(ARQ_ENXOVAL, "r");
    char linha[512];
    int n = 0;
    if (!arq) return -1;
    if (!fgets(linha, sizeof linha, arq)) { fclose(arq); return -1; }
    while (fgets(linha, sizeof linha, arq)) {
        ItemEnxoval item;
        if (n >= limite || !ler_item_enxoval(linha, &item)) {
            fclose(arq);
            return -1;
        }
        itens[n++] = item;
    }
    if (ferror(arq)) { fclose(arq); return -1; }
    fclose(arq);
    return n;
}

/* Le as atribuicoes da data no vetor e devolve a quantidade ou -1 em erro. */
int carregar_atribuicoes(const char *data, Atribuicao atribuicoes[], int limite) {
    FILE *arq = fopen(ARQ_DISTRIBUICAO, "r");
    char linha[256];
    int n = 0;
    if (!arq) return -1;
    if (!fgets(linha, sizeof linha, arq)) { fclose(arq); return 0; }
    while (fgets(linha, sizeof linha, arq)) {
        Atribuicao a;
        char *c[5];
        if (separar_csv(linha, c, 5) != 5) { fclose(arq); return -1; }
        a.id = atoi(c[0]);
        a.id_camareira = atoi(c[1]);
        a.id_uh = atoi(c[2]);
        snprintf(a.data, sizeof a.data, "%s", c[3]);
        snprintf(a.tipo, sizeof a.tipo, "%s", c[4]);
        if (strcmp(a.data, data) == 0 && a.id_camareira > 0 && a.id_uh > 0) {
            if (n >= limite) { fclose(arq); return -1; }
            atribuicoes[n++] = a;
        }
    }
    if (ferror(arq)) { fclose(arq); return -1; }
    fclose(arq);
    return n;
}

/* Resolve UH e tipologia; preenche ID/tipo de cama e devolve 1 quando encontrados. */
int buscar_tipologia_uh(int id_uh, int *id_tipologia, char *tipo_cama, size_t tam_tipo) {
    FILE *arq = fopen(ARQ_UHS, "r");
    char linha[256];
    int encontrou = 0;
    if (!arq) return 0;
    if (!fgets(linha, sizeof linha, arq)) { fclose(arq); return 0; }
    while (fgets(linha, sizeof linha, arq)) {
        char *c[6];
        if (separar_csv(linha, c, 6) != 6) continue;
        if (atoi(c[0]) == id_uh) {
            *id_tipologia = atoi(c[5]);
            encontrou = 1;
            break;
        }
    }
    fclose(arq);
    if (!encontrou) return 0;

    arq = fopen(ARQ_TIPOLOGIAS, "r");
    if (!arq) return 0;
    if (!fgets(linha, sizeof linha, arq)) { fclose(arq); return 0; }
    encontrou = 0;
    while (fgets(linha, sizeof linha, arq)) {
        char *c[5];
        if (separar_csv(linha, c, 5) != 5) continue;
        if (atoi(c[0]) == *id_tipologia) {
            snprintf(tipo_cama, tam_tipo, "%s", c[2]);
            encontrou = 1;
            break;
        }
    }
    fclose(arq);
    return encontrou;
}

/* Carrega tipos e quantidades da tipologia e devolve a quantidade de composicoes. */
int carregar_composicoes(int id_tipologia, Composicao comp[], int limite) {
    FILE *arq = fopen(ARQ_COMPOSICAO, "r");
    char linha[256];
    int n = 0;
    if (!arq) return -1;
    if (!fgets(linha, sizeof linha, arq)) { fclose(arq); return 0; }
    while (fgets(linha, sizeof linha, arq)) {
        char *c[3];
        if (separar_csv(linha, c, 3) != 3) { fclose(arq); return -1; }
        if (atoi(c[0]) != id_tipologia) continue;
        if (n >= limite || atoi(c[2]) <= 0) { fclose(arq); return -1; }
        comp[n].id_tipologia = id_tipologia;
        snprintf(comp[n].tipo_item, sizeof comp[n].tipo_item, "%s", c[1]);
        comp[n].quantidade = atoi(c[2]);
        n++;
    }
    if (ferror(arq)) { fclose(arq); return -1; }
    fclose(arq);
    return n;
}

/* Percorre entregas e devolve o maior ID existente, ou zero na base vazia. */
int max_id_entrega(void) {
    FILE *arq = fopen(ARQ_ENTREGAS, "r");
    char linha[256];
    int maximo = 0;
    if (!arq) return 0;
    fgets(linha, sizeof linha, arq);
    while (fgets(linha, sizeof linha, arq)) {
        int id;
        if (sscanf(linha, "%d,", &id) == 1 && id > maximo) maximo = id;
    }
    fclose(arq);
    return maximo;
}

/* Percorre consumos e devolve o maior ID existente, ou zero na base vazia. */
int max_id_consumo(void) {
    FILE *arq = fopen(ARQ_CONSUMOS, "r");
    char linha[256];
    int maximo = 0;
    if (!arq) return 0;
    fgets(linha, sizeof linha, arq);
    while (fgets(linha, sizeof linha, arq)) {
        int id;
        if (sscanf(linha, "%d,", &id) == 1 && id > maximo) maximo = id;
    }
    fclose(arq);
    return maximo;
}

/* Consulta o historico e devolve 1 se alguma peca ja tiver consumo nessa entrega. */
int entrega_tem_consumo(int id_entrega) {
    FILE *arq = fopen(ARQ_CONSUMOS, "r");
    char linha[256];
    if (!arq) return 0;
    fgets(linha, sizeof linha, arq);
    while (fgets(linha, sizeof linha, arq)) {
        int id_consumo, id;
        if (sscanf(linha, "%d,%d,", &id_consumo, &id) == 2 && id == id_entrega) {
            fclose(arq); return 1;
        }
    }
    fclose(arq);
    return 0;
}

/* Le entregas no vetor, aceitando o formato antigo de tres campos; devolve quantidade ou -1. */
int carregar_entregas(Entrega entregas[], int limite) {
    FILE *arq = fopen(ARQ_ENTREGAS, "r");
    char linha[256];
    int n = 0;
    if (!arq) return -1;
    if (!fgets(linha, sizeof linha, arq)) { fclose(arq); return 0; }
    while (fgets(linha, sizeof linha, arq)) {
        char *c[5];
        int qtd = separar_csv(linha, c, 5);
        if (n >= limite || (qtd != 3 && qtd != 5)) { fclose(arq); return -1; }
        memset(&entregas[n], 0, sizeof entregas[n]);
        entregas[n].id_entrega = atoi(c[0]);
        entregas[n].id_camareira = atoi(c[1]);
        if (qtd >= 5) {
            entregas[n].id_responsavel = atoi(c[2]);
            snprintf(entregas[n].data_hora, sizeof entregas[n].data_hora, "%s", c[3]);
            entregas[n].conferido = atoi(c[4]);
        } else {
            
            entregas[n].id_responsavel = entregas[n].id_camareira;
            snprintf(entregas[n].data_hora, sizeof entregas[n].data_hora, "%s", c[2]);
            entregas[n].conferido = 0;
        }
        n++;
    }
    if (ferror(arq)) { fclose(arq); return -1; }
    fclose(arq);
    return n;
}

/* Le o mapa entrega/UH, aceitando tres campos antigos ou quatro com hora final. */
int carregar_mapa_entregas(MapaEntregaUH mapa[], int limite) {
    FILE *arq = fopen(ARQ_MAPA_ENTREGA_UH, "r");
    char linha[256];
    int n = 0;
    if (!arq) return -1;
    if (!fgets(linha, sizeof linha, arq)) { fclose(arq); return 0; }
    while (fgets(linha, sizeof linha, arq)) {
        char *c[4];
        int campos = separar_csv(linha, c, 4);
        if (n >= limite || (campos != 3 && campos != 4)) { fclose(arq); return -1; }
        memset(&mapa[n], 0, sizeof mapa[n]);
        mapa[n].id_entrega = atoi(c[0]);
        mapa[n].id_uh = atoi(c[1]);
        snprintf(mapa[n].data, sizeof mapa[n].data, "%s", c[2]);
        if (campos == 4) snprintf(mapa[n].data_hora_fim, sizeof mapa[n].data_hora_fim, "%s", c[3]);
        n++;
    }
    if (ferror(arq)) { fclose(arq); return -1; }
    fclose(arq);
    return n;
}

/* Localiza a entrega por ID e preenche seu inicio; devolve 0 se nao a encontrar. */
int buscar_data_hora_entrega(int id_entrega, char *data_hora, size_t tamanho) {
    Entrega *entregas = calloc(MAX_ITENS_ENXOVAL, sizeof *entregas);
    if (!entregas) return 0;
    int n = carregar_entregas(entregas, MAX_ITENS_ENXOVAL);
    int ok = 0;
    for (int i = 0; i < n; ++i) {
        if (entregas[i].id_entrega == id_entrega) {
            snprintf(data_hora, tamanho, "%s", entregas[i].data_hora);
            ok = 1;
            break;
        }
    }
    free(entregas);
    return ok;
}

/* Le os IDs fisicos vinculados a entrega para o vetor e devolve a quantidade. */
int carregar_ligacoes_entrega(int id_entrega, int ids[], int limite) {
    FILE *arq = fopen(ARQ_ENTREGA_ITENS, "r");
    char linha[128];
    int n = 0;
    if (!arq) return -1;
    if (!fgets(linha, sizeof linha, arq)) { fclose(arq); return 0; }
    while (fgets(linha, sizeof linha, arq)) {
        char *campos[2];
        if (separar_csv(linha, campos, 2) != 2 || atoi(campos[0]) <= 0 || atoi(campos[1]) <= 0) {
            fclose(arq);
            return -1;
        }
        if (atoi(campos[0]) == id_entrega) {
            if (n >= limite) { fclose(arq); return -1; }
            ids[n++] = atoi(campos[1]);
        }
    }
    if (ferror(arq)) { fclose(arq); return -1; }
    fclose(arq);
    return n;
}

/* Devolve 1 quando a entrega ja possui horario inicial, impedindo remontagem. */
int entrega_tem_data_hora(int id_entrega) {
    char data_hora[32];
    if (!buscar_data_hora_entrega(id_entrega, data_hora, sizeof data_hora)) return 0;
    return data_hora[0] != '\0';
}

/* Confere o formato basico de um timestamp CSV e devolve 1 quando reconhecido. */
int data_hora_valida(const char *valor) {
    return valor && strlen(valor) >= 19 && valor[4] == '-' && valor[7] == '-' && valor[10] == ' ' && valor[13] == ':';
}

/* Converte HH:MM em minutos desde meia-noite; devolve -1 se nao puder interpretar. */
int horario_para_minutos(const char *hora) {
    int h = 0, m = 0;
    if (!hora || sscanf(hora, "%d:%d", &h, &m) != 2) return -1;
    return h * 60 + m;
}
