<!-- SPDX-FileCopyrightText: 2026 Shiney-X and xbox-series-d3d12 contributors -->
<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Fase 5 — Execução guest e caminho ao primeiro frame

## 5A: auditoria e preflight de executáveis (não é boot)

Instalar o MSIX desta etapa como **Game**, após os checks UWP verdes.
Manter Deltarune e/ou Sonic Mania na biblioteca USB existente. Em Games,
usar X RESCAN ou selecionar a pasta e aguardar a varredura.

Abaixo dos metadados do jogo, esperar um dos estados:

- `ELF HEADER OK NOT BOOTED`: cabeçalho ELF x86-64 reconhecido.
- `SELF HEADER OK NOT BOOTED`: cabeçalho SELF/ELF embutido reconhecido.
- `EBOOT HEADER UNSUPPORTED`: prefixo truncado/formato não suportado;
  verificar o motivo no relatório, sem concluir que o jogo é incompatível.
- `EBOOT READ FAILED`: falha de acesso ao arquivo; reportar HRESULT.

Enviar captura e `phase5-preflight.jsonl`, além de `phase0-lifecycle.jsonl`
e `phase0-results.jsonl` para regressão. O novo relatório é escrito em
LocalState a cada scan completo, com uma linha por jogo e um resumo de scan.
O resumo positivo significa varredura concluída, não todos os headers válidos.
Os logs antigos não são alterados pelo novo relatório.

Confere no máximo 16 KiB de cada `eboot.bin`, sem carregar o arquivo inteiro.
Relata formato, tipo, identidade Orbis, entry, quantidade de program headers
e flags de segmentos SELF. `header_valid=1` **não** prova integridade dos
segmentos, relocations, importações, descriptografia ou prontidão do loader.
`orbis_identity` só indica OSABI/ABI/tipo do header esperado pelo upstream.
`encrypted_segments=1` não faz descriptografia nem implica suporte.
Falha de inspeção não remove o jogo da biblioteca nem impede ícones/navegação.

Todos os registros mantêm `guest_executed=0`. Não há botão de boot nesta
build. O header inspector usa o layout documentado no loader upstream,
mas **não é o loader upstream ligado ao UWP**.

## Teste portátil do parser

```sh
cmake -S tests/guest_preflight -B out/guest-preflight -DCMAKE_BUILD_TYPE=Debug
cmake --build out/guest-preflight -j 2
ctest --test-dir out/guest-preflight --output-on-failure
```

Fixtures de cabeçalho ELF/SELF são autorais, não programas executáveis.
Testam leituras desalinhadas, limites, truncamentos e offsets extremos;
nunca são executadas como código. CI Windows também roda o teste.

## 5B: plano PT_LOAD e staging de dados (não é boot)

Instalar o novo MSIX como Game, depois dos três checks do workflow
`Xbox UWP shell` verdes: `portable-quality`, `build-and-test` e
`build-uwp-package`. Não é necessário recompilar manualmente no console.

1. Abrir o app: a fixture ELF autoral é carregada automaticamente em um buffer
   de dados de 32 KiB e liberada após a verificação. Não é código do PS4.
2. Em Games, usar X RESCAN com Sonic Mania e Deltarune na pasta.
3. Voltar ao Dev Home, reabrir e conferir navegação/ícones/Diagnostics.
4. Enviar `phase5-loader.jsonl`, `phase5-segments.jsonl`,
   `phase5-preflight.jsonl`, `phase0-results.jsonl` e `phase0-lifecycle.jsonl`.

No `phase5-loader.jsonl`, esperar `guest-loader-fixture` com `passed=true`,
`staged_bytes=32768`, `copy_bss_verified=1`, `host_permissions_applied=0`
e `guest_executed=0`. A verificação compara todos os bytes do buffer,
incluindo payloads, BSS e intervalos zerados. Uma falha aparece também nos
system probes. Não foi adicionada outra linha à tela Diagnostics.

`phase5-segments.jsonl` inspeciona a tabela de program headers dentro do mesmo
prefixo de 16 KiB. Relata ranges e flags PT_LOAD, entry file-backed executável,
overflow, alinhamento, sobreposição e orçamento de imagem de 16 MiB.
Esses limites são política restrita do protótipo, não limites do hardware
nem uma medida da memória necessária para rodar esses jogos.

