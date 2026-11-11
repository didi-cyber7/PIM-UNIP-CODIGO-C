PROJETO PIM II - HOTEL LE JARDIN
==============================

DESCRICAO E ESCOPO
-----------------
Sistema academico de terminal em C11 para Governanca de hotel, com persistencia
local em CSV. O SQL de outros/ e referencia de dados; nao ha servidor SQL
conectado. Execute o programa a partir da raiz do projeto.

Distribuicao/realocacao e produtividade compoem o escopo academico original.
Nesta continuacao, a PO confirmou a produtividade historica, sem filtros de
turno ou periodo. Estoque, lavanderia, relatorios gerenciais e chamados atendem
as ampliacoes solicitadas nesta etapa. Os documentos de referencia nao foram
reescritos; anotacoes antigas podem descrever propostas ou fluxos anteriores.

BASE ATUAL
----------
- 8 camareiras e 15 UHs operacionais: 10 King e 5 Dupla viuvo.
- 10 colaboradores: oito camareiras, Marina (ID 9, governanta) e Lucia
  (ID 10, roupeira). Apenas as oito camareiras participam da distribuicao.
- Dupla viuvo possui duas camas e dois leitos; seu kit mantem 17 pecas.
- 4.380 pecas cadastradas. Na revisao atual, 4.155 estao em_estoque e
  225 em_uso, reservadas nas entregas. A execucao altera esses numeros.
- Uma distribuicao completa das 15 UHs usa 225 pecas em 15 entregas.
- O planejamento de referencia considera 80 King, 20 Dupla e tres trocas:
  (80 x 14 + 20 x 17) x 3 = 4.380 pecas. Essa capacidade de 100 UHs e
  diferente da demonstracao operacional de 15 UHs.
- A revisao acrescentou dois colaboradores, corrigiu os leitos e atualizou
  o cabecalho de chamado.csv, que estava sem registros. IDs, RFIDs e
  historicos existentes foram preservados; a base nao foi reinicializada.

ZERAR A BASE PARA UMA INSTALACAO LIMPA
------------------------------------
Feche o programa e execute na raiz:

powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\zerar_base.ps1

O script restaura a referencia fixa de scripts/base_inicial/: 4.380 pecas,
todas em_estoque, ciclos zerados, sem motivos/responsaveis de baixa;
15 UHs, 8 camareiras, 1 governanta, 1 roupeira, titulares, tipologias e
composicoes iniciais. Dupla viuvo retorna com dois leitos.
Pecas/cadastros acrescentados depois da instalacao sao removidos.
Faltas, distribuicoes, entregas, consumos, ocorrencias, movimentacoes,
lotes e chamados ficam apenas com os cabecalhos CSV.

Limpa CSVs/TXTs extras e temporarios dentro de dados/ e resultados/,
inclusive copias como "atribuicao_diaria (1).csv", logs e resultados/BKP/.
Parametros de estoque e senha personalizada tambem sao removidos:
o sistema volta ao planejamento padrao e a senha supervisao123.
Nao apaga fontes, scripts, README nem documentos de outros/.

ANTES da limpeza, copia dados/ e resultados/ para uma pasta unica em
backups/reset_base_DATA_HORA_.../. Backups de resets anteriores permanecem.
Para apenas visualizar a operacao, acrescente -WhatIf ao comando.
O script foi testado em copias; nao foi executado sobre sua base atual.

COMPILAR E EXECUTAR (POWERSHELL)
--------------------------------------
Na raiz, com GCC disponivel no PATH:

gcc -std=c11 -Wall -Wextra -Wpedantic -Iinclude (Get-ChildItem src -Recurse -Filter *.c).FullName -o programa.exe

.\programa.exe

O comando inclui automaticamente todos os modulos de src/.


MENU PRINCIPAL
--------------
1) Distribuicao e substituicao de UHs
2) Registro de faltas
3) Relatorios
4) Registro pos-atendimento
5) Estoque e lavanderia
6) Chamados
0) Sair

FALTAS E DISTRIBUICAO
--------------------
- Faltas sao registradas por data. Remover uma falta tambem remove sua linha
  dessa data no CSV, preservando registros de outros dias.
- Camareiras presentes mantem UHs titulares. UHs de ausentes sao realocadas
  pela menor carga, com desempate pelo menor ID da camareira.
