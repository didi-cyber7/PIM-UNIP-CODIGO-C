/* Distribuicoes com substituicao so persistem depois da aprovacao da supervisao. */
#include "interno.h"

/* Identifica substituicoes comparando cada UH do dia com as titulares da camareira. */
static int tem_substituicao(const Camareira camareiras[], int quantidade) {
    for (int i = 0; i < quantidade; ++i) {
        for (int j = 0; j < camareiras[i].quantidade_total; ++j) {
            int titular = 0;
            for (int k = 0; k < camareiras[i].quantidade_titular; ++k) {
                if (camareiras[i].uhs_atribuidas[j] == camareiras[i].uhs_titulares[k]) titular = 1;
            }
            if (!titular) return 1;
        }
    }
    return 0;
}

/* Calcula o mapa em memoria, solicita aprovacao quando preciso e salva a distribuicao. */
void tela_distribuicao(Camareira camareiras[], int quantidade) {
    char data[11];
    obter_data_atual(data, sizeof data);
    if (!data[0]) return;
    carregar_faltas_do_dia(camareiras, quantidade, data);
    if (!distribuicao_pode_ser_recalculada(data)) {
        log_puts("Distribuicao bloqueada: ja existe atendimento iniciado/concluido para hoje.");
        return;
    }
    if (!existe_camareira_presente(camareiras, quantidade)) {
        log_puts("Nao ha camareiras presentes. Distribuicao cancelada.");
        return;
    }
    if (!atribuir_titulares(camareiras, quantidade) || !realocar_ausencias(camareiras, quantidade)) return;
    imprimir_mapa(camareiras, quantidade, stdout);
    if (tem_substituicao(camareiras, quantidade) && !ler_senha_supervisao()) return;
    FILE *arquivo = fopen("resultados/mapa_distribuicao.tmp", "w");
    if (!arquivo) {
        log_puts("Erro ao preparar o mapa de distribuicao.");
        return;
    }
    imprimir_mapa(camareiras, quantidade, arquivo);
    int ok = !ferror(arquivo);
    if (fclose(arquivo) != 0) ok = 0;
    if (!ok || !preparar_distribuicao_csv(camareiras, quantidade, data)) {
        remove("resultados/mapa_distribuicao.tmp");
        log_puts("Erro ao salvar distribuicao. O mapa oficial nao foi substituido.");
        return;
    }
    const char *temporarios[] = {"resultados/mapa_distribuicao.tmp", "dados/atribuicao_diaria.tmp"};
    const char *destinos[] = {"resultados/mapa_distribuicao.txt", ARQ_DISTRIBUICAO};
    if (!confirmar_arquivos(temporarios, destinos, 2)) {
        log_puts("Falha ao confirmar TXT e CSV da distribuicao.");
        return;
    }
    salvar_backup_resultado(destinos[0]);
    log_puts("Distribuicao aprovada e salva em TXT e CSV.");
    if (!montar_kits_distribuicao(data)) {
        log_puts("Distribuicao salva, mas kits nao montados. Nenhuma nova reserva parcial foi criada.");
    }
}