Um `plan_valid=1` em SELF **não valida os blocos do container**: `p_offset`
é lógico ao ELF, não um offset diretamente copiável de `eboot.bin`.
`self_adapter_ready=0` e `payload_loaded=0` continuam explícitos.
Uma rejeição do plano não remove o título e não prova incompatibilidade.
O resumo `guest-segment-scan` só confirma que o scan terminou.

`StageRawGuest` só aceita payload ELF cru, recalcula o plano antes de alocar,
copia bytes e zera BSS. Valida flags guest e rejeita W+X, mas **não aplica
permissões de página, endereço virtual fixo ou relocations**. O buffer host
não é um address space guest e seu endereço não é o entry. Aplicação real
de permissões, guard pages, stack e boundary de execução permanece gate da 5C.
Não há carregamento/boot dos jogos, áudio nem entrada guest nesta build.

O teste portátil agora inclui `phase5.guest-loader`. Exercita truncamentos,
ranges extremos, W+X, alinhamento, entry inválido, BSS, limite de imagem,
rejeição de staging SELF e corrupção detectada pelo oracle de bytes.

## CI proporcional ao escopo

Mudanças exclusivamente nos caminhos Xbox já filtrados, incluindo
`tests/guest_preflight/**`, não disparam builds SDL desktop. Alterações em
código compartilhado continuam disparando-os. Branches de trabalho disparam
o workflow desktop via PR, não também via push; `main` continua coberta por
push e execução manual permanece disponível. O workflow Xbox mantém testes
Windows, pacote UWP e qualidade/licenças/testes portáteis com sanitizers.
Alterações no próprio workflow desktop ainda disparam os builds nesta PR.
Não altera regras de proteção de branch ou transforma checks antigos em passes.

## 5C inicial (PR 45): execução da fixture ELF autoral, não boot de jogo

Esperar os checks Windows/UWP/qualidade verdes, instalar o novo MSIX como
Game e abrir. O probe roda automaticamente em uma thread de teste dedicada.
Não selecionar jogo para executar: Games continua só inspecionando SELF.

Enviar `phase5-execution.jsonl`, `phase0-results.jsonl` e
`phase0-lifecycle.jsonl`. Conferir UI e Diagnostics, sair para Dev Home e
reabrir, depois fazer X RESCAN para regressão da biblioteca.

Esperado no registro `guest-execution-fixture`:

- `passed=true`, `win32_error=0`, `guest_executed=1` (somente a fixture).
- `abi=sysv_integer_leaf`, `arguments=19,23`, `return_value=42`.
- `alignment_red_zone_verified=1`, `host_permissions_verified=1`.
- `stack=windows_worker_thread`, `stack_verified=1`.
- `fault_recovered=1`, `fault_code=3221225477` (access violation esperada).
- `fault_filter_verified=1`: sete casos fora do escopo não são absorvidos.
- `fault_boundary=win64_leaf_seh`, `sysv_fault_unwind_supported=0`.
- `return_after_fault=42`, `allocations_released=1`.
- `game_executed=0`, `game_frame=0`, `loader_linked=0`.

A fixture é ELF cru autoral com instruções x86-64 de um leaf SysV. Usa o
staging 5B, depois copia a imagem para alocação virtual própria, sem fixar
seus endereços guest. Páginas de código e bridge passam RW→RX; dados RW;
gaps NOACCESS e extremos reservados/não comprometidos. Nunca usa RWX.
O thunk preserva RSI/RDI e XMM6..15, adapta dois argumentos inteiros e
verifica alinhamento de entrada e uso síncrono de 128 bytes de red zone.

A stack é a de uma thread Windows com reserva solicitada de 256 KiB e
limites verificados, não uma stack de processo Orbis montada pelo linker.
Não há troca manual de RSP da UI nem alteração de TEB. A proteção/expansão
da stack é gerenciada pelo Windows; o probe não simula stack overflow.

