/* Chamados de governanca: abertura pela distribuicao de hoje e transicoes de estado. */
#include "chamados.h"
#include "interno.h"
#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

#define ARQ_CHAMADOS "dados/chamado.csv"
#define TMP_CHAMADOS "dados/chamado.tmp"

typedef struct {
    int id;
    int id_uh;
    char origem[31];
    char status[21];
    char data_abertura[20];
    int id_camareira;
} Chamado;

/* Converte um campo numerico positivo obrigatorio; devolve 0 se o ID for invalido. */
static int ler_id(const char *campo, int *destino) {
    char *fim;
    long valor;
    if (!campo[0]) return 0;
    errno = 0;
    valor = strtol(campo, &fim, 10);
    if (errno || *fim || valor <= 0 || valor > INT_MAX) return 0;
    *destino = (int)valor;
    return 1;
}

/* Reconhece os tres estados persistidos do SQL e devolve se o texto e valido. */
static int status_valido(const char *status) {
    return strcmp(status, "aberto") == 0 || strcmp(status, "em_atendimento") == 0 ||
           strcmp(status, "concluido") == 0;
}

/* Retira o fim de linha; rejeita linhas que nao couberam integralmente no buffer. */
static int preparar_linha(char *linha, FILE *arquivo) {
    if (!strchr(linha, '\n') && !feof(arquivo)) return 0;
    linha[strcspn(linha, "\r\n")] = '\0';
    return 1;
}

/* Carrega todo o historico dinamicamente; devolve sucesso sem truncar os registros. */
static int carregar_chamados(Chamado **chamados, size_t *quantidade) {
    FILE *arquivo = fopen(ARQ_CHAMADOS, "r");
    char linha[1024];
    size_t capacidade = 0;
    int ok = 1;
    *chamados = NULL;
    *quantidade = 0;
    if (!arquivo) return errno == ENOENT;
    if (!fgets(linha, sizeof linha, arquivo) || !preparar_linha(linha, arquivo) ||
        strcmp(linha, "id_chamado,id_uh,origem,status_chamado,data_abertura,id_camareira") != 0) {
        fclose(arquivo);
        return 0;
    }
    while (fgets(linha, sizeof linha, arquivo)) {
        char *campos[6];
        Chamado atual = {0};
        if (!preparar_linha(linha, arquivo)) {
            ok = 0;
            break;
        }
        if (!linha[0]) continue;
        if (separar_csv(linha, campos, 6) != 6 || !ler_id(campos[0], &atual.id) ||
            !ler_id(campos[1], &atual.id_uh) ||
            (strcmp(campos[2], "recepcao") != 0 && strcmp(campos[2], "hack") != 0) ||
            !status_valido(campos[3]) || !campos[4][0] || strlen(campos[4]) >= sizeof atual.data_abertura ||
            !ler_id(campos[5], &atual.id_camareira)) {
            ok = 0;
            break;
        }
        for (size_t i = 0; i < *quantidade; ++i) {
            if ((*chamados)[i].id == atual.id) ok = 0;
        }
        if (!ok) break;
        snprintf(atual.origem, sizeof atual.origem, "%s", campos[2]);
        snprintf(atual.status, sizeof atual.status, "%s", campos[3]);
        snprintf(atual.data_abertura, sizeof atual.data_abertura, "%s", campos[4]);
        if (*quantidade == capacidade) {
            size_t nova_capacidade = capacidade ? capacidade * 2 : 32;
            Chamado *novos = realloc(*chamados, nova_capacidade * sizeof *novos);
            if (!novos) {
                ok = 0;
                break;
            }
            *chamados = novos;
            capacidade = nova_capacidade;
        }
        (*chamados)[(*quantidade)++] = atual;
    }
    if (ferror(arquivo)) ok = 0;
    fclose(arquivo);
    if (!ok) {
        free(*chamados);
        *chamados = NULL;
        *quantidade = 0;
    }
    return ok;
}

