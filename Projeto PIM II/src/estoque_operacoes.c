/* Estoque fisico, movimentacoes auditaveis e envio/retorno parcial de lavanderia. */
#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include "interno.h"
#include "estoque_operacoes.h"

#define ARQ_ITENS "dados/item_enxoval.csv"
#define ARQ_MOVIMENTOS "dados/movimentacao_estoque.csv"
#define ARQ_LOTES "dados/lote_lavanderia.csv"
#define ARQ_ITEM_LOTE "dados/item_lote.csv"
#define TMP_ITENS "dados/item_enxoval.estoque.tmp"
#define TMP_MOVIMENTOS "dados/movimentacao_estoque.estoque.tmp"
#define TMP_LOTES "dados/lote_lavanderia.estoque.tmp"
#define TMP_ITEM_LOTE "dados/item_lote.estoque.tmp"
#define LIMITE_HISTORICO 500000

typedef struct {
    int id;
    char motivo[31];
    char envio[32];
    char retorno[32];
    int enviada;
    int recebida;
    int autor_envio;
    int autor_retorno;
} LoteLavanderia;

typedef struct {
    int id_lote;
    int id_item;
    int retornou;
    char resultado[51];
    int encerrado;
} ItemLote;

static const char *tipos_entrada[] = {
    "lencol_baixo", "lencol_cima", "fronha", "toalha_banho", "toalha_rosto",
    "toalha_piso", "roupao", "capa_duvet"
};

/* Localiza uma peca pelo ID; devolve o indice ou -1 quando ela nao existe. */
static int indice_item(const ItemEnxoval itens[], int quantidade, int id) {
    for (int i = 0; i < quantidade; i++) {
        if (itens[i].id_item == id) {
            return i;
        }
    }
    return -1;
}

/* Lista nome/cargo e valida o operador escolhido; zero ou EOF cancela sem gravar. */
static int solicitar_operador(int *id) {
    FILE *arquivo = fopen(ARQ_COLABORADORES, "r");
    char linha[512], nome[51] = "", cargo[31] = "";
    if (!arquivo) { log_puts("Nao foi possivel carregar os colaboradores."); return 0; }
    log_puts("Colaboradores cadastrados:");
    if (fgets(linha, sizeof linha, arquivo)) {
        while (fgets(linha, sizeof linha, arquivo)) {
            char *campos[5];
            if (separar_csv(linha, campos, 5) != 5) { fclose(arquivo); return 0; }
            log_printf("ID %s - %s [%s]\n", campos[0], campos[1], campos[3]);
        }
    }
    int leitura_ok = !ferror(arquivo);
    fclose(arquivo);
    if (!leitura_ok) return 0;
    for (;;) {
        if (!ler_inteiro_prompt("ID do colaborador responsavel (0 cancela): ", 0, INT_MAX, id) || *id == 0) {
            return 0;
        }
        arquivo = fopen(ARQ_COLABORADORES, "r");
        if (!arquivo) return 0;
        int encontrado = 0;
        if (fgets(linha, sizeof linha, arquivo)) {
            while (fgets(linha, sizeof linha, arquivo)) {
                char *campos[5];
                if (separar_csv(linha, campos, 5) != 5) { fclose(arquivo); return 0; }
                if (atoi(campos[0]) == *id) {
                    snprintf(nome, sizeof nome, "%s", campos[1]);
                    snprintf(cargo, sizeof cargo, "%s", campos[3]);
                    encontrado = 1;
                }
            }
        }
        leitura_ok = !ferror(arquivo);
        fclose(arquivo);
        if (!leitura_ok) return 0;
        if (encontrado) { log_printf("Responsavel: ID %d - %s [%s].\n", *id, nome, cargo); return 1; }
        log_puts("Colaborador nao cadastrado.");
    }
}

/* Confirma uma operacao completa; qualquer cancelamento evita a gravacao dos CSVs. */
static int confirmar_operacao(void) {
    int resposta;
    return ler_inteiro_prompt("Confirmar operacao? 1 - Sim / 0 - Cancelar: ", 0, 1, &resposta) && resposta == 1;
}

/* Carrega o estoque em memoria dinamica; libera e devolve NULL em caso de falha. */
static ItemEnxoval *abrir_estoque(int *quantidade) {
    ItemEnxoval *itens = calloc(MAX_ITENS_ENXOVAL, sizeof *itens);
    if (!itens) {
        log_puts("Memoria insuficiente para carregar o estoque.");
        return NULL;
    }
    *quantidade = carregar_itens_estoque(itens, MAX_ITENS_ENXOVAL);
    if (*quantidade < 0) {
        log_puts("Estoque nao foi carregado. Confira o arquivo CSV.");
        free(itens);
        return NULL;
    }
    return itens;
}

/* Copia um CSV para a versao temporaria e encontra o maior ID da primeira coluna. */
static FILE *copiar_movimentos(int *proximo_id) {
    char linha[1024];
    FILE *entrada = fopen(ARQ_MOVIMENTOS, "r");
    FILE *saida;

    if (!entrada && errno != ENOENT) {
        return NULL;
    }
    saida = fopen(TMP_MOVIMENTOS, "w");
    if (!saida) {
        if (entrada) fclose(entrada);
        return NULL;
    }
    *proximo_id = 1;
    if (!entrada) {
        log_fprintf(saida, "id_movimentacao,id_item,tipo_movimentacao,data_hora,id_colaborador,fornecedor,custo_unitario\n");
    } else {
        while (fgets(linha, sizeof linha, entrada)) {
            int id = atoi(linha);
            if (id == INT_MAX) {
                fclose(entrada);
                fclose(saida);
                return NULL;
            }
            if (id >= *proximo_id) *proximo_id = id + 1;
            fputs(linha, saida);
        }
        if (ferror(entrada)) {
            fclose(entrada);
            fclose(saida);
            return NULL;
        }
        fclose(entrada);
    }
    if (ferror(saida)) {
        fclose(saida);
        return NULL;
    }
    return saida;
}