A segunda entrada autoral é uma **leaf Win64 separada**, chamada diretamente
por helper nativo com `__try/__except`. Lê um endereço reservado pertencente
ao probe. O filtro SEH trata exclusivamente esse RIP/read/endereço enquanto
armado no escopo da chamada. A leaf não altera RSP/não voláteis; não precisa
de tabela de unwind própria. Não passa pela bridge SysV. O SDK AppContainer
rejeitou Add/RemoveVectoredExceptionHandler no primeiro build; o caminho VEH
foi removido, sem redeclarar/importar APIs desktop à força.

Outras exceções continuam sua busca normal. **Recuperação de faults através
da bridge SysV não está implementada**, e não há handler geral de jogos.
O segundo retorno 42 comprova chamada SysV após recuperação da leaf Win64,
não recuperação de fault ocorrido no corpo SysV.

Não aceita paths, entrys ou instruções externas. A execução tem código fixo
sem loops e espera a thread terminar; não é um executor com timeout/cancelamento
para conteúdo não confiável. Mitigações proibitivas ou CFG ativo fazem o gate
falhar, sem desligar políticas. API não suportada também deve falhar o gate.

Ainda faltam Orbis main/startup, relocations/imports, adapter SELF, TLS/HLE,
chamadas guest→host e tratamento geral de faults, além de comandos gráficos
de jogo. Esse sucesso não certifica o ABI completo do PS4 ou um JIT do shadPS4.
O teste Linux do mesmo thunk valida bytes/aritmética no host, não substitui
o teste nativo Windows e AppContainer no Xbox.

## Complemento 5C: fault SysV através da bridge registrada

O perfil inicial acima foi validado no Xbox com 25 probes positivos,
retorno 42 e apresentação após retomada. O novo MSIX adiciona um gate:
unwind da bridge não leaf usando tabela dinâmica, sem VEH.

Esperar os três checks Xbox verdes, instalar como Game e abrir. Repetir
Dev Home/reabertura/navegação/X RESCAN. Enviar `phase5-execution.jsonl`,
`phase0-results.jsonl` e `phase0-lifecycle.jsonl`.

O relatório continua no mesmo arquivo, agora esperando:

- `passed=true`, `return_value=42`, `return_after_fault=42`.
- `fault_boundary=sysv_leaf_bridge_seh`.
- `win64_fault_recovered=1`, `sysv_fault_recovered=1`.
- `unwind_context_verified=1`: oracle de RSP/RIP, RSI/RDI e XMM6–15.
- `function_table_registered=1`, `function_table_removed=1`.
- `sysv_fault_unwind_supported=1` **somente para o perfil autoral testado**.
- `unwind_scope=authored_leaf_and_fixed_bridge`.
- Flags anteriores de proteções, filtros, stack e cleanup positivas.
- `game_executed=0`, `game_frame=0`, `loader_linked=0`.

`RtlAddFunctionTable` registra a tabela de uma única bridge. `RtlVirtualUnwind`
é testado com contexto sintético e canários antes de entrar no guest. O fault
SysV copia o guard para R10, clobbera RSI/RDI/XMM6 e lê o endereço reservado;
a leaf não modifica RSP. O OS trata a leaf e usa UNWIND_INFO da bridge para
chegar ao helper nativo SEH. A captura aborta essa chamada; não retoma a
instrução inválida nem corrige page faults de jogos.

O frame guest não leaf, a stack Orbis, unwind em prólogos/epílogos guest,
guest→host, TLS e exceções arbitrárias continuam fora do gate. Tabela e
metadados vivem na alocação própria até a remoção. Se remoção falhar, o probe
falha e mantém essa alocação residente, em vez de deixar o OS apontando para
memória liberada. Detalhes no ADR 0046.

## 5D inicial: round-trip HLE autoral

O complemento 5C passou no Series S: fault SysV/Win64, contexto de unwind,
remoção de tabela e soma 42; 25 probes e apresentação após retomada, sem alertas.

Instalar o novo MSIX como Game e abrir; o teste continua automático.
Voltar ao Dev Home/reabrir e testar navegação/X RESCAN. Enviar os mesmos três
arquivos: `phase5-execution.jsonl`, `phase0-results.jsonl`, `phase0-lifecycle.jsonl`.

Esperar todos os campos 5C positivos e também:

- `hle_scope=authored_integer_tailcall`, `orbis_hle_linked=0`.
- `hle_roundtrip_verified=1`, `hle_return=42`.
- `hle_unknown_rejected=1`, `hle_overflow_rejected=1`.
- `hle_unwind_verified=1`, `hle_table_removed=1`, `allocations_released=1`.
- `game_executed=0`, `game_frame=0`, `loader_linked=0`.

O guest autoral chama um serviço C++ nativo através de thunk SysV→Win64 e
retorna à bridge inicial. Não abre Sonic/Deltarune, não resolve imports Orbis,
não implementa filesystem/TLS/threads/scePad. Não esperar uma tela nova.
O oracle de unwind é sintético; não provoca fault dentro do callback HLE.
Detalhes e dependências no ADR 0047. Esse gate não conclui toda a 5D.

## 5D: duas workers e contexto host TLS

O round-trip inicial 5D passou no Series S, com 25 probes e retomada sem
alertas. O novo pacote executa o mesmo código em duas workers e adiciona
serviços de contexto autorais. Não adiciona linha em Diagnostics.

Instalar como Game, abrir, voltar ao Dev Home/reabrir e testar navegação.
Enviar `phase5-execution.jsonl`, `phase0-results.jsonl`, `phase0-lifecycle.jsonl`.
O teste continua automático e não precisa de jogos/sysmodules novos.

Esperado, além dos campos positivos anteriores:

- `worker_threads=2`, `workers_passed=1`, `thread_context_isolated=1`.
- `thread_rendezvous_verified=1` e IDs de worker distintos.
- `worker0_tls_value=100`, `worker1_tls_value=101`.
- `tls_scope=host_slot_not_orbis`, `tls_parent_isolated=1`.
- `tls_missing_context_rejected=1`, `tls_foreign_context_rejected=1`,
  `tls_invalid_key_rejected=1`.
- `tls_bindings_cleared=1`, `tls_slot_released=1`, `thread_handles_released=1`.
- `worker0_error=0`, `worker1_error=0`, `allocations_released=1`.
- `game_executed=0`, `game_frame=0`, `orbis_hle_linked=0`.

As duas workers gravam valores distintos por HLE antes da leitura, com
rendezvous pelo coordenador. Contextos não usam GS/TEB guest e não são TCB/DTV
Orbis. Não há pthreads guest, filesystem ou scePad nesse gate (ADR 0048).
A 5D permanece aberta; não confundir contexto host com TLS de um jogo.

## 5D: payloads reais em dados via USB

O gate threads/contexto da PR 48 passou no Xbox: duas workers, 100/101,
cleanup e 25 probes com retomada. Agora o pacote adiciona leitura/staging de
PT_LOAD reais por StorageFile; não executa entry nem retém a imagem para boot.

Instalar como Game e abrir. Entrar em Games com a pasta que já contém Sonic
Mania e Deltarune; usar X RESCAN e esperar terminar. Se necessário selecionar
a pasta novamente. Voltar ao Dev Home/reabrir e confirmar biblioteca/ícones.

Enviar `phase5-payload.jsonl`, `phase5-execution.jsonl`,
`phase0-results.jsonl` e `phase0-lifecycle.jsonl`.

Esperado por jogo compatível:

- `passed=true`, `stage=payload_data_staging`, `container=SELF`.
- `data_staged=1`, `payload_loaded=1`, `load_segments=2`; RELRO é contado
  separadamente em `relro_segments` (1 no Deltarune local).
- `image_bytes` e `copied_bytes` positivos; `bss_verified=1`.
- `source=uwp_storage_snapshot`, `image_retained=0`.
- `host_permissions_applied=0`, `guest_executed=0`, `game_frame=0`, `loader_linked=0`.

Para o eboot Deltarune testado localmente: imagem 12.222.464 bytes,
cópia 6.402.592 e BSS 3.214.776, incluindo PT_SCE_RELRO. Versão/update/dump diferente pode mudar esses
valores; não usar o fingerprint FNV como certificado de autenticidade.
Summary deve indicar ready=2 se apenas os dois jogos compatíveis estiverem lá.

