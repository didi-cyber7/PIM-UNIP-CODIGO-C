param([string]$Destino)

$ErrorActionPreference = 'Stop'
$raizProjeto = Split-Path -Parent $PSScriptRoot
if (-not $Destino) {
    $Destino = Join-Path ([IO.Path]::GetTempPath()) ('PIM_II_sem_comentarios_' + (Get-Date -Format 'yyyyMMdd_HHmmss'))
}
$destinoCompleto = [IO.Path]::GetFullPath($Destino)
$raizCompleta = [IO.Path]::GetFullPath($raizProjeto).TrimEnd('\')
if ($destinoCompleto.Equals($raizCompleta, [StringComparison]::OrdinalIgnoreCase) -or
    $destinoCompleto.StartsWith($raizCompleta + '\', [StringComparison]::OrdinalIgnoreCase)) {
    throw 'O destino deve ficar fora da pasta do projeto, para evitar copiar a exportacao para dentro dela mesma.'
}
if (Test-Path -LiteralPath $destinoCompleto) {
    throw 'O destino ja existe. Escolha uma pasta nova para preservar entregas anteriores.'
}

# Remove comentarios C com um pequeno analisador que preserva strings, caracteres e quebras de linha.
function Remove-CComments([string]$Texto) {
    $saida = [Text.StringBuilder]::new()
    $estado = 'codigo'
    for ($indice = 0; $indice -lt $Texto.Length; $indice++) {
        $atual = $Texto[$indice]
        $proximo = [char]0
        if ($indice + 1 -lt $Texto.Length) { $proximo = $Texto[$indice + 1] }
        if ($estado -eq 'linha') {
            if ($atual -eq "`n") { [void]$saida.Append($atual); $estado = 'codigo' }
            continue
        }
        if ($estado -eq 'bloco') {
            if ($atual -eq '*' -and $proximo -eq '/') { $indice++; $estado = 'codigo' }
            elseif ($atual -eq "`n" -or $atual -eq "`r") { [void]$saida.Append($atual) }
            continue
        }
        if ($estado -eq 'string' -or $estado -eq 'caractere') {
            [void]$saida.Append($atual)
            if ($atual -eq '\' -and $indice + 1 -lt $Texto.Length) {
                $indice++
                [void]$saida.Append($Texto[$indice])
            } elseif (($estado -eq 'string' -and $atual -eq '"') -or
                      ($estado -eq 'caractere' -and $atual -eq "'")) {
                $estado = 'codigo'
            }
            continue
        }
        if ($atual -eq '/' -and $proximo -eq '/') {
            [void]$saida.Append(' ')
            $indice++
            $estado = 'linha'
        } elseif ($atual -eq '/' -and $proximo -eq '*') {
            [void]$saida.Append(' ')
            $indice++
            $estado = 'bloco'
        } else {
            [void]$saida.Append($atual)
            if ($atual -eq '"') { $estado = 'string' }
            elseif ($atual -eq "'") { $estado = 'caractere' }
        }
    }
    if ($estado -eq 'bloco') { throw 'Comentario C nao terminado na origem.' }
    return $saida.ToString()
}

New-Item -ItemType Directory -Path $destinoCompleto | Out-Null
Get-ChildItem -LiteralPath $raizProjeto -Force | Where-Object {
    $_.Name -notin @('.git', '.agents', '.codex', '.verificacao_relatorios')
} | ForEach-Object {
    Copy-Item -LiteralPath $_.FullName -Destination $destinoCompleto -Recurse -Force
}
$utf8 = [Text.UTF8Encoding]::new($false)
Get-ChildItem -LiteralPath (Join-Path $destinoCompleto 'src'),(Join-Path $destinoCompleto 'include') -Recurse -File |
    Where-Object { $_.Extension -in @('.c', '.h') } | ForEach-Object {
        $texto = [IO.File]::ReadAllText($_.FullName, [Text.Encoding]::UTF8)
        [IO.File]::WriteAllText($_.FullName, (Remove-CComments $texto), $utf8)
    }
Write-Output ('Versao sem comentarios gerada em: ' + $destinoCompleto)