/* Persiste estoque e movimentos preparados como uma unica operacao com rollback. */
static int concluir_movimento(ItemEnxoval itens[], int quantidade, FILE *movimentos) {
    const char *temporarios[] = {TMP_ITENS, TMP_MOVIMENTOS};
    const char *destinos[] = {ARQ_ITENS, ARQ_MOVIMENTOS};
    int erro = ferror(movimentos);

    if (fclose(movimentos) != 0) erro = 1;
    if (erro || !salvar_itens_estoque(itens, quantidade, TMP_ITENS)) {
        return 0;
    }
    return confirmar_arquivos(temporarios, destinos, 2);
}

/* Identifica IDs ligados a entregas ainda nao finalizadas; devolve 0 se a leitura falhar. */
static int marcar_reservados(const ItemEnxoval itens[], int quantidade, unsigned char reservados[]) {
    FILE *entregas = fopen("dados/entrega_kit.csv", "r");
    FILE *vinculos = NULL;
    int *pendentes = NULL;
    int total = 0;
    char linha[512];

    if (!entregas) return 0;
    pendentes = calloc(LIMITE_HISTORICO, sizeof *pendentes);
    if (!pendentes) {
        fclose(entregas);
        return 0;
    }
    if (fgets(linha, sizeof linha, entregas)) {
        while (fgets(linha, sizeof linha, entregas)) {
            char *campos[6];
            int n = separar_csv(linha, campos, 6);
            if (n < 5 || total == LIMITE_HISTORICO) {
                fclose(entregas);
                free(pendentes);
                return 0;
            }
            if (!campos[3][0]) pendentes[total++] = atoi(campos[0]);
        }
    }
    if (ferror(entregas)) {
        fclose(entregas);
        free(pendentes);
        return 0;
    }
    fclose(entregas);
    vinculos = fopen("dados/entrega_kit_item.csv", "r");
    if (!vinculos) {
        free(pendentes);
        return 0;
    }
    if (fgets(linha, sizeof linha, vinculos)) {
        while (fgets(linha, sizeof linha, vinculos)) {
            char *campos[3];
            if (separar_csv(linha, campos, 3) < 2) {
                fclose(vinculos);
                free(pendentes);
                return 0;
            }
            for (int i = 0; i < total; i++) {
                if (pendentes[i] == atoi(campos[0])) {
                    int indice = indice_item(itens, quantidade, atoi(campos[1]));
                    if (indice >= 0) reservados[indice] = 1;
                    break;
                }
            }
        }
    }
    int ok = !ferror(vinculos);
    fclose(vinculos);
    free(pendentes);
    return ok;
}

/* Mostra os campos da peca, inclusive os ciclos atual/maximo e sua disponibilidade. */
static void imprimir_peca(const ItemEnxoval *item) {
    log_printf("%6d  %-15s %-21s %-8s %-22s %d/%d\n", item->id_item, item->tag_rfid,
               item->tipo, item->tamanho_cama[0] ? item->tamanho_cama : "-", item->situacao,
               item->ciclos_lavagem, item->limite_ciclos);
}

/* Agrupa tipos equivalentes para consulta, mantendo os dados originais de cada peca. */
static void imprimir_quantidades(const ItemEnxoval itens[], int quantidade) {
    typedef struct {
        const char *tipo;
        const char *tamanho;
        int total;
        int disponiveis;
    } GrupoConsulta;
    GrupoConsulta *grupos = calloc((size_t)(quantidade > 0 ? quantidade : 1), sizeof *grupos);
    int ng = 0;

    if (!grupos) {
        log_puts("Nao foi possivel gerar o resumo por tipo; a consulta individual continua disponivel.");
        return;
    }
    for (int i = 0; i < quantidade; i++) {
        const char *tipo = tipo_item_normalizado(itens[i].tipo);
        const char *tamanho = tamanho_item_normalizado(&itens[i]);
        int grupo = 0;
        while (grupo < ng && (strcmp(grupos[grupo].tipo, tipo) != 0 ||
               strcmp(grupos[grupo].tamanho, tamanho) != 0)) grupo++;
        if (grupo == ng) {
            grupos[ng].tipo = tipo;
            grupos[ng].tamanho = tamanho;
            ng++;
        }
        grupos[grupo].total++;
        if (strcmp(itens[i].situacao, "em_estoque") == 0) grupos[grupo].disponiveis++;
    }
    log_puts("\nTipo                  Tamanho     Cadastradas  Disponiveis");
    for (int i = 0; i < ng; i++) {
        log_printf("%-21s %-10s %11d %12d\n", grupos[i].tipo,
                   grupos[i].tamanho[0] ? grupos[i].tamanho : "-", grupos[i].total, grupos[i].disponiveis);
    }
    free(grupos);
}

/* Lista estoque por paginas e consulta por ID; nao modifica os dados recebidos. */
void consultar_estoque(void) {
    int quantidade, opcao;
    ItemEnxoval *itens = abrir_estoque(&quantidade);
    if (!itens) return;

    log_printf("\nQuantidade total de pecas cadastradas: %d\n", quantidade);
    static const char *situacoes[] = {
        "em_estoque", "em_uso", "em_lavanderia_comum", "em_remocao_manchas",
        "em_relavagem", "pendente_avaliacao", "baixado"
    };
    for (int s = 0; s < 7; s++) {
        int total = 0;
        for (int i = 0; i < quantidade; i++) {
            if (strcmp(itens[i].situacao, situacoes[s]) == 0) total++;
        }
        log_printf("  %-24s %d\n", situacoes[s], total);
    }
    imprimir_quantidades(itens, quantidade);
    while (ler_inteiro_prompt("1 - Listar pecas / 2 - Consultar ID / 0 - Voltar: ", 0, 2, &opcao) && opcao) {
        if (opcao == 2) {
            int id;
            if (!ler_inteiro_prompt("ID da peca (0 cancela): ", 0, INT_MAX, &id)) break;
            int indice = indice_item(itens, quantidade, id);
            if (indice < 0) log_puts("Peca nao encontrada.");
            else imprimir_peca(&itens[indice]);
        } else {
            for (int i = 0; i < quantidade; i++) {
                if (i % 25 == 0) log_puts("    ID  RFID            Tipo                  Tamanho  Situacao               Ciclos");
                imprimir_peca(&itens[i]);
                if ((i + 1) % 25 == 0 && i + 1 < quantidade) {
                    int seguir;
                    if (!ler_inteiro_prompt("1 - Proxima pagina / 0 - Voltar: ", 0, 1, &seguir) || !seguir) break;
                }
            }
        }
    }
    free(itens);
}

