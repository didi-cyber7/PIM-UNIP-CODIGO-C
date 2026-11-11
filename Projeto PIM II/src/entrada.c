/* Entrada por linha: rejeita sobras, encerra em EOF e permite campos com espacos. */
#include "interno.h"
#include <ctype.h>

/* Le uma linha completa; devolve 0 em EOF e -1 se exceder o buffer. */
static int ler_linha(char *destino, size_t tamanho, int registrar) {
    if (!fgets(destino, (int)tamanho, stdin)) return 0;
    if (!strchr(destino, '\n') && !feof(stdin)) {
        int caractere;
        while ((caractere = getchar()) != '\n' && caractere != EOF) {}
        log_puts("Entrada longa demais.");
        return -1;
    }
    destino[strcspn(destino, "\r\n")] = '\0';
    if (registrar) log_registrar_entrada(destino);
    return 1;
}

/* Solicita inteiro no intervalo; repete entradas invalidas e devolve 0 somente em EOF. */
int ler_inteiro_prompt(const char *mensagem, int minimo, int maximo, int *valor) {
    char entrada[128];
    for (;;) {
        log_printf("%s", mensagem);
        int lido = ler_linha(entrada, sizeof entrada, 1);
        if (!lido) return 0;
        if (lido < 0) continue;
        char *fim;
        errno = 0;
        long numero = strtol(entrada, &fim, 10);
        while (isspace((unsigned char)*fim)) ++fim;
        if (fim != entrada && !*fim && !errno && numero >= minimo && numero <= maximo) {
            *valor = (int)numero;
            return 1;
        }
        log_printf("Informe um inteiro de %d a %d.\n", minimo, maximo);
    }
}

/* Solicita decimal finito no intervalo; aceita ponto ou virgula e devolve 0 em EOF. */
int ler_decimal_prompt(const char *mensagem, double minimo, double maximo, double *valor) {
    char entrada[128];
    for (;;) {
        log_printf("%s", mensagem);
        int lido = ler_linha(entrada, sizeof entrada, 1);
        if (!lido) return 0;
        if (lido < 0) continue;
        for (char *p = entrada; *p; ++p) if (*p == ',') *p = '.';
        char *fim;
        errno = 0;
        double numero = strtod(entrada, &fim);
        while (isspace((unsigned char)*fim)) ++fim;
        if (fim != entrada && !*fim && !errno && numero >= minimo && numero <= maximo) {
            *valor = numero;
            return 1;
        }
        log_printf("Informe um numero entre %.2f e %.2f.\n", minimo, maximo);
    }
}

/* Solicita texto nao vazio sem separadores CSV; devolve 0 em EOF. */
int ler_texto_prompt(const char *mensagem, char *destino, size_t tamanho) {
    for (;;) {
        log_printf("%s", mensagem);
        int lido = ler_linha(destino, tamanho, 1);
        if (!lido) return 0;
        if (lido < 0) continue;
        if (destino[0] && !strchr(destino, ',') && !strchr(destino, '"')) return 1;
        log_puts("Informe um texto sem virgulas ou aspas.");
    }
}

/* Autoriza substituicoes com senha unica de demonstracao, sem registrar o segredo no log. */
int ler_senha_supervisao(void) {
    char senha[128] = "supervisao123";
    char entrada[128];
    FILE *arquivo = fopen("dados/senha_supervisao.txt", "r");
    if (arquivo) {
        if (fgets(senha, sizeof senha, arquivo)) senha[strcspn(senha, "\r\n")] = '\0';
        fclose(arquivo);
    }
    if (!senha[0]) {
        log_puts("Senha de supervisao vazia: configure dados/senha_supervisao.txt.");
        return 0;
    }
    for (;;) {
        log_printf("Senha da supervisao (0 cancela): ");
        int lido = ler_linha(entrada, sizeof entrada, 0);
        if (!lido || (lido > 0 && strcmp(entrada, "0") == 0)) {
            log_puts("Autorizacao cancelada. Distribuicao nao salva.");
            return 0;
        }
        if (lido > 0 && strcmp(entrada, senha) == 0) {
            log_puts("Distribuicao aprovada pela supervisao.");
            return 1;
        }
        log_puts("Senha incorreta. A aprovacao continua pendente.");
    }
}

/* Preenche a data local atual em ISO; devolve string vazia se o relogio falhar. */
void obter_data_atual(char *destino, size_t tamanho) {
    time_t agora = time(NULL);
    struct tm *local = localtime(&agora);
    if (!local || !strftime(destino, tamanho, "%Y-%m-%d", local)) destino[0] = '\0';
}

/* Preenche data e hora locais no formato usado pelos CSVs. */
void obter_data_hora(char *destino, size_t tamanho) {
    time_t agora = time(NULL);
    struct tm *local = localtime(&agora);
    if (!local || !strftime(destino, tamanho, "%Y-%m-%d %H:%M:%S", local)) destino[0] = '\0';
}