UI pode mostrar SELF DATA OK NOT BOOTED. Erros ficam no relatório e não devem
ocultar títulos/ícones. Encryption/compression e perfis não suportados são
rejeitados, sem decriptação. Limites: 32 MiB/arquivo, 64 MiB/oito arquivos por
varredura e 16 MiB/imagem; bibliotecas maiores podem ter resultados skipped.
O relatório de segments/preflight continua metadata-only. ADR 0049 descreve
os limites e os módulos ainda pendentes para runtime/boot.

## 5D: manifesto de link read-only

O staging da PR 49 passou no Series S: Sonic e Deltarune com dois LOAD e
um RELRO, BSS verificado, ready=2. Sessão `134357076266563940-5268`:
25 probes, retomada e apresentação sem alertas. Nenhum entry foi chamado.

O próximo pacote inspeciona dependências, imports, relocations e PT_TLS.
Instalar como Game, entrar em Games e usar X RESCAN na mesma pasta USB.
Esperar a varredura terminar; voltar ao Dev Home e reabrir. Biblioteca e
ícones devem continuar disponíveis; a UI permanece NOT BOOTED.

Enviar `phase5-link.jsonl`, `phase5-payload.jsonl`, `phase5-execution.jsonl`,
`phase0-results.jsonl` e `phase0-lifecycle.jsonl`.

Esperado para o Deltarune CUSA15250 fornecido pelo usuário (oracle local):

- `passed=true`, `manifest_valid=1`, `source=uwp_storage_snapshot`.
- 580 símbolos; 571 funções e 7 objetos indefinidos.
- 10.773 relocations: 10.125 relativas, 571 jump slots, 77 de símbolos.
- `relocation_target_checks=10773`, `other_relocations=0`.
- Um PT_TLS vazio (`tls_file_bytes=0`, `tls_memory_bytes=0`); módulos
  dependentes ainda podem exigir TLS.
- `imports_resolved=0`, `relocations_applied=0`, `ready_for_boot=0`,
  `guest_executed=0`, `game_frame=0`.

Versão/update diferente pode mudar os números. Summary `valid=2` exige
manifestos positivos dos dois títulos; um relatório vazio não passa.
No startup, `guest-loader-fixture` também exige
`link_manifest_fixture_verified=1`, sem nova linha em Diagnostics.

Se falhar, enviar o erro, não remover o título ou copiar sysmodules às cegas.
O parser não resolve IDs/versões dos módulos, não aplica relocations, não
executa inicializadores e não implementa TCB/DTV/FS/GS Orbis. Tipos desconhecidos
são contados, mas seus targets não são validados. ADR 0050 delimita o gate;
5D continua aberta e o pacote ainda não tenta boot de Deltarune.

## 5D: relocations transacionais em dados

O manifesto da PR 50 passou no Xbox: dois títulos válidos, 25 probes com
retomada/apresentação sem alertas na sessão `134357092219333946-5156`.

O novo pacote aplica RELATIVE e local64 suportadas em uma imagem de dados
com bias numérico de 4 GiB. Não é reserva de endereço guest/host nem boot.
Imports externos não são substituídos por stubs ou zero.

Instalar como Game; em Games usar X RESCAN na mesma pasta USB. Esperar a
varredura, voltar ao Dev Home/reabrir e confirmar títulos/ícones e Diagnostics.
Não é necessário modificar o pendrive ou adicionar sysmodules para esse gate.

Enviar `phase5-relocations.jsonl` (novo), `phase5-link.jsonl`,
`phase5-payload.jsonl`, `phase5-execution.jsonl`, `phase0-results.jsonl` e
`phase0-lifecycle.jsonl`.

Esperado no Deltarune CUSA15250 testado localmente:

- `passed=true`, `data_link_valid=1`, `relative_applied=10125`.
- `local_symbol_applied=0`, `pending_import_relocations=648`.
- `pending_symbol_bindings=0`, `pending_type_relocations=0`.
- `writes_verified=1`, `untouched_bytes_verified=1`.
- `all_relocations_applied=0`, `imports_resolved=0`, `ready_for_boot=0`.
- `address_scope=synthetic_load_bias`, `load_bias=4294967296`.
- FNV diagnóstico antes `5509961483789055787`, depois `13996771465480706034`.
- `guest_executed=0`, `game_frame=0`, `host_permissions_applied=0`,
  `runtime_instance_created=0`, `image_retained=0`.