/* Recebe dados comuns de compra e RFIDs individuais, reservando IDs ainda nao utilizados. */
void entrada_estoque(void) {
    int quantidade, tipo, tamanho = 0, novas, limite, autor, maior_id = 0;
    double custo;
    char fornecedor[81], instante[32], data[11];
    const char *tamanhos[] = {"", "king", "viúvo"};
    ItemEnxoval *itens = abrir_estoque(&quantidade);
    if (!itens) return;

    for (int i = 0; i < 8; i++) log_printf("%d - %s\n", i + 1, tipos_entrada[i]);
    if (!ler_inteiro_prompt("Tipo (0 cancela): ", 0, 8, &tipo) || !tipo) goto fim;
    if (tipo == 1 || tipo == 2 || tipo == 8) {
        if (!ler_inteiro_prompt("Tamanho: 1 - King / 2 - Viuvo / 0 - Cancelar: ", 0, 2, &tamanho) ||
            !tamanho) goto fim;
    }
    if (quantidade >= MAX_ITENS_ENXOVAL) {
        log_puts("Limite de pecas cadastradas atingido.");
        goto fim;
    }
    if (!ler_inteiro_prompt("Quantidade de novas pecas (0 cancela): ", 0, MAX_ITENS_ENXOVAL - quantidade, &novas) ||
        novas == 0 || !ler_inteiro_prompt("Limite de ciclos por peca: ", 1, INT_MAX, &limite) ||
        !ler_texto_prompt("Fornecedor (0 cancela): ", fornecedor, sizeof fornecedor) || strcmp(fornecedor, "0") == 0 ||
        !ler_decimal_prompt("Custo unitario (R$): ", 0, 99999999.99, &custo) || !solicitar_operador(&autor)) goto fim;

    for (int i = 0; i < quantidade; i++) {
        if (itens[i].id_item > maior_id) maior_id = itens[i].id_item;
    }
    if (maior_id > INT_MAX - novas) {
        log_puts("Nao ha IDs disponiveis para a entrada.");
        goto fim;
    }
    obter_data_atual(data, sizeof data);
    obter_data_hora(instante, sizeof instante);
    for (int i = 0; i < novas; i++) {
        ItemEnxoval *nova = &itens[quantidade + i];
        for (;;) {
            int duplicado = 0;
            log_printf("Peca %d/%d - ", i + 1, novas);
            if (!ler_texto_prompt("RFID (0 cancela toda a entrada): ", nova->tag_rfid, sizeof nova->tag_rfid) ||
                strcmp(nova->tag_rfid, "0") == 0) goto fim;
            for (int j = 0; j < quantidade + i; j++) {
                if (strcmp(itens[j].tag_rfid, nova->tag_rfid) == 0) duplicado = 1;
            }
            if (!duplicado) break;
            log_puts("RFID ja cadastrado. Informe uma etiqueta unica.");
        }
        nova->id_item = ++maior_id;
        snprintf(nova->tipo, sizeof nova->tipo, "%s", tipos_entrada[tipo - 1]);
        snprintf(nova->tamanho_cama, sizeof nova->tamanho_cama, "%s", tamanhos[tamanho]);
        snprintf(nova->situacao, sizeof nova->situacao, "em_estoque");
        snprintf(nova->data_aquisicao, sizeof nova->data_aquisicao, "%s", data);
        nova->limite_ciclos = limite;
    }
    log_printf("Entrada de %d pecas. Valor total estimado: R$ %.2f.\n", novas, novas * custo);
    if (confirmar_operacao()) {
        int proximo;
        FILE *movimentos = copiar_movimentos(&proximo);
        if (!movimentos) {
            log_puts("Erro ao preparar as movimentacoes. Nenhum dado foi alterado.");
            goto fim;
        }
        if (proximo > INT_MAX - novas) {
            fclose(movimentos);
            log_puts("Limite de identificadores de movimentacao atingido.");
            goto fim;
        }
        for (int i = 0; i < novas; i++) {
            log_fprintf(movimentos, "%d,%d,entrada_compra,%s,%d,%s,%.2f\n", proximo++,
                        itens[quantidade + i].id_item, instante, autor, fornecedor, custo);
        }
        if (concluir_movimento(itens, quantidade + novas, movimentos)) log_puts("Entrada registrada.");
        else log_puts("Erro ao salvar a entrada. Confira as mensagens de persistencia.");
    }
fim:
    free(itens);
}

/* Registra a baixa e seu movimento de forma conjunta; permite uso pela finalizacao do consumo. */
int baixar_item_com_movimento(int id_item, const char *motivo,
                             const char *responsabilidade, int id_colaborador) {
    int quantidade, proximo, ok = 0;
    char instante[32];
    const char *responsaveis[] = {"hospede", "lavanderia", "equipe", "desgaste_natural"};
    int responsavel_valido = 0;

    if (!motivo || !responsabilidade ||
        (strcmp(motivo, "perda") != 0 && strcmp(motivo, "avaria") != 0) || !colaborador_existe(id_colaborador)) return 0;
    for (int i = 0; i < 4; i++) {
        if (strcmp(responsabilidade, responsaveis[i]) == 0) responsavel_valido = 1;
    }
    if (!responsavel_valido) return 0;
    ItemEnxoval *itens = abrir_estoque(&quantidade);
    if (!itens) return 0;
    int indice = indice_item(itens, quantidade, id_item);
    if (indice < 0 || strcmp(itens[indice].situacao, "baixado") == 0) goto fim;
    if (strcmp(itens[indice].situacao, "pendente_avaliacao") == 0) {
        free(itens);
        return resolver_avaliacao_item(id_item, 1, motivo, responsabilidade, id_colaborador);
    }
    snprintf(itens[indice].situacao, sizeof itens[indice].situacao, "baixado");
    snprintf(itens[indice].motivo_baixa, sizeof itens[indice].motivo_baixa, "%s", motivo);
    snprintf(itens[indice].responsavel_baixa, sizeof itens[indice].responsavel_baixa, "%s", responsabilidade);
    FILE *movimentos = copiar_movimentos(&proximo);
    if (!movimentos) goto fim;
    obter_data_hora(instante, sizeof instante);
    log_fprintf(movimentos, "%d,%d,baixa,%s,%d,,\n", proximo, id_item, instante, id_colaborador);
    ok = concluir_movimento(itens, quantidade, movimentos);
fim:
    free(itens);
    return ok;
}