- Com qualquer substituicao, o mapa calculado aguarda senha da supervisao
  ANTES de salvar atribuicao CSV, mapa TXT ou reservar kits.
- Senha unica de demonstracao: supervisao123. Para substituir, crie
  dados/senha_supervisao.txt com a senha na primeira linha. A entrada da
  senha nao e registrada no log. E um mecanismo didatico, sem contas por
  usuario nem infraestrutura de autenticacao.
- Senha incorreta mantem a solicitacao aberta; 0 ou encerramento da entrada
  cancelam a aprovacao e preservam a distribuicao anteriormente salva.
- Sem substituicao, segue diretamente. Com todas ausentes, cancela.
- Atribuicoes de outras datas sao preservadas. Nao e permitido recalcular
  apos atendimento iniciado/concluido na data.
- Mapa oficial: resultados/mapa_distribuicao.txt. CSV operacional:
  dados/atribuicao_diaria.csv.

MONTAGEM DOS KITS
----------------
- Consulta uh.csv -> tipologia_uh.csv -> item_composicao_padrao.csv.
- Seleciona somente pecas em_estoque, respeitando tipo e tamanho de cama.
- Verifica todas as necessidades antes de confirmar reservas. Falta de
  estoque nao cria novos kits parciais nem altera reservas anteriores.
- Pecas reservadas passam a em_uso, com entrega por UH/camareira/data e
  vinculos individuais em entrega_kit_item.csv.
- Regerar antes de atendimento permite substituir reservas pendentes.
  Pecas de entregas atendidas nao sao liberadas por essa operacao.
- Se a distribuicao foi salva mas faltou estoque, o mapa permanece salvo.
  Apos repor, regenere antes de atender ou monte um kit pela tela de estoque.
- Criar novo kit permite King ou Dupla viuvo. Exige UH e camareira da
  distribuicao de hoje, tipologia correspondente e ausencia de entrega
  conflitante. Nao cria reserva sem destino nem novo tipo de kit.

POS-ATENDIMENTO E PRODUTIVIDADE
-----------------------------
- Exige UH da distribuicao do dia e entrega pendente para a camareira.
- Valida inicio/fim em HH:MM, com fim posterior ao inicio no mesmo dia.
- Manchada: consumo com peca_manchada=1 e situacao em_remocao_manchas.
  Avariada: pendente_avaliacao. Detalhes em registro_ocorrencias.csv.
- Faltante: ocorrencia e situacao pendente_avaliacao, sem motivo ou
  responsabilidade de baixa. NAO gera baixa automatica nem linha no consumo.
  A falta so vira perda confirmada depois da decisao na fila de avaliacao.
- Demais pecas consumidas continuam em_uso ate o fluxo de lavanderia.
- entrega_kit.data_hora_entrega armazena o inicio. O auxiliar
  entrega_kit_uh.csv armazena data_hora_fim, inclusive se todas as pecas
  forem faltantes; assim o atendimento tem duracao e produtividade.
  O leitor tambem aceita o formato auxiliar antigo com tres colunas.
- Indicadores agregam o historico carregado, distinguem titular/substituicao
  e ordenam pelo tempo medio crescente. A decisao atual da PO dispensa
  recorte de turno ou periodo.
- O pos-atendimento pode atualizar produtividade.txt e produtividade.csv;
  os mesmos indicadores ficam no menu exclusivo de relatorios.

ESTOQUE E LAVANDERIA
-------------------
- O menu possui uma unica opcao "5 - Lavanderia". Ela abre o submenu
  "1 - Ida / registrar envio" e "2 - Volta / conferir pecas".
  Na volta, classifique por ID: relavagem, remocao de manchas ou avariado.
- Consulta: quantidades por situacao e tipo/tamanho; busca/listagem por ID
  com RFID, ciclos atuais e limite de ciclos.
- Entrada: oito tipos rotativos (lencois baixo/cima, fronha, toalhas de
  banho/rosto/piso, roupao e capa_duvet). Lencois e capa pedem tamanho King
  ou viuvo; os demais nao usam tamanho. Informa quantidade, limite de
  ciclos, fornecedor, custo unitario, colaborador e RFID unico por peca.
  Gera entrada_compra em movimentacao_estoque.csv e estado em_estoque.
  Travesseiros permanentes nao fazem parte das entradas desta tela.