/* Escreve a lista completa em temporario e confirma a troca pelo CSV oficial. */
static int salvar_chamados(const Chamado chamados[], size_t quantidade) {
    FILE *arquivo = fopen(TMP_CHAMADOS, "w");
    const char *temporarios[] = {TMP_CHAMADOS};
    const char *destinos[] = {ARQ_CHAMADOS};
    int ok;
    if (!arquivo) return 0;
    log_fprintf(arquivo, "id_chamado,id_uh,origem,status_chamado,data_abertura,id_camareira\n");
    for (size_t i = 0; i < quantidade; ++i) {
        log_fprintf(arquivo, "%d,%d,%s,%s,%s,%d\n", chamados[i].id, chamados[i].id_uh,
                    chamados[i].origem, chamados[i].status, chamados[i].data_abertura, chamados[i].id_camareira);
    }
    ok = !ferror(arquivo);
    if (fclose(arquivo) != 0) ok = 0;
    if (ok) ok = confirmar_arquivos(temporarios, destinos, 1);
    if (!ok) remove(TMP_CHAMADOS);
    return ok;
}

/* Verifica se o ID informado existe no cadastro das UHs; devolve 1 se encontrado. */
static int uh_cadastrada(int id_uh) {
    FILE *arquivo = fopen("dados/uh.csv", "r");
    char linha[1024];
    int encontrada = 0;
    if (!arquivo) return 0;
    if (fgets(linha, sizeof linha, arquivo)) {
        while (fgets(linha, sizeof linha, arquivo)) {
            char *campos[6];
            int id;
            if (!preparar_linha(linha, arquivo)) break;
            if (separar_csv(linha, campos, 6) == 6 && ler_id(campos[0], &id) && id == id_uh) {
                encontrada = 1;
                break;
            }
        }
    }
    fclose(arquivo);
    return encontrada;
}

/* Busca a unica atribuicao de hoje para a UH; devolve o ID da responsavel ou zero. */
static int responsavel_do_dia(int id_uh, const char *data, Camareira camareiras[], int quantidade) {
    FILE *arquivo = fopen(ARQ_DISTRIBUICAO, "r");
    char linha[1024];
    int responsavel = 0, encontradas = 0, ok = 1;
    if (!arquivo) return 0;
    if (!fgets(linha, sizeof linha, arquivo)) ok = 0;
    while (ok && fgets(linha, sizeof linha, arquivo)) {
        char *campos[5];
        int uh, camareira;
        if (!preparar_linha(linha, arquivo) || separar_csv(linha, campos, 5) != 5) {
            ok = 0;
            break;
        }
        if (strcmp(campos[3], data) != 0) continue;
        if (!ler_id(campos[2], &uh) || !ler_id(campos[1], &camareira)) {
            ok = 0;
            break;
        }
        if (uh == id_uh) {
            responsavel = camareira;
            ++encontradas;
        }
    }
    if (ferror(arquivo)) ok = 0;
    fclose(arquivo);
    if (!ok || encontradas != 1 || encontrar_camareira(camareiras, quantidade, responsavel) < 0) return 0;
    return responsavel;
}