/* Carrega lotes sem descartar linhas invalidas; devolve -1 em erro de formato, memoria ou leitura. */
static int carregar_lotes(LoteLavanderia **lotes) {
    char linha[512];
    int quantidade = 0, capacidade = 32;
    FILE *arquivo = fopen(ARQ_LOTES, "r");

    *lotes = NULL;
    if (!arquivo) return errno == ENOENT ? 0 : -1;
    *lotes = calloc((size_t)capacidade, sizeof **lotes);
    if (!*lotes) {
        fclose(arquivo);
        return -1;
    }
    if (fgets(linha, sizeof linha, arquivo)) {
        while (fgets(linha, sizeof linha, arquivo)) {
            char *c[9];
            if (separar_csv(linha, c, 9) != 8 || quantidade == LIMITE_HISTORICO) goto erro;
            if (quantidade == capacidade) {
                int nova_capacidade = capacidade * 2;
                LoteLavanderia *novo = realloc(*lotes, (size_t)nova_capacidade * sizeof **lotes);
                if (!novo) goto erro;
                *lotes = novo;
                capacidade = nova_capacidade;
            }
            LoteLavanderia *lote = &(*lotes)[quantidade++];
            memset(lote, 0, sizeof *lote);
            lote->id = atoi(c[0]);
            snprintf(lote->motivo, sizeof lote->motivo, "%s", c[1]);
            snprintf(lote->envio, sizeof lote->envio, "%s", c[2]);
            snprintf(lote->retorno, sizeof lote->retorno, "%s", c[3]);
            lote->enviada = atoi(c[4]);
            lote->recebida = atoi(c[5]);
            lote->autor_envio = atoi(c[6]);
            lote->autor_retorno = atoi(c[7]);
            if (lote->id <= 0 || lote->enviada <= 0 || lote->recebida < 0 || lote->recebida > lote->enviada) goto erro;
        }
    }
    if (ferror(arquivo)) goto erro;
    fclose(arquivo);
    return quantidade;
erro:
    fclose(arquivo);
    free(*lotes);
    *lotes = NULL;
    return -1;
}

/* Carrega os vinculos item/lote e inspecoes; devolve -1 caso algum registro seja invalido. */
static int carregar_vinculos(ItemLote **vinculos) {
    char linha[512];
    int quantidade = 0, capacidade = 64;
    FILE *arquivo = fopen(ARQ_ITEM_LOTE, "r");

    *vinculos = NULL;
    if (!arquivo) return errno == ENOENT ? 0 : -1;
    *vinculos = calloc((size_t)capacidade, sizeof **vinculos);
    if (!*vinculos) {
        fclose(arquivo);
        return -1;
    }
    if (fgets(linha, sizeof linha, arquivo)) {
        while (fgets(linha, sizeof linha, arquivo)) {
            char *c[5];
            int campos = separar_csv(linha, c, 5);
            if ((campos != 4 && campos != 5) || quantidade == LIMITE_HISTORICO) goto erro;
            if (quantidade == capacidade) {
                int nova_capacidade = capacidade * 2;
                ItemLote *novo = realloc(*vinculos, (size_t)nova_capacidade * sizeof **vinculos);
                if (!novo) goto erro;
                *vinculos = novo;
                capacidade = nova_capacidade;
            }
            ItemLote *vinculo = &(*vinculos)[quantidade++];
            vinculo->id_lote = atoi(c[0]);
            vinculo->id_item = atoi(c[1]);
            vinculo->retornou = atoi(c[2]);
            snprintf(vinculo->resultado, sizeof vinculo->resultado, "%s", c[3]);
            vinculo->encerrado = campos == 5 ? atoi(c[4]) :
                (vinculo->retornou && strcmp(vinculo->resultado, "conforme") == 0);
            if (vinculo->id_lote <= 0 || vinculo->id_item <= 0 ||
                (vinculo->retornou != 0 && vinculo->retornou != 1) ||
                (vinculo->encerrado != 0 && vinculo->encerrado != 1)) goto erro;
        }
    }
    if (ferror(arquivo)) goto erro;
    fclose(arquivo);
    return quantidade;
erro:
    fclose(arquivo);
    free(*vinculos);
    *vinculos = NULL;
    return -1;
}

/* Informa se uma peca aguarda retorno de algum lote, impedindo o envio duplicado. */
static int item_em_lote_aberto(const ItemLote vinculos[], int quantidade, int id_item) {
    for (int i = 0; i < quantidade; i++) {
        if (vinculos[i].id_item == id_item && !vinculos[i].retornou && !vinculos[i].encerrado) return 1;
    }
    return 0;
}

/* Solicita motivo e responsabilidade da baixa manual, preservando pecas reservadas ou em transito. */
void saida_estoque(void) {
    int quantidade, id, motivo, responsavel, autor;
    const char *motivos[] = {"perda", "avaria"};
    const char *responsaveis[] = {"hospede", "lavanderia", "equipe", "desgaste_natural"};
    ItemEnxoval *itens = abrir_estoque(&quantidade);
    ItemLote *vinculos = NULL;
    unsigned char *reservados = calloc(MAX_ITENS_ENXOVAL, 1);
    int nv = carregar_vinculos(&vinculos);

    if (!itens || !reservados || nv < 0) goto fim;
    if (!marcar_reservados(itens, quantidade, reservados)) {
        log_puts("Nao foi possivel verificar as reservas. Saida cancelada.");
        goto fim;
    }
    if (!ler_inteiro_prompt("ID da peca para baixa (0 cancela): ", 0, INT_MAX, &id) || id == 0) goto fim;
    int indice = indice_item(itens, quantidade, id);
    if (indice < 0) {
        log_puts("Peca nao encontrada.");
        goto fim;
    }
    imprimir_peca(&itens[indice]);
    if (reservados[indice]) {
        log_puts("Peca reservada em entrega pendente. Registre falta no pos-atendimento, se necessario.");
        goto fim;
    }
    if (item_em_lote_aberto(vinculos, nv, id) && strcmp(itens[indice].situacao, "pendente_avaliacao") != 0) {
        log_puts("Peca em transito. Registre a divergencia no retorno para encaminhar a avaliacao.");
        goto fim;
    }
    if (strcmp(itens[indice].situacao, "baixado") == 0) {
        log_puts("A peca ja esta baixada.");
        goto fim;
    }
    if (!ler_inteiro_prompt("Motivo: 1 - Perda / 2 - Avaria / 0 - Cancelar: ", 0, 2, &motivo) || !motivo ||
        !ler_inteiro_prompt("Responsabilidade: 1-hospede 2-lavanderia 3-equipe 4-desgaste natural 0-cancelar: ",
                            0, 4, &responsavel) || !responsavel || !solicitar_operador(&autor)) goto fim;
    if (confirmar_operacao()) {
        if (baixar_item_com_movimento(id, motivos[motivo - 1], responsaveis[responsavel - 1], autor)) {
            log_puts("Baixa registrada no estoque e nas movimentacoes.");
        } else log_puts("Erro ao registrar a baixa.");
    }
fim:
    free(itens);
    free(reservados);
    free(vinculos);
}