Contagens podem mudar com versão/update. Sonic tem 1.150 relativas no
manifesto validado; confirmar aplicação no Xbox, sem presumir sucesso só
pela contagem. Summary valid=2 significa **estágio parcial de dados** passou
nos dois títulos, não que os jogos estejam linkados ou prontos para boot.

`guest-loader-fixture` exige `data_relocation_fixture_verified=1`, sem nova
linha em Diagnostics. `phase5-payload.jsonl` continua diagnosticando o estado
**anterior** às escritas (`payload_phase=pre_relocation`); BSS pode receber
ponteiros depois. O manifesto continua read-only, então seu campo
`relocations_applied=0` não contradiz o novo relatório em dados.

Gates inválidos (overflow, target RX/read-only não RELRO, sobreposição,
tipo desconhecido) devem mostrar erro e zero patches aplicados. Tipos
conhecidos pendentes e imports são explicitados, sem execução. ADR 0051;
5D permanece aberta, aguardando resolver NID/módulos/HLE e runtime Orbis.

### Evidência Xbox do link de dados anterior

Sessão `134357105319283937-1800`: Sonic aplicou 1.150 relativas e deixou
275 slots importados pendentes; Deltarune aplicou 10.125 e deixou 648.
Ambos tiveram `writes_verified=1`, `untouched_bytes_verified=1`, zero local64
e zero pendência de tipo/binding. FNV pós: Sonic `13228043239366263461`,
Deltarune `13996771465480706034`. Os 25 probes e a retomada passaram.
Isso valida o gate da ADR 0051, não boot ou resolução runtime.

## Teste 5D — namespace e registry numérico de imports

Instalar o novo pacote como Game, abrir Games e usar X RESCAN. Voltar ao
Dev Home/reabrir e repetir a varredura. A interface permanece NOT BOOTED;
não se espera uma nova imagem ou execução de Deltarune neste bloco.

Enviar `phase5-imports.jsonl` (novo), `phase5-relocations.jsonl`,
`phase5-link.jsonl`, `phase5-payload.jsonl`, `phase5-execution.jsonl`,
`phase0-results.jsonl` e `phase0-lifecycle.jsonl`.

Esperado: namespace válido nos dois títulos, registry de jogos vazio,
`registered_data_exports=0`, `numeric_imports_matched=0`,
`unresolved_imports=274` em Sonic e `578` em Deltarune. Versões/update podem
alterar contagens. `binding_keys` lista as chaves normalizadas; unresolved
é esperado e não é falha de namespace. Não equivale a imports resolvidos.

O probe existente `guest-loader-fixture` deve mostrar
`import_resolver_fixture_verified=1`, sem adicionar linha a Diagnostics.
Somente essa fixture cadastra exports numéricos data-only. Relocations/FNV
dos jogos devem continuar iguais à evidência anterior, com
`data_import_relocations_applied=0`. Confirmar evento `guest-imports` com
`report_error=0`, probes e apresentação após retomada.

`imports_resolved=0`, `runtime_exports_callable=0`, `ready_for_boot=0`,
`guest_executed=0` e `game_frame=0` são obrigatórios. ADR 0052;
Este gate foi validado no Xbox na sessão `134357125360003938-3028`: 25 probes,
oracle de imports, namespaces dos dois títulos, fingerprints e retomada
passaram. Os eventos `guest-imports` registraram `valid=2;report_error=0`
antes e após resume. A 5D permanece aberta.

## Teste 5D — chamada autoral via import resolvido

Instalar o novo pacote como Game e abrir Diagnostics: o teste é automático
na inicialização, sem conteúdo adicional no USB. Voltar ao Dev Home/reabrir
e confirmar apresentação normal. Em Games usar X RESCAN para conferir que
a inspeção dos títulos continua sem regressão.

Enviar os mesmos sete arquivos do bloco anterior, especialmente
`phase5-execution.jsonl`, `phase0-results.jsonl` e `phase0-lifecycle.jsonl`.
Não esperar uma nova tela de jogo ou aumento das linhas em Diagnostics.

