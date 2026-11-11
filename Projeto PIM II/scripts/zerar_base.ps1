<# Restaura os CSVs iniciais e limpa dados operacionais e resultados, com backup previo. #>
[CmdletBinding(SupportsShouldProcess)]
param()

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$raizProjeto = [IO.Path]::GetFullPath((Split-Path -Parent $PSScriptRoot)).TrimEnd('\')
$baseInicial = Join-Path $PSScriptRoot 'base_inicial'
$utf8 = [Text.UTF8Encoding]::new($false)

# Confere cada caminho absoluto antes de copiar, escrever ou apagar arquivos do reset.
function Assert-DentroDoProjeto([string]$Caminho) {
    $absoluto = [IO.Path]::GetFullPath($Caminho)
    if (-not $absoluto.StartsWith($raizProjeto + '\', [StringComparison]::OrdinalIgnoreCase)) {
        throw "Caminho fora do projeto: $absoluto"
    }
}

# Seleciona somente dados e resultados gerados; documentos e fontes ficam preservados.
function Get-ArquivosParaLimpar {
    foreach ($pasta in @('dados', 'resultados')) {
        $caminho = Join-Path $raizProjeto $pasta
        Assert-DentroDoProjeto $caminho
        if (-not (Test-Path -LiteralPath $caminho)) { continue }
        $itens = @(Get-ChildItem -LiteralPath $caminho -Recurse -Force)
        foreach ($item in @((Get-Item -LiteralPath $caminho)) + $itens) {
            Assert-DentroDoProjeto $item.FullName
            if (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -and
                ($item.Attributes -band [IO.FileAttributes]::Directory)) {
                throw "Pasta vinculada encontrada; reset cancelado: $($item.FullName)"
            }
        }
        $itens | Where-Object { -not $_.PSIsContainer -and $_.Extension -in @('.csv', '.txt', '.tmp', '.rollback') }
    }
}

# Apaga apenas arquivos previamente validados, sem exclusao recursiva de diretorios.
function Clear-ArquivosGerados {
    foreach ($arquivo in @(Get-ArquivosParaLimpar)) {
        Assert-DentroDoProjeto $arquivo.FullName
        Remove-Item -LiteralPath $arquivo.FullName -Force
    }
}

$esperados = @('atribuicao_diaria.csv', 'camareira.csv', 'chamado.csv', 'colaborador.csv',
    'config_camareiras.csv', 'consumo_uh.csv', 'entrega_kit.csv', 'entrega_kit_item.csv',
    'entrega_kit_uh.csv', 'faltas.csv', 'item_composicao_padrao.csv', 'item_enxoval.csv',
    'item_lote.csv', 'lote_lavanderia.csv', 'movimentacao_estoque.csv',
    'registro_ocorrencias.csv', 'tipologia_uh.csv', 'uh.csv')
foreach ($nome in $esperados) {
    $arquivo = Join-Path $baseInicial $nome
    if (-not (Test-Path -LiteralPath $arquivo -PathType Leaf)) { throw "Base inicial incompleta: $nome" }
    if (-not [IO.File]::ReadAllLines($arquivo, [Text.Encoding]::UTF8).Length) { throw "CSV inicial vazio: $nome" }
}
$estoque = @(Import-Csv -LiteralPath (Join-Path $baseInicial 'item_enxoval.csv') -Encoding UTF8)
if ($estoque.Count -ne 4380 -or @($estoque | Where-Object {
    $_.situacao -ne 'em_estoque' -or $_.ciclos_lavagem -ne '0' -or $_.motivo_baixa -or $_.responsavel_baixa
}).Count) { throw 'Estoque inicial deve conter 4.380 pecas disponiveis, ciclos zerados e sem baixa.' }
if (@(Import-Csv (Join-Path $baseInicial 'camareira.csv') -Encoding UTF8).Count -ne 8 -or
    @(Import-Csv (Join-Path $baseInicial 'uh.csv') -Encoding UTF8).Count -ne 15) {
    throw 'Base inicial de camareiras/UHs inconsistente.'
}
$colaboradores = @(Import-Csv (Join-Path $baseInicial 'colaborador.csv') -Encoding UTF8)
if ($colaboradores.Count -ne 10 -or
    @($colaboradores | Where-Object { $_.cargo -eq 'camareira' }).Count -ne 8 -or
    @($colaboradores | Where-Object { $_.cargo -eq 'governanta' }).Count -ne 1 -or
    @($colaboradores | Where-Object { $_.cargo -eq 'roupeira' }).Count -ne 1) {
    throw 'Base inicial deve conter oito camareiras, uma governanta e uma roupeira.'
}
$dupla = @(Import-Csv (Join-Path $baseInicial 'tipologia_uh.csv') -Encoding UTF8 |
    Where-Object { $_.id_tipologia -eq '2' })
if ($dupla.Count -ne 1 -or $dupla[0].qtd_camas -ne '2' -or $dupla[0].num_leitos -ne '2') {
    throw 'Tipologia Dupla viuvo deve conter duas camas e dois leitos.'
}
# Enumera antes de qualquer mudanca para cancelar caso haja um caminho nao permitido.
$null = @(Get-ArquivosParaLimpar)
if (-not $PSCmdlet.ShouldProcess($raizProjeto, 'Restaurar base inicial e limpar CSVs/TXTs operacionais')) { return }

$backup = Join-Path $raizProjeto ('backups/reset_base_' + (Get-Date -Format 'yyyyMMdd_HHmmss') + '_' +
    [guid]::NewGuid().ToString('N').Substring(0, 8))
Assert-DentroDoProjeto $backup
New-Item -ItemType Directory -Path $backup | Out-Null
foreach ($nome in @('dados', 'resultados')) {
    $origem = Join-Path $raizProjeto $nome
    if (Test-Path -LiteralPath $origem) { Copy-Item -LiteralPath $origem -Destination $backup -Recurse -Force }
}
# So limpa depois que a copia de seguranca das duas pastas terminou sem erro.
try {
    Clear-ArquivosGerados
    $dados = Join-Path $raizProjeto 'dados'
    New-Item -ItemType Directory -Path $dados -Force | Out-Null
    foreach ($nome in $esperados) {
        $destino = Join-Path $dados $nome
        Assert-DentroDoProjeto $destino
        $texto = [IO.File]::ReadAllText((Join-Path $baseInicial $nome), [Text.Encoding]::UTF8)
        [IO.File]::WriteAllText($destino, $texto, $utf8)
    }
    New-Item -ItemType Directory -Path (Join-Path $raizProjeto 'resultados/BKP') -Force | Out-Null
} catch {
    $falha = $_
    try {
        foreach ($nome in @('dados', 'resultados')) {
            $origem = Join-Path $backup $nome
            $destino = Join-Path $raizProjeto $nome
            if (Test-Path -LiteralPath $origem) {
                New-Item -ItemType Directory -Path $destino -Force | Out-Null
                Get-ChildItem -LiteralPath $origem -Force | ForEach-Object {
                    Copy-Item -LiteralPath $_.FullName -Destination $destino -Recurse -Force
                }
            }
        }
    } catch { Write-Warning "Restauracao incompleta; os originais estao em $backup" }
    throw "Reset nao concluido: $($falha.Exception.Message). Backup preservado em $backup"
}
Write-Output 'Base zerada: 4.380 pecas em_estoque, ciclos zerados, 15 UHs, 8 camareiras, 1 governanta e 1 roupeira.'
Write-Output 'Historicos, relatorios e configuracoes adicionais foram limpos; senha padrao: supervisao123.'
Write-Output ('Backup anterior: ' + $backup)