/* Reserva um kit para uma UH/camareira da distribuicao de hoje, validando a tipologia escolhida. */
void criar_kit_estoque(Camareira camareiras[], int quantidade) {
    int tipo, uh, camareira;
    char data[11];
    (void)camareiras;
    (void)quantidade;

    if (!ler_inteiro_prompt("Kit: 1 - King / 2 - Dupla viuvo / 0 - Cancelar: ", 0, 2, &tipo) || !tipo ||
        !ler_inteiro_prompt("ID da UH (0 cancela): ", 0, INT_MAX, &uh) || !uh ||
        !ler_inteiro_prompt("ID da camareira na distribuicao de hoje (0 cancela): ", 0, INT_MAX, &camareira) ||
        !camareira) return;
    obter_data_atual(data, sizeof data);
    if (!tipo_atendimento_distribuido(camareira, uh, data)) {
        log_puts("UH e camareira nao correspondem a distribuicao de hoje. Kit cancelado.");
        return;
    }
    if (!confirmar_operacao()) return;
    if (montar_kit_uh(uh, camareira, data, tipo)) {
        log_puts("Kit reservado e vinculado a UH/camareira da distribuicao de hoje.");
    } else log_puts("Kit nao foi reservado. Confira tipologia, disponibilidade e entregas existentes.");
}

/* Prepara estoque, lotes e inspecoes; encerrado distingue resolucao de retorno fisico. */
static int preparar_lavanderia(const ItemEnxoval itens[], int ni, const LoteLavanderia lotes[], int nl,
                              const ItemLote vinculos[], int nv) {
    FILE *saida_lotes = fopen(TMP_LOTES, "w");
    FILE *saida_vinculos = fopen(TMP_ITEM_LOTE, "w");

    if (!saida_lotes || !saida_vinculos) {
        if (saida_lotes) fclose(saida_lotes);
        if (saida_vinculos) fclose(saida_vinculos);
        return 0;
    }
    log_fprintf(saida_lotes, "id_lote,motivo_envio,data_envio,data_retorno,qtd_enviada,qtd_recebida,");
    log_fprintf(saida_lotes, "id_colaborador_envio,id_colaborador_retorno\n");
    for (int i = 0; i < nl; i++) {
        const LoteLavanderia *lote = &lotes[i];
        log_fprintf(saida_lotes, "%d,%s,%s,%s,%d,%d,%d,", lote->id, lote->motivo, lote->envio,
                    lote->retorno, lote->enviada, lote->recebida, lote->autor_envio);
        if (lote->autor_retorno) log_fprintf(saida_lotes, "%d", lote->autor_retorno);
        log_fprintf(saida_lotes, "\n");
    }
    log_fprintf(saida_vinculos, "id_lote,id_item,retornou,resultado_inspecao,encerrado\n");
    for (int i = 0; i < nv; i++) {
        log_fprintf(saida_vinculos, "%d,%d,%d,%s,%d\n", vinculos[i].id_lote, vinculos[i].id_item,
                    vinculos[i].retornou, vinculos[i].resultado, vinculos[i].encerrado);
    }
    int erro = ferror(saida_lotes) || ferror(saida_vinculos);
    if (fclose(saida_lotes) != 0) erro = 1;
    if (fclose(saida_vinculos) != 0) erro = 1;
    if (erro || !salvar_itens_estoque(itens, ni, TMP_ITENS)) return 0;
    return 1;
}

/* Confirma os tres CSVs preparados para envio ou retorno de lavanderia. */
static int salvar_lavanderia(const ItemEnxoval itens[], int ni, const LoteLavanderia lotes[], int nl,
                            const ItemLote vinculos[], int nv) {
    const char *temporarios[] = {TMP_ITENS, TMP_LOTES, TMP_ITEM_LOTE};
    const char *destinos[] = {ARQ_ITENS, ARQ_LOTES, ARQ_ITEM_LOTE};
    if (!preparar_lavanderia(itens, ni, lotes, nl, vinculos, nv)) return 0;
    return confirmar_arquivos(temporarios, destinos, 3);
}

/* Conta os vinculos ainda sem destino final, sem reabrir registros historicos encerrados. */
static int pendencias_lote(const ItemLote vinculos[], int nv, int id_lote) {
    int total = 0;
    for (int i = 0; i < nv; ++i) {
        if (vinculos[i].id_lote == id_lote && !vinculos[i].encerrado) ++total;
    }
    return total;
}

/* Encerra vinculos com destino final e mantem a data de retorno fisico separada da resolucao. */
static void encerrar_destinos_finais(const ItemEnxoval itens[], int ni, ItemLote vinculos[], int nv) {
    for (int v = 0; v < nv; ++v) {
        if (vinculos[v].encerrado) continue;
        int i = indice_item(itens, ni, vinculos[v].id_item);
        if (i >= 0 && (strcmp(itens[i].situacao, "em_estoque") == 0 || strcmp(itens[i].situacao, "baixado") == 0)) {
            vinculos[v].encerrado = 1;
        }
    }
}

