/*
 * INDICADORES: combina entregas e consumos para contar UHs atendidas,
 * distinguir titular/substituicao e calcular tempo medio em minutos.
 * O TXT oficial e sobrescrito; cada versao tambem vai para resultados/BKP.
 */
#include <stdio.h>
#include "projeto.h"
#include "relatorios.h"

/* Gera o agregado historico existente e suas exportacoes TXT/CSV. */
void tela_produtividade(Camareira camareiras[], int quantidade) {
    RegistroAtendimento registros[MAX_REGISTROS];
    int quantidade_registros = carregar_atendimentos(registros, MAX_REGISTROS);
    if (quantidade_registros < 0) {
        log_printf("Erro ao ler entrega_kit.csv e consumo_uh.csv.\n");
        return;
    }
    Indicador indicadores[MAX_CAMAREIRAS];
    calcular_indicadores(registros, quantidade_registros, camareiras, quantidade, indicadores);
    ordenar_indicadores(indicadores, quantidade);

    FILE *arquivo = fopen("resultados/produtividade.txt", "w");
    if (!arquivo) {
        log_printf("Erro ao criar resultados/produtividade.txt.\n");
        return;
    }
    imprimir_indicadores(indicadores, quantidade, stdout);
    imprimir_indicadores(indicadores, quantidade, arquivo);
    fclose(arquivo);
    if (!salvar_backup_resultado("resultados/produtividade.txt"))
        log_puts("Aviso: backup dos indicadores nao foi criado.");
    exportar_produtividade_csv(camareiras, quantidade);
    log_printf("Indicadores salvos em resultados/produtividade.txt e .csv\n");
}