/* Abre chamado com UH/origem validas e responsavel distribuida hoje; devolve sucesso. */
int chamado_criar(int id_uh, const char *origem, Camareira camareiras[], int quantidade) {
    Chamado *chamados = NULL, novo = {0};
    size_t total;
    char data[11];
    int maior_id = 0, ok;
    if (!origem || (strcmp(origem, "recepcao") != 0 && strcmp(origem, "hack") != 0)) return 0;
    if (!uh_cadastrada(id_uh)) {
        log_puts("UH inexistente. O chamado nao foi criado.");
        return 0;
    }
    obter_data_atual(data, sizeof data);
    novo.id_camareira = responsavel_do_dia(id_uh, data, camareiras, quantidade);
    if (!novo.id_camareira) {
        log_puts("Nao ha uma responsavel valida na distribuicao de hoje para essa UH.");
        log_puts("Gere a distribuicao antes de abrir o chamado.");
        return 0;
    }
    if (!carregar_chamados(&chamados, &total)) {
        log_puts("Nao foi possivel ler o historico de chamados. Nenhum dado foi alterado.");
        return 0;
    }
    for (size_t i = 0; i < total; ++i) {
        if (chamados[i].id > maior_id) maior_id = chamados[i].id;
    }
    if (maior_id == INT_MAX) {
        free(chamados);
        log_puts("Nao ha IDs disponiveis para novos chamados.");
        return 0;
    }
    Chamado *ampliados = realloc(chamados, (total + 1) * sizeof *ampliados);
    if (!ampliados) {
        free(chamados);
        return 0;
    }
    chamados = ampliados;
    novo.id = maior_id + 1;
    novo.id_uh = id_uh;
    snprintf(novo.origem, sizeof novo.origem, "%s", origem);
    snprintf(novo.status, sizeof novo.status, "aberto");
    obter_data_hora(novo.data_abertura, sizeof novo.data_abertura);
    if (!novo.data_abertura[0]) {
        free(chamados);
        return 0;
    }
    chamados[total++] = novo;
    ok = salvar_chamados(chamados, total);
    free(chamados);
    if (ok) log_printf("Chamado %d aberto para UH %d. Camareira responsavel: %d.\n",
                       novo.id, novo.id_uh, novo.id_camareira);
    else log_puts("Falha ao salvar o chamado. Confira o acesso ao arquivo.");
    return ok;
}

/* Exibe chamados no estado solicitado; devolve quantidade listada ou -1 em erro. */
int chamado_listar(const char *status, Camareira camareiras[], int quantidade) {
    Chamado *chamados = NULL;
    size_t total;
    int exibidos = 0;
    if (!status || !status_valido(status) || !carregar_chamados(&chamados, &total)) {
        log_puts("Nao foi possivel ler os chamados.");
        return -1;
    }
    for (size_t i = 0; i < total; ++i) {
        int indice;
        if (strcmp(chamados[i].status, status) != 0) continue;
        indice = encontrar_camareira(camareiras, quantidade, chamados[i].id_camareira);
        log_printf("ID %d | UH %d | %s | %s | ", chamados[i].id, chamados[i].id_uh,
                   chamados[i].origem, chamados[i].data_abertura);
        if (indice >= 0) log_printf("Camareira: %s (%d)\n", camareiras[indice].nome, chamados[i].id_camareira);
        else log_printf("Camareira: %d\n", chamados[i].id_camareira);
        ++exibidos;
    }
    free(chamados);
    if (!exibidos) log_puts("Nenhum chamado nessa situacao.");
    return exibidos;
}

/* Avanca somente aberto -> em_atendimento -> concluido, preservando o historico. */
int chamado_avancar(int id_chamado, const char *status_atual, const char *novo_status) {
    Chamado *chamados = NULL;
    size_t total;
    int encontrado = 0, ok = 0;
    if (!status_atual || !novo_status ||
        !((strcmp(status_atual, "aberto") == 0 && strcmp(novo_status, "em_atendimento") == 0) ||
          (strcmp(status_atual, "em_atendimento") == 0 && strcmp(novo_status, "concluido") == 0))) return 0;
    if (!carregar_chamados(&chamados, &total)) {
        log_puts("Nao foi possivel ler o historico de chamados.");
        return 0;
    }
    for (size_t i = 0; i < total; ++i) {
        if (chamados[i].id != id_chamado || strcmp(chamados[i].status, status_atual) != 0) continue;
        snprintf(chamados[i].status, sizeof chamados[i].status, "%s", novo_status);
        encontrado = 1;
        break;
    }
    if (encontrado) {
        ok = salvar_chamados(chamados, total);
        if (ok) log_printf("Chamado %d atualizado para %s.\n", id_chamado,
                           strcmp(novo_status, "concluido") == 0 ? "fechado" : "em andamento");
        else log_puts("Falha ao salvar a alteracao do chamado.");
    } else log_puts("Chamado inexistente ou fora da situacao selecionada.");
    free(chamados);
    return ok;
}