/* Resolve uma pendencia com autoria, movimento e conciliacao de lotes no mesmo commit. */
int resolver_avaliacao_item(int id_item, int baixar, const char *motivo,
                            const char *responsabilidade, int id_colaborador) {
    int ni = 0, nl, nv, ok = 0, proximo;
    ItemEnxoval *itens = abrir_estoque(&ni);
    LoteLavanderia *lotes = NULL;
    ItemLote *vinculos = NULL;
    FILE *movimentos = NULL;
    nl = carregar_lotes(&lotes);
    nv = carregar_vinculos(&vinculos);
    if (!itens || nl < 0 || nv < 0 || !colaborador_existe(id_colaborador)) goto fim;
    int i = indice_item(itens, ni, id_item);
    if (i < 0 || strcmp(itens[i].situacao, "pendente_avaliacao") != 0) goto fim;
    if (baixar && (!motivo || !responsabilidade ||
        (strcmp(motivo, "perda") != 0 && strcmp(motivo, "avaria") != 0) ||
        (strcmp(responsabilidade, "hospede") != 0 && strcmp(responsabilidade, "lavanderia") != 0 &&
         strcmp(responsabilidade, "equipe") != 0 && strcmp(responsabilidade, "desgaste_natural") != 0))) goto fim;
    unsigned char *reservados = calloc(MAX_ITENS_ENXOVAL, 1);
    if (!reservados) goto fim;
    int reservas_ok = marcar_reservados(itens, ni, reservados) && !reservados[i];
    free(reservados);
    if (!reservas_ok) goto fim;
    snprintf(itens[i].situacao, sizeof itens[i].situacao, "%s", baixar ? "baixado" : "em_estoque");
    snprintf(itens[i].motivo_baixa, sizeof itens[i].motivo_baixa, "%s", baixar ? motivo : "");
    snprintf(itens[i].responsavel_baixa, sizeof itens[i].responsavel_baixa, "%s", baixar ? responsabilidade : "");
    for (int v = 0; v < nv; ++v) {
        if (vinculos[v].id_item != id_item || vinculos[v].encerrado) continue;
        if (!baixar && !vinculos[v].retornou) {
            for (int l = 0; l < nl; ++l) {
                if (lotes[l].id == vinculos[v].id_lote) {
                    ++lotes[l].recebida;
                    lotes[l].autor_retorno = id_colaborador;
                    if (lotes[l].recebida == lotes[l].enviada) obter_data_hora(lotes[l].retorno, sizeof lotes[l].retorno);
                }
            }
            vinculos[v].retornou = 1;
            snprintf(vinculos[v].resultado, sizeof vinculos[v].resultado, "conforme");
        }
        vinculos[v].encerrado = 1;
    }
    movimentos = copiar_movimentos(&proximo);
    if (!movimentos) goto fim;
    char agora[32];
    obter_data_hora(agora, sizeof agora);
    log_fprintf(movimentos, "%d,%d,%s,%s,%d,,\n", proximo, id_item,
                baixar ? "baixa" : "contagem", agora, id_colaborador);
    ok = !ferror(movimentos);
    if (fclose(movimentos) != 0) ok = 0;
    movimentos = NULL;
    if (ok) ok = preparar_lavanderia(itens, ni, lotes, nl, vinculos, nv);
    const char *temporarios[] = {TMP_ITENS, TMP_LOTES, TMP_ITEM_LOTE, TMP_MOVIMENTOS};
    const char *destinos[] = {ARQ_ITENS, ARQ_LOTES, ARQ_ITEM_LOTE, ARQ_MOVIMENTOS};
    if (ok) ok = confirmar_arquivos(temporarios, destinos, 4);
fim:
    if (movimentos) fclose(movimentos);
    free(itens);
    free(lotes);
    free(vinculos);
    return ok;
}

/* Confere o estado exigido pelo motivo e impede misturar pecas reservadas ou em transito. */
static int elegivel_envio(const ItemEnxoval *item, int reservado, const ItemLote vinculos[], int nv, int motivo) {
    static const char *estados_origem[] = {"em_uso", "em_remocao_manchas", "em_relavagem"};
    return motivo >= 1 && motivo <= 3 && !reservado &&
           !item_em_lote_aberto(vinculos, nv, item->id_item) &&
           strcmp(item->situacao, estados_origem[motivo - 1]) == 0;
}

