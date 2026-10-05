# ADR 0052 — Resolver tipado de imports em dados

Status: implementado e validado no Xbox (sessão `134357125360003938-3028`).

## Contexto e decisão

A etapa anterior aplicou relativas em dados no Series S, mas deixou 275 slots
de Sonic e 648 de Deltarune dependentes de imports. Identificar apenas o NID
não basta: IDs locais apontam para declarações de biblioteca e módulo.

Implementar `ResolveGuestImports` portátil, independente de WinRT, Vulkan,
logger e callbacks HLE. Usar como referência primária `ModuleInfo`,
`LibraryInfo`, `Module::EncodeId` e `Linker::Resolve` do código upstream.
O manifesto opt-in conserva índices de símbolos e declarações packed.
Biblioteca usa versão nos bits 32–47; módulo minor 32–39 e major 40–47;
ambos usam ID nos bits 48–63. Extrair por shifts, não por bitfields host.

Normalizar `NID#library_id#module_id`. Validar o alfabeto
`ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+-`, representação
mínima e limite uint16. IDs duplicados, desconhecidos, formas inválidas e
índices repetidos/inválidos impedem todas as escritas do link em dados.

Lookup usa chave estruturada NID, nome/versão de biblioteca, nome/major/minor
de módulo e tipo ELF. Não misturar funções e objetos. Versões de módulo são
exatas, **mais estritas que a chave HLE upstream**, que não inclui major/minor.
Não incorporar os stubs de fallback upstream nem presumir ABI compatível.

## Registry e relocations

`GuestDataExport` contém um valor numérico opaco não zero, não um callback,
thunk ou ponteiro host executável. Máximo 4.096 entradas; chaves duplicadas
são erro, não first-wins. Limites anteriores permanecem: 131.072 símbolos,
8.192 imports, 512 declarações módulo/biblioteca e imagem de 16 MiB.

A fixture autoral raw/SELF cadastra dois exports numéricos de função/objeto.
Os matches alimentam R_X86_64_64 (S+A), GLOB_DAT e JUMP_SLOT (S). Todas as
escritas continuam sujeitas a overflow, targets/permissions lógicas, overlaps,
modo estrito e comparação exata de bytes não alvo da ADR 0051.
Unresolved mantém os slots intactos, inclusive weak; zero não é um stub válido.

O scanner de jogos não registra nenhum export. Portanto os 274 imports de
Sonic e 578 de Deltarune permanecem unresolved. Quantidade de imports não é
quantidade de relocations que dependem deles. Valores numéricos não certificam
backing, reserva de VA, permissões finais ou callability.

## Evidência e consequências

Testes raw/SELF cobrem todos os 65.536 IDs, versões/namespaces/tipos
incompatíveis, duplicatas, limites, símbolos, aplicação das três relocations
selecionadas e overflow sem commit. O oracle entra em `guest-loader-fixture`
sem aumentar linhas de Diagnostics. `phase5-imports.jsonl` e evento
`guest-imports` reutilizam o mesmo snapshot UWP, sem novas leituras USB.

O Deltarune local normaliza 578 imports com registry vazio e conserva 10.125
relativas, 648 slots pendentes e FNV pós-relocation `13996771465480706034`.
No Xbox, Sonic normalizou 274 imports e Deltarune 578, todos unresolved como
esperado; fingerprints e relocations foram preservados. Oracle do resolver,
25 probes, retomada e apresentação após resume passaram, sem erro de relatório.

`imports_resolved=0`, `runtime_exports_callable=0`, `loader_linked=0`,
`ready_for_boot=0`, `guest_executed=0` e `game_frame=0` permanecem, inclusive
quando a fixture vincula valores numéricos. Próximas entregas precisam de
exports HLE chamáveis seguros, filesystem/threads/TLS/controle Orbis, VA e
startup/faults/unwind guest gerais. Este bloco não conclui 5D nem tenta boot.