- Saida: baixa individual com motivo e responsabilidade; preserva a peca
  no cadastro e registra baixa em movimentacao_estoque.csv. Impede baixa
  manual de pecas reservadas ou ainda em transito sem divergencia conferida.
  Pendencias de lavanderia podem ser baixadas apos avaliacao, mesmo sem
  retorno fisico; a operacao encerra o acompanhamento sem inventar recebimento.
- Opcao 6 - Fila de avaliacao: lista todas as pecas pendente_avaliacao.
  Selecione o ID e escolha Localizada (em_estoque) ou Confirmar baixa
  (perda/avaria e responsavel: hospede, lavanderia, equipe ou desgaste natural).
  As duas acoes exigem colaborador existente e confirmacao, com movimento
  de contagem ou baixa. Cancelamento/EOF nao gravam dados. Nenhuma divergencia
  gera baixa automaticamente, inclusive falta na UH ou falta na lavanderia.
- Ida: seleciona pecas elegiveis nao reservadas e nao enviadas em outro lote.
  Lavagem comum aceita somente em_uso; remocao de manchas aceita somente
  em_remocao_manchas; relavagem aceita somente em_relavagem. O mesmo filtro
  vale na listagem, nos IDs individuais e em "todas as pecas", preservando
  as marcas de tratamento das demais pecas. Registra lote, vinculos por
  peca, motivo, data/hora e colaborador responsavel.
- Operacoes de estoque/lavanderia listam ID, nome e cargo do colaborador
  antes de solicitar o operador. Na demonstracao, use Lucia (ID 10,
  roupeira) para enviar/conferir lotes e Marina (ID 9, governanta) na avaliacao.
- Volta: lista lotes com pecas sem destino final, mesmo que toda a quantidade
  ja tenha retornado. No primeiro recebimento, pecas sem defeitos sao
  conformes; marque por ID relavagem, manchas, avaria ou NAO retornou.
  Falta gera alerta e pendente_avaliacao; nao fica computada como lavagem comum.
  Pendencias anteriores permanecem selecionadas ate decisao explicita.
  Restaurar conforme confirma regularizacao ou chegada tardia.
- Conforme volta para em_estoque. Relavagem/manchas ficam nos estados
  em_relavagem/em_remocao_manchas e podem ser enviadas novamente.
  Avariada fica pendente_avaliacao. As telas informam que a baixa depende
  de avaliacao explicita com motivo e responsavel; nao ocorre baixa direta.
- Cada primeiro retorno fisico em um lote incrementa um ciclo. Reavaliar
  a mesma inspecao nao incrementa outra vez. Novo envio/retorno de relavagem
  incrementa um novo ciclo. Peca que nao retornou fica na fila e no acompanhamento,
  sem ciclos adicionais. Localizada pela avaliacao nao comprova lavagem:
  volta ao estoque sem incrementar ciclos; concilia o recebimento se estava faltando.
  Baixa confirmada nunca aumenta qtd_recebida. data_retorno registra retorno
  fisico integral; pode permanecer vazia em lote resolvido com pecas perdidas.
- item_lote registra conforme, necessita_relavagem ou avariado, conforme
  SQL. O estado da peca distingue relavagem de remocao de manchas.
  O CSV tem uma quinta coluna auxiliar encerrado: separa retorno fisico de
  resolucao definitiva ou transferencia do retrabalho para novo lote.
  Leitura aceita o formato antigo com quatro colunas. O SQL de referencia
  nao foi alterado: essa extensao do prototipo deve ser conciliada em eventual importacao.
  Vinculos encerrados nao reabrem quando a peca for usada em outro atendimento.
- Estados de lavanderia podem incluir pecas aguardando novo envio; lotes
  identificam o que foi efetivamente enviado/recebido.
- Atingir limite de ciclos sinaliza reposicao nos relatorios; nao baixa a
  peca automaticamente. A governanta decide a saida. Nao ha integracao
  externa com lavanderia nem leitura RFID automatica.

RELATORIOS COMPARTILHAVEIS
-------------------------
O menu Relatorios oferece produtividade, gerencial, painel, compras e
configuracao do minimo. Arquivos oficiais em resultados/ e copias datadas
em resultados/BKP/, nos dois formatos TXT e CSV:

- produtividade.txt/.csv: indicadores historicos por camareira.
- gerencial_enxoval.txt/.csv: entregas por peca cadastrada, pecas com
  perda/avaria, ciclos medios, limite medio informado, vida consumida,
  vida esgotada e impacto financeiro estimado. Giro e a razao entregas
  fisicas registradas / pecas cadastradas, sem media historica de estoque
  nem taxa por periodo. Perdas/avarias e impacto financeiro consideram
  somente baixas confirmadas. Faltas e inspecoes ainda pendentes ficam
  separadas, sem perda financeira atribuida antes da avaliacao.
  Quantidades e impactos sao separados por hospede, lavanderia, equipe,
  desgaste natural e nao informado (historico sem responsabilidade cadastrada).
- perdas_por_responsavel.txt/.csv: detalhamento gerado junto ao gerencial,
  com ID/RFID, motivo, responsavel decidido, ultima UH/camareira e lote
  conhecidos, alem do impacto estimado. UH/lote sao contexto de rastreabilidade;
  nao determinam culpa automaticamente. Custos desconhecidos continuam vazios.
- painel_estoque.txt/.csv: estoque, uso, lavagem comum, manchas, relavagem,
  pendente e baixado, por tipo/tamanho. Rele os CSVs a cada geracao, sem
  atualizacao automatica. RFID mostra tags cadastradas/ausentes e declara
  infraestrutura NAO MONITORADA; nao simula disponibilidade de sensores.
  Inclui alerta de pecas nao retornadas e contagens de faltas de UH,
  faltas de lavanderia e acompanhamento ativo, sem duplicar a matriz de estados.
- lista_compras.txt/.csv: minimo planejado, utilizaveis, vida esgotada,
  sugestao original, quantidade ajustada pela governanta e estimativa.

Minimos sao configuraveis: dados/parametros_estoque.csv e criado somente ao
acessar gerar/configurar compras, com as colunas
id_tipologia,quantidade_uhs,numero_trocas. Referencia inicial: 80 King,
20 Dupla, tres trocas, ajustavel no menu. O calculo le as mesmas tipologias
e composicoes usadas na montagem dos kits.

Minimo = quantidade por kit x UHs planejadas x trocas.
Utilizaveis = estoque + uso + lavanderia com vida util restante.
Sugestao = max(0, minimo - utilizaveis).

Baixadas, pendentes, estados desconhecidos e esgotadas sao excluidos dos
utilizaveis. Esgotadas nao sao somadas outra vez ao deficit. Pecas sem
limite informado sao consideradas utilizaveis e identificadas no gerencial.
Ajuste manual modifica a lista, sem efetuar compra nem alterar o estoque.

Precos de referencia: ultima entrada_compra do tipo/tamanho, pela data/hora
e ID do movimento. Sem custo, TXT mostra "nao informado" e CSV deixa campo
vazio. Subtotais incluem apenas custos conhecidos e indicam quantidades
sem preco; nao inventam precos da base inicial nem equivalem a perdas
financeiras contabilizadas.

Matriz academica em src/relatorios.c: quantidades[128][7]. Linha representa
tipo/tamanho; coluna representa situacao. Dois lacos percorrem as dimensoes
para gerar painel e totais. Cadastros/historicos usam vetores de estruturas.

CHAMADOS
--------
- Novo: ID de UH existente, origem recepcao/hack, status aberto, data/hora
  atual e camareira obtida da distribuicao salva de hoje. Sem distribuicao
  valida para a UH, a abertura e recusada.
- Em aberto: lista e permite colocar o selecionado em andamento.
- Em andamento: lista e permite fechar o selecionado.
- Chamados fechados: consulta os concluidos sem alterar ou reabrir registros.
- Tela usa os rotulos pedidos; CSV preserva os valores SQL:
  aberto -> em_atendimento -> concluido.
- As transicoes preservam historico e nao alteram a distribuicao.
- Cada chamado possui somente a camareira responsavel, obrigatoria no SQL
  e obtida da distribuicao no C. chamado.csv possui seis colunas:
  id_chamado,id_uh,origem,status_chamado,data_abertura,id_camareira.