/* Cria lote com pecas do motivo escolhido, sem incluir reservas nem pecas em transito. */
void enviar_lavanderia(void) {
    int ni = 0, motivo, autor, nl, nv, selecionadas = 0, id_lote = 1;
    const char *motivos[] = {"lavagem_comum", "remocao_manchas", "relavagem"};
    const char *estados[] = {"em_lavanderia_comum", "em_remocao_manchas", "em_relavagem"};
    ItemEnxoval *itens = abrir_estoque(&ni);
    LoteLavanderia *lotes = NULL;
    ItemLote *vinculos = NULL;
    unsigned char *reservados = calloc(MAX_ITENS_ENXOVAL, 1);
    unsigned char *escolhidos = calloc(MAX_ITENS_ENXOVAL, 1);
    nl = carregar_lotes(&lotes);
    nv = carregar_vinculos(&vinculos);

    if (!itens || !reservados || !escolhidos || nl < 0 || nv < 0) {
        log_puts("Erro ao carregar estoque ou historico de lavanderia.");
        goto fim;
    }
    if (!marcar_reservados(itens, ni, reservados)) {
        log_puts("Nao foi possivel verificar reservas. Envio cancelado.");
        goto fim;
    }
    if (!ler_inteiro_prompt("Motivo: 1-lavagem comum 2-remocao de manchas 3-relavagem 0-cancelar: ",
                            0, 3, &motivo) || !motivo || !solicitar_operador(&autor)) goto fim;
    log_puts("Pecas elegiveis (ID, RFID, tipo, tamanho, situacao, ciclos):");
    int elegiveis = 0;
    for (int i = 0; i < ni; i++) {
        if (elegivel_envio(&itens[i], reservados[i], vinculos, nv, motivo)) {
            imprimir_peca(&itens[i]);
            elegiveis++;
        }
    }
    if (!elegiveis) {
        log_puts("Nenhuma peca elegivel para envio. Entregas pendentes permanecem reservadas.");
        goto fim;
    }
    log_puts("Informe IDs para selecionar; repita um ID para remover. 0 conclui; -1 cancela tudo; -2 seleciona todas.");
    for (;;) {
        int id;
        if (!ler_inteiro_prompt("ID da peca: ", -2, INT_MAX, &id) || id == -1) goto fim;
        if (id == 0) break;
        if (id == -2) {
            for (int i = 0; i < ni; i++) {
                if (elegivel_envio(&itens[i], reservados[i], vinculos, nv, motivo) && !escolhidos[i]) {
                    escolhidos[i] = 1;
                    selecionadas++;
                }
            }
            log_printf("%d pecas selecionadas.\n", selecionadas);
            continue;
        }
        int indice = indice_item(itens, ni, id);
        if (indice < 0 || !elegivel_envio(&itens[indice], reservados[indice], vinculos, nv, motivo)) {
            log_puts("Peca inexistente, ja enviada, reservada ou incompativel com o motivo escolhido.");
            continue;
        }
        escolhidos[indice] = !escolhidos[indice];
        selecionadas += escolhidos[indice] ? 1 : -1;
        log_printf("Peca %d %s. Total: %d.\n", id, escolhidos[indice] ? "selecionada" : "removida", selecionadas);
    }
    if (!selecionadas || !confirmar_operacao()) goto fim;
    if (nl >= LIMITE_HISTORICO || nv > LIMITE_HISTORICO - selecionadas) {
        log_puts("Limite de historico de lavanderia atingido.");
        goto fim;
    }
    for (int i = 0; i < nl; i++) {
        if (lotes[i].id == INT_MAX) goto fim;
        if (lotes[i].id >= id_lote) id_lote = lotes[i].id + 1;
    }
    LoteLavanderia *novos_lotes = realloc(lotes, (size_t)(nl + 1) * sizeof *lotes);
    if (!novos_lotes) goto fim;
    lotes = novos_lotes;
    ItemLote *novos_vinculos = realloc(vinculos, (size_t)(nv + selecionadas) * sizeof *vinculos);
    if (!novos_vinculos) goto fim;
    vinculos = novos_vinculos;
    /* Um novo lote assume o retrabalho; o vinculo anterior permanece como historico. */
    for (int v = 0; v < nv; ++v) {
        int i = indice_item(itens, ni, vinculos[v].id_item);
        if (i >= 0 && escolhidos[i] && vinculos[v].retornou) vinculos[v].encerrado = 1;
    }
    LoteLavanderia *lote = &lotes[nl++];
    memset(lote, 0, sizeof *lote);
    lote->id = id_lote;
    lote->enviada = selecionadas;
    lote->autor_envio = autor;
    obter_data_hora(lote->envio, sizeof lote->envio);
    snprintf(lote->motivo, sizeof lote->motivo, "%s", motivos[motivo - 1]);
    for (int i = 0; i < ni; i++) {
        if (!escolhidos[i]) continue;
        snprintf(itens[i].situacao, sizeof itens[i].situacao, "%s", estados[motivo - 1]);
        ItemLote *vinculo = &vinculos[nv++];
        memset(vinculo, 0, sizeof *vinculo);
        vinculo->id_item = itens[i].id_item;
        vinculo->id_lote = id_lote;
    }
    if (salvar_lavanderia(itens, ni, lotes, nl, vinculos, nv)) {
        log_printf("Lote %d enviado com %d pecas.\n", id_lote, selecionadas);
    } else log_puts("Erro ao salvar o envio. Confira as mensagens de persistencia.");
fim:
    free(itens);
    free(lotes);
    free(vinculos);
    free(reservados);
    free(escolhidos);
}

/* Lista cada peca ainda sem destino final, mesmo quando ja houve retorno fisico. */
static void listar_retorno(const ItemEnxoval itens[], int ni, const ItemLote vinculos[], int nv,
                          int id_lote, const unsigned char inspecoes[]) {
    static const char *resultados[] = {"conforme", "relavagem", "remocao_manchas", "avariado", "nao_retornou"};
    for (int i = 0; i < nv; i++) {
        if (vinculos[i].id_lote != id_lote || vinculos[i].encerrado) continue;
        int indice = indice_item(itens, ni, vinculos[i].id_item);
        if (indice < 0) continue;
        imprimir_peca(&itens[indice]);
        log_printf("        Retorno fisico: %s | Inspecao planejada: %s\n",
                   vinculos[i].retornou ? "recebido" : "nao conferido", resultados[inspecoes[indice]]);
    }
}

