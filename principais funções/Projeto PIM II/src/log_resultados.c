/*
 * LOG E BACKUP: registra na tela e no TXT da sessao simultaneamente.
 * As telas chamam log_printf/log_puts/log_fprintf diretamente, sem macros da libc.
 * O backup com timestamp e criado APOS fechar o arquivo oficial.
 */
#include "projeto.h"
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#ifdef _WIN32
#include <direct.h>
#include <io.h>
#define CRIAR_PASTA(path) _mkdir(path)
#define TERMINAL_INTERATIVO() _isatty(_fileno(stdout))
#else
#include <sys/stat.h>
#include <unistd.h>
#define CRIAR_PASTA(path) mkdir(path, 0775)
#define TERMINAL_INTERATIVO() isatty(STDOUT_FILENO)
#endif

static FILE *log_arquivo = NULL;
static char caminho_log[256] = "";

/* Cria a pasta recebida; considera sucesso se ela ja existir. */
static int criar_pasta(const char *caminho) {
    return CRIAR_PASTA(caminho) == 0 || errno == EEXIST;
}

/* Garante as pastas de relatorios e backups e devolve 0 se nao puder cria-las. */
int preparar_resultados(void) {
    return criar_pasta("resultados") && criar_pasta("resultados/BKP");
}

/* Escolhe um caminho unico; sufixos evitam sobrescrever arquivos do mesmo segundo. */
static int nome_disponivel(char *destino, size_t tamanho, const char *pasta,
                          const char *nome, const char *extensao) {
    time_t agora = time(NULL);
    struct tm *local = localtime(&agora);
    char data[32];
    if (!local || !strftime(data, sizeof data, "%Y%m%d_%H%M%S", local)) return 0;
    for (int numero = 0; numero < 10000; ++numero) {
        if (numero == 0)
            snprintf(destino, tamanho, "%s/%s_%s.%s", pasta, nome, data, extensao);
        else
            snprintf(destino, tamanho, "%s/%s_%s_%03d.%s", pasta, nome, data, numero, extensao);
        FILE *teste = fopen(destino, "rb");
        if (!teste) return 1;
        fclose(teste);
    }
    return 0;
}

/* Copia os bytes entre caminhos e devolve 0 se a leitura ou escrita falhar. */
static int copiar_arquivo(const char *origem, const char *destino) {
    FILE *entrada = fopen(origem, "rb");
    if (!entrada) return 0;
    FILE *saida = fopen(destino, "wb");
    if (!saida) { fclose(entrada); return 0; }
    char buffer[8192];
    size_t lidos;
    int ok = 1;
    while ((lidos = fread(buffer, 1, sizeof buffer, entrada)) != 0) {
        if (fwrite(buffer, 1, lidos, saida) != lidos) { ok = 0; break; }
    }
    if (ferror(entrada)) ok = 0;
    if (fclose(saida) != 0) ok = 0;
    fclose(entrada);
    if (!ok) remove(destino);
    return ok;
}

/* Copia um relatorio TXT/CSV para um nome datado unico na pasta BKP. */
int salvar_backup_resultado(const char *arquivo) {
    char nome[128], destino[256];
    const char *inicio = strrchr(arquivo, '/');
    inicio = inicio ? inicio + 1 : arquivo;
    const char *ponto = strrchr(inicio, '.');
    if (!ponto || (strcmp(ponto, ".txt") != 0 && strcmp(ponto, ".csv") != 0) ||
        !preparar_resultados()) return 0;
    size_t tamanho = (size_t)(ponto - inicio);
    if (tamanho == 0 || tamanho >= sizeof nome) return 0;
    memcpy(nome, inicio, tamanho);
    nome[tamanho] = '\0';
    return nome_disponivel(destino, sizeof destino, "resultados/BKP", nome, ponto + 1) &&
           copiar_arquivo(arquivo, destino);
}

/* Preserva o log anterior e abre resultados/log_console.txt para a sessao atual. */
int iniciar_log(void) {
    if (!preparar_resultados()) return 0;
    snprintf(caminho_log, sizeof caminho_log, "resultados/log_console.txt");
    FILE *anterior = fopen(caminho_log, "r");
    if (anterior) {
        fclose(anterior);
        if (!salvar_backup_resultado(caminho_log)) return 0;
    }
    log_arquivo = fopen(caminho_log, "w");
    if (!log_arquivo) return 0;
    log_printf("=== INICIO DA SESSAO ===\nArquivo: %s\n", caminho_log);
    return 1;
}

/* Fecha o log oficial e cria uma copia datada, sem apagar backups anteriores. */
void finalizar_log(void) {
    if (!log_arquivo) return;
    log_puts("=== FIM DA SESSAO ===");
    fflush(log_arquivo);
    fclose(log_arquivo);
    log_arquivo = NULL;
    if (!salvar_backup_resultado(caminho_log))
        fprintf(stderr, "Aviso: backup do log nao foi criado.\n");
}

/* Limpa somente um terminal interativo, preservando saida redirecionada e log. */
void limpar_console(void) {
    if (!TERMINAL_INTERATIVO()) return;
#ifdef _WIN32
    system("cls");
#else
    fputs("\033[2J\033[H", stdout);
    fflush(stdout);
#endif
}

/* Escreve na saida real e replica no log, sem alterar as mensagens das telas. */
static int log_vfprintf(FILE *destino, const char *formato, va_list argumentos) {
    va_list copia;
    va_copy(copia, argumentos);
    int resultado = vfprintf(destino, formato, argumentos);
    if ((destino == stdout || destino == stderr) && log_arquivo) {
        vfprintf(log_arquivo, formato, copia);
        fflush(log_arquivo);
    }
    va_end(copia);
    if (destino == stdout) fflush(stdout);
    return resultado;
}

/* Exibe a mensagem formatada e replica sua saida no log aberto. */
int log_printf(const char *formato, ...) {
    va_list argumentos;
    va_start(argumentos, formato);
    int resultado = log_vfprintf(stdout, formato, argumentos);
    va_end(argumentos);
    return resultado;
}

/* Escreve no fluxo recebido e replica no log apenas saida de console. */
int log_fprintf(FILE *destino, const char *formato, ...) {
    va_list argumentos;
    va_start(argumentos, formato);
    int resultado = log_vfprintf(destino, formato, argumentos);
    va_end(argumentos);
    return resultado;
}

/* Exibe texto com quebra de linha e replica a mesma mensagem no log. */
int log_puts(const char *texto) {
    int resultado = puts(texto);
    if (log_arquivo) { fprintf(log_arquivo, "%s\n", texto); fflush(log_arquivo); }
    return resultado;
}

/* Wrapper legado para leitura de um campo e registro da entrada; novos formularios usam linhas. */
int log_scanf(const char *formato, void *destino) {
    int resultado = scanf(formato, destino);
    if (log_arquivo) {
        if (resultado == 1 && strcmp(formato, "%d") == 0)
            fprintf(log_arquivo, "[entrada] %d\n", *(int *)destino);
        else if (resultado == 1 && strcmp(formato, "%63s") == 0)
            fprintf(log_arquivo, "[entrada] %s\n", (char *)destino);
        else
            fprintf(log_arquivo, "[entrada] nao lida ou invalida\n");
        fflush(log_arquivo);
    }
    return resultado;
}

/* Registra uma entrada de formulario; a senha usa leitura separada e nao chama esta funcao. */
void log_registrar_entrada(const char *texto) {
    if (log_arquivo) {
        fprintf(log_arquivo, "[entrada] %s\n", texto);
        fflush(log_arquivo);
    }
}