Esperado no probe de execução: `passed=true`, retorno 42, workers/contextos
100/101 e cleanup/unwind positivos. Novos campos agregados das duas workers
raw/SELF devem valer 1: `import_link_verified`, `import_slot_verified`,
`import_relative_verified`, `import_call_verified`, `import_missing_rejected`,
`import_version_rejected`, `import_type_rejected`.

Exigir `import_execution_scope=closed_authored_raw_self`,
`import_binding_source=typed_resolver_jump_slot`, `import_manual_patch=0`,
`game_runtime_exports_callable=0`, `game_executed=0`, `game_frame=0`.
A fixture executa o destino autoral via resolver/relocation; jogos continuam
com registry vazio, mesmas relativas/fingerprints e imports unresolved.
ABI/serviço/corpus fechados não certificam runtime geral ou HLE Orbis.
ADR 0053: validado na sessão `134357133173433935-3916`, sete novos gates em 1,
retorno 42, workers 100/101, cleanup/unwind, 25 probes e apresentação após
retomada. Inspeção dos jogos permaneceu idêntica e sem erro de relatório.

## Teste 5D — três serviços Orbis de tempo

Instalar como Game, abrir Diagnostics, voltar ao Dev Home/reabrir e confirmar
apresentação. Em Games usar X RESCAN para checar regressões. Enviar os mesmos
sete logs, especialmente `phase5-execution.jsonl`, resultados e lifecycle.
Não é necessário adicionar conteúdo no USB; as fixtures são internas.

O novo gate executa NIDs reais de `sceKernelGetProcessTime`,
`sceKernelGetProcessTimeCounter` e `sceKernelGetProcessTimeCounterFrequency`
em código autoral ELF/SELF. Exigir `kernel_clock_services=3`, frequency > 0 e
`kernel_clock_calls_verified=1`, `kernel_clock_units_verified=1`,
`kernel_clock_unwind_cleanup_verified=1`. O backend é `qpc_virtual`;
scope `closed_authored_raw_self`. Os 25 probes anteriores continuam necessários.

`kernel_clock_direct_rdtsc_compatible=0`, `game_clock_exports_registered=0`,
`game_executed=0`, `game_frame=0`. Jogos mantêm registry vazio, mesmas relativas
e fingerprints/imports pendentes. Serviço Orbis real não equivale a boot ou
integração do runtime completo. Política temporal de suspensão não é validada
pelo resume gráfico. ADR 0054; validado na sessão `134357143372603935-5336`:
três serviços completos, 25 probes e apresentação após retomada passaram.
Inspeção dos jogos manteve fingerprints/imports, sem regressão. A captura
não inclui varredura após resume nem certifica política temporal de pause.

## Teste 5D — ponteiros guest e serviços libc de memória

Instalar o novo MSIX como Game. Abrir Diagnostics (teste automático), voltar
ao Dev Home e reabrir. Conferir apresentação e, em Games, usar X RESCAN após
a retomada. Não adicionar arquivos nem modificar o pendrive. A UI não ganha
uma nova linha: o gate fica no probe de execução existente.

Enviar `phase5-execution.jsonl`, `phase0-results.jsonl`,
`phase0-lifecycle.jsonl`, `phase5-imports.jsonl`, `phase5-relocations.jsonl`,
`phase5-link.jsonl` e `phase5-payload.jsonl`.

Esperado: `guest_memory_scope=closed_authored_raw_self`, services=3,
calls=34, rejected_calls=16, stage `complete` e todos os campos
`guest_memory_calls_verified`, `guest_memory_ranges_verified`,
`guest_memory_rejections_verified`, `guest_memory_unwind_cleanup_verified`
em 1. As 16 rejeições são **intencionais**, não falhas de execução: ponteiro
host, gap NOACCESS, limite lógico, RX, overflow, tamanho excessivo e overlap.
Cada chamada verifica retorno e bytes exatos do LOAD de dados/código.

Os 25 probes e o gate anterior de clock continuam necessários. Serviços reais
de memcpy/memset/memcmp são chamados apenas pelas fixtures autorais raw/SELF;
`game_memory_exports_registered=0`, `guest_memory_general_faults_supported=0`,
`game_executed=0`, `game_frame=0` permanecem. Os jogos mantêm registry vazio,
mesmas relativas/FNV e imports pendentes. Não é heap/MMU, startup Orbis,
filesystem ou boot de Deltarune. ADR 0055; validado na sessão
`134357153616663927-6448`: 34 chamadas/16 rejeições, 25 probes e varredura
após retomada sem regressão. Esse gate fechado não certifica runtime geral.