/* Confere retornos e acompanha retrabalhos/faltas ate estoque ou baixa confirmada. */
void receber_lavanderia(void) {
    int ni = 0, nl, nv, id_lote, indice_lote = -1, autor, abertas = 0;
    ItemEnxoval *itens = abrir_estoque(&ni);
    LoteLavanderia *lotes = NULL;
    ItemLote *vinculos = NULL;
    unsigned char *inspecoes = calloc(MAX_ITENS_ENXOVAL, 1);
    unsigned char *pendentes = calloc(MAX_ITENS_ENXOVAL, 1);
    nl = carregar_lotes(&lotes);
    nv = carregar_vinculos(&vinculos);

    if (!itens || !inspecoes || !pendentes || nl < 0 || nv < 0) {
        log_puts("Erro ao carregar os dados de lavanderia.");
        goto fim;
    }
    encerrar_destinos_finais(itens, ni, vinculos, nv);
    log_puts("Lotes com pecas aguardando retorno, retrabalho ou avaliacao:");
    for (int i = 0; i < nl; i++) {
        int abertas_lote = pendencias_lote(vinculos, nv, lotes[i].id);
        if (abertas_lote) {
            log_printf("Lote %d | %s | enviadas %d | recebidas %d | sem destino final %d\n", lotes[i].id,
                       lotes[i].motivo, lotes[i].enviada, lotes[i].recebida, abertas_lote);
            if (lotes[i].recebida < lotes[i].enviada) {
                log_printf("ALERTA: %d pecas nao retornaram fisicamente; confira os IDs pendentes.\n",
                           lotes[i].enviada - lotes[i].recebida);
            }
            abertas++;
        }
    }
    if (!abertas) {
        log_puts("Nenhum lote tem pecas pendentes de resolucao.");
        goto fim;
    }
    if (!ler_inteiro_prompt("ID do lote (0 cancela): ", 0, INT_MAX, &id_lote) || !id_lote) goto fim;
    for (int i = 0; i < nl; i++) {
        if (lotes[i].id == id_lote && pendencias_lote(vinculos, nv, id_lote)) indice_lote = i;
    }
    if (indice_lote < 0) {
        log_puts("Lote inexistente ou ja concluido.");
        goto fim;
    }
    int total_vinculos = 0, retornos_fisicos = 0;
    for (int i = 0; i < nv; i++) {
        if (vinculos[i].id_lote != id_lote) continue;
        ++total_vinculos;
        retornos_fisicos += vinculos[i].retornou;
        if (vinculos[i].encerrado) continue;
        int indice = indice_item(itens, ni, vinculos[i].id_item);
        if (indice < 0 || pendentes[indice]) {
            log_puts("Historico inconsistente: peca inexistente ou duplicada. Retorno cancelado.");
            goto fim;
        }
        if (strcmp(itens[indice].situacao, "em_lavanderia_comum") != 0 &&
            strcmp(itens[indice].situacao, "em_remocao_manchas") != 0 &&
            strcmp(itens[indice].situacao, "em_relavagem") != 0 &&
            strcmp(itens[indice].situacao, "pendente_avaliacao") != 0) {
            log_puts("Situacao da peca diverge do lote aberto. Retorno cancelado para preservar os dados.");
            goto fim;
        }
        pendentes[indice] = 1;
        if (!vinculos[i].retornou && strcmp(itens[indice].situacao, "pendente_avaliacao") == 0) inspecoes[indice] = 4;
        else if (vinculos[i].retornou) {
            if (strcmp(itens[indice].situacao, "em_relavagem") == 0) inspecoes[indice] = 1;
            else if (strcmp(itens[indice].situacao, "em_remocao_manchas") == 0) inspecoes[indice] = 2;
            else inspecoes[indice] = 3;
        }
    }
    if (total_vinculos != lotes[indice_lote].enviada || retornos_fisicos != lotes[indice_lote].recebida) {
        log_puts("Quantidade do lote diverge das pecas pendentes. Retorno cancelado.");
        goto fim;
    }
    log_puts("No primeiro retorno, pecas sem defeitos sao conformes. Marque os IDs que nao retornaram.");
    log_puts("Pendencias anteriores sao preservadas. Use Restaurar conforme para confirmar a regularizacao.");
    log_puts("Pecas avariadas ficam pendentes de avaliacao; a baixa exige motivo e responsavel.");
    for (;;) {
        int opcao, id;
        listar_retorno(itens, ni, vinculos, nv, id_lote, inspecoes);
        log_puts("1-Relavagem 2-Remocao de manchas 3-Avariado 4-Nao retornou 5-Restaurar conforme");
        if (!ler_inteiro_prompt("Inspecao: 0-Concluir / -1-Cancelar: ", -1, 5, &opcao) || opcao == -1) goto fim;
        if (!opcao) break;
        if (!ler_inteiro_prompt("ID da peca (0 volta): ", 0, INT_MAX, &id)) goto fim;
        if (!id) continue;
        int indice = indice_item(itens, ni, id);
        if (indice < 0 || !pendentes[indice]) {
            log_puts("A peca nao esta pendente neste lote.");
            continue;
        }
        int ja_recebida = 0;
        for (int v = 0; v < nv; ++v) {
            if (vinculos[v].id_lote == id_lote && vinculos[v].id_item == id) ja_recebida = vinculos[v].retornou;
        }
        if (opcao == 4 && ja_recebida) {
            log_puts("Peca ja recebida: seu retorno fisico nao pode ser desfeito.");
            continue;
        }
        inspecoes[indice] = opcao == 5 ? 0 : (unsigned char)opcao;
    }
    if (!solicitar_operador(&autor)) goto fim;
    int recebidas = 0, faltantes = 0;
    for (int i = 0; i < ni; i++) {
        if (!pendentes[i]) continue;
        if (inspecoes[i] == 4) faltantes++;
        else {
            for (int v = 0; v < nv; ++v) {
                if (vinculos[v].id_lote == id_lote && vinculos[v].id_item == itens[i].id_item &&
                    !vinculos[v].retornou) ++recebidas;
            }
        }
    }
    log_printf("Confirmar %d novos retornos; %d faltantes vao para avaliacao, sem baixa automatica.\n", recebidas, faltantes);
    if (!confirmar_operacao()) goto fim;
    static const char *estados[] = {"em_estoque", "em_relavagem", "em_remocao_manchas", "pendente_avaliacao"};
    static const char *resultados[] = {"conforme", "necessita_relavagem", "necessita_relavagem", "avariado"};
    for (int i = 0; i < nv; i++) {
        if (vinculos[i].id_lote != id_lote || vinculos[i].encerrado) continue;
        int indice = indice_item(itens, ni, vinculos[i].id_item);
        int resultado = inspecoes[indice];
        if (resultado == 4) {
            snprintf(itens[indice].situacao, sizeof itens[indice].situacao, "pendente_avaliacao");
            itens[indice].motivo_baixa[0] = '\0';
            itens[indice].responsavel_baixa[0] = '\0';
            continue;
        }
        if (!vinculos[i].retornou && itens[indice].ciclos_lavagem == INT_MAX) {
            log_puts("Limite de contador de ciclos atingido. Retorno cancelado.");
            goto fim;
        }
        if (!vinculos[i].retornou) itens[indice].ciclos_lavagem++;
        snprintf(itens[indice].situacao, sizeof itens[indice].situacao, "%s", estados[resultado]);
        vinculos[i].retornou = 1;
        snprintf(vinculos[i].resultado, sizeof vinculos[i].resultado, "%s", resultados[resultado]);
        vinculos[i].encerrado = resultado == 0;
    }
    LoteLavanderia *lote = &lotes[indice_lote];
    lote->recebida += recebidas;
    lote->autor_retorno = autor;
    if (lote->recebida == lote->enviada) obter_data_hora(lote->retorno, sizeof lote->retorno);
    if (salvar_lavanderia(itens, ni, lotes, nl, vinculos, nv)) {
        log_printf("Conferencia salva: %d novos retornos; %d pecas ainda sem destino final.\n",
                   recebidas, pendencias_lote(vinculos, nv, id_lote));
    } else log_puts("Erro ao salvar o retorno. Confira as mensagens de persistencia.");
fim:
    free(itens);
    free(lotes);
    free(vinculos);
    free(inspecoes);
    free(pendentes);
}