ORGANIZACAO DOS FONTES
---------------------
- include/projeto.h: estruturas e contratos publicos.
- include/interno.h: estruturas/utilitarios compartilhados pelos modulos.
- src/main.c e src/telas/: menus e interacao dos fluxos.
- src/csv.c: leitura dos cadastros, atribuicoes e historicos CSV.
- src/persistencia.c: escrita, normalizacao e confirmacao de arquivos.
- src/distribuicao.c: faltas, atribuicoes e realocacao de UHs.
- src/estoque.c: montagem e reserva de kits.
- src/estoque_operacoes.c: entradas/baixas, consulta e lavanderia.
- src/consumo.c: conclusao do atendimento, consumo e ocorrencias.
- src/indicadores.c: calculo/ordenacao da produtividade.
- src/relatorios.c: agregacoes, matriz e exportacoes gerenciais.
- src/chamados.c: abertura, listagem e transicoes de chamados.
- src/entrada.c: entrada validada, datas e aprovacao da supervisao.
- src/log_resultados.c: log explicito e backups de resultados.

Chamadas usam log_printf/log_puts/log_fprintf diretamente, sem redefinir
printf, scanf, puts ou fprintf por macros. Modulos possuem comentarios
antes das funcoes. funcoes.c deixou de concentrar responsabilidades.

COMPATIBILIDADE DOS DADOS E LIMITACOES
------------------------------------
- edredom_colcha consta no enum do SQL de referencia. Leitura trata o nome
  legado como compativel com capa_duvet; tamanho vazio de lencol/capa legado
  e reconhecido como viuvo. Novas entradas usam capa_duvet e tamanho
  explicito; SQL local ja aceita esses valores. CSVs antigos nao foram
  convertidos em massa nem houve troca de IDs/RFIDs.
- entrega_kit_uh.csv, config_camareiras.csv, faltas.csv,
  registro_ocorrencias.csv e parametros_estoque.csv sao auxiliares do
  prototipo, sem tabela identica no SQL original. A quarta coluna
  data_hora_fim do auxiliar de entregas e compativel com leitura anterior,
  sem criar consumo artificial para pecas faltantes.
- id_responsavel_entrega usa camareira destinataria; conferido permanece
  0. data_hora_entrega fica vazia antes do atendimento. Uma importacao
  futura para SQL precisara conciliar esses estados de trabalho com as
  restricoes e responsabilidades do banco.
- Operacoes agrupadas usam temporarios e rollback para falhas de I/O;
  nao equivalem a transacao duravel contra queda de energia nem suportam
  uso simultaneo por duas instancias do programa.
- Estoque suporta ate 10.000 pecas; produtividade mantem limite atual de
  500 atendimentos carregados. Rever antes de uso alem da demonstracao.
- Integracao RFID, banco conectado, contas individuais, compras efetivas
  e integracoes externas continuam como possibilidades futuras.

LOG E BACKUPS
-------------
- resultados/log_console.txt e o log oficial da sessao atual. A sessao
  anterior recebe backup antes de ser substituida; no encerramento normal,
  a atual tambem recebe copia em resultados/BKP/.
- Mensagens e entradas comuns sao registradas; senha da supervisao nao.
- Relatorios TXT/CSV recebem backups com data/hora e sufixos para impedir
  sobrescrita de copias geradas no mesmo segundo.
- Limpeza do console ocorre em terminal interativo e nao apaga o log.
- CSVs operacionais em dados/ sao a base persistida; backup de relatorio
  nao substitui copia integral da pasta antes de demonstracoes manuais.

VERSAO COM E SEM COMENTARIOS
---------------------------
A versao principal comentada e a fonte de manutencao. Para evitar duas
bases divergentes, gere automaticamente a versao sem comentarios:

powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\exportar_sem_comentarios.ps1

Destino padrao: pasta temporaria do usuario, informada pelo script.
Opcionalmente use -Destino com uma pasta fora deste projeto. A exportacao
copia o projeto e remove comentarios apenas dos fontes C e cabecalhos,
preservando strings e separacao entre tokens. Dados/documentos e versao
principal sao preservados. Compile a copia com o mesmo comando C11 antes
de entrega-la; mantenha futuras alteracoes na versao principal.