## Teste 5D — sessão de preparação do executável selecionado

Instalar como Game. Abrir Diagnostics e conferir o probe de execução existente:
`startup_preparation_fixture_verified=1`; sem nova linha no painel.

1. Em Games selecionar **Deltarune** e pressionar **A PREPARE**. Esperar
   `STARTUP BLOCKED SEE LOG`; não esperar tela de jogo.
2. Pressionar B para voltar às pastas, reentrar/selecionar a biblioteca,
   selecionar Sonic Mania e repetir A PREPARE.
3. Com uma sessão preparada, voltar ao Dev Home e reabrir. Confirmar shell,
   reescanear e preparar Deltarune novamente. Não precisa reinstalar.
4. Opcional: B durante a leitura deve cancelar sem fechar o aplicativo.

Enviar **`phase5-startup.jsonl` (novo)** e os sete logs anteriores.

Nas linhas dos títulos: preparation_passed, mapped_readback_verified,
entry_stack_tcb_dtv_prepared e data_only_protections_verified em 1;
address_scope `actual_owned_mapping`, bias não sintético, image_retained=1,
blocker `unresolved_imports`. Os dumps recebidos têm 274/578 imports e
1150/10125 relativas (Sonic/Deltarune); update pode alterar contagens.
O checksum do relatório antigo continua usando bias sintético, portanto não
comparar bytes de relativas da nova sessão com os fingerprints antigos.

Nas linhas `-release`: cleanup_verified=1, image_retained=0,
release_reason `back`, `next_attempt`, `rescan` ou `suspend`. O journal deve
registrar guest-startup sem erro de relatório, além de retomada/apresentação.
O relatório mantém até 16 eventos desta execução, não um histórico ilimitado.

Obrigatórios: upstream_startup_layout_shared=1,
upstream_linker_execute_integrated=0, tls_main_module_only=1, tls_bound=0,
entry_boundary_ready=0, ready_for_entry=0, game_code_executable=0,
guest_entry_called=0, game_executed=0, game_frame=0. Preparação válida com
imports ausentes **não é boot bem-sucedido**. Este bloco ainda é 5D, não 5E.
Falha de leitura/mapeamento deve aparecer separada do blocker de imports.
ADR 0056; validação física pendente.

## Gates seguintes (estimativa, não garantia de primeiro frame)

| Bloco | Entrega/gate | Teste no console |
| --- | --- | --- |
| 5A | Auditoria e identificação read-only ELF/SELF | Inspecionar eboot real |
| 5B | Loader mínimo controlado, ranges/PT_LOAD/permissões e limites | Carregar fixture guest sem executá-la |
| 5C | ABI SysV, entrada/retorno, stack e diagnóstico de exceções | Executar código guest autoral com resultado conhecido |
| 5D | HLE/threads/TLS/filesystem e entrada de controle guest mínima | Fixture com chamada HLE e leitura de controle |
| 5E | Tentativa de boot Deltarune, imports e bloqueios documentados | Executar até o primeiro bloqueio identificável |
| 5F | PM4, shaders/recursos reais exigidos pela primeira tela | Tentativa de renderização real |
| 5G | Primeiro frame reproduzível e regressões | Captura de imagem produzida pelo jogo |

Dependências podem exigir juntar blocos ou adicionar correções; só concluir
gates com evidência. Antecipar tentativas de Deltarune se a execução ficar
pronta antes; não chamar inspeção/carregamento de boot. HLE mínimo pode
não bastar para esse jogo. Teste autoral isola falhas; Deltarune mede o avanço
real. Não redistribuir binários comerciais/sysmodules nem incluir dumps no Git.

Controlar a UI UWP já funciona; gamepad guest é outro caminho. A Fase 5
inclui seu suporte básico, conforme necessidade para avançar telas do jogo.
As extensões de shaders/resources da Fase 4 continuam gates explícitos.
