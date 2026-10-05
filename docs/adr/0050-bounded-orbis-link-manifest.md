# ADR 0050 — Inventário Orbis de linkedição no snapshot USB

Status: implementado e validado no Series S; não certifica link/boot.

Evidência da PR 50: sessão `134357092219333946-5156`, 25 probes positivos,
retomada/apresentação sem alertas. Dois manifestos válidos: Sonic com 267
funções e 7 objetos indefinidos, 1.425 relocations; Deltarune com 571 funções,
7 objetos e 10.773 relocations, coincidindo com o oracle local. Staging,
contexto TLS/threads e cleanup também positivos; nenhum jogo executado.

## Viabilidade e dependências

O staging real da PR 49 passou no Series S, mas ainda não fornece imports,
relocations ou inicialização Orbis. O gate seguinte precisa identificar esses
requisitos antes de ligar HLE ou executar entry. `Module::LoadDynamicInfo`
no upstream documenta tags SCE cujos offsets são relativos a DYNLIBDATA.
O parser UWP é separado: não declara integração do linker desktop.

Depende apenas de C++20, preflight/planner existentes e do snapshot
StorageFile já autorizado. Sem novos pacotes ou sysmodules no repositório.

## Decisão e implementação

Separar `PlanGuestPayloadFile` do copy/BSS/hash. Ele revalida internamente
ELF/SELF e fornece ranges físicos/lógicos sem alocar a imagem. SELF relaciona
IsBlocked/GetId ao índice original do PH; verifica tamanho igual a p_filesz,
overflow e overlaps lógicos além dos checks físicos da PR 49. Um PT_DYNAMIC
pode estar dentro do bloco PT_SCE_DYNLIBDATA, sem bloco próprio.

`InspectGuestLinkManifest` aceita somente identidade Orbis e extrai:

- PT_DYNAMIC e PT_SCE_DYNLIBDATA únicos, DT_NULL obrigatório.
- DT_NEEDED, DT_SCE_NEEDED_MODULE e DT_SCE_IMPORT_LIB: nomes diagnósticos.
- Símbolos global/weak SHN_UNDEF: strings completas, incluindo NID#lib#module.
- RELA e JMPREL: contagens por tipo, índice de símbolo em range, RELATIVE
  com símbolo zero e target inteiro dentro de um LOAD/RELRO quando a largura
  é conhecida. Addends não são avaliados/aplicados.
- PT_TLS único: tamanho, alinhamento e backing/range do template; sem TCB.

Limites além dos existentes 32 MiB/snapshot e 16 MiB/imagem: dynamic 64 KiB,
DYNLIBDATA 8 MiB, strtab 4 MiB, 131.072 símbolos, 8.192 imports, 512 nomes de
dependência, 262.144 relocations por tabela. Nome ASCII imprimível até 256
bytes, excluindo `;` e `,`; orçamento agregado 1 MiB. Rejeitar tabelas
truncadas, singleton tags duplicadas, entry sizes incompatíveis e offsets
fora de range, inclusive tabela vazia com offset inválido.

Não validar todos os símbolos definidos, hash table ou tags de startup.
Tipos de relocation desconhecidos são contados em `other_relocations`, sem
target check. `manifest_valid=1` certifica apenas esse perfil limitado,
não suporte à aplicação de todas as relocations ou boot. Nomes de módulo/lib
não incluem a decodificação de IDs/versões packed; essa relação precisa ser
revalidada no futuro resolver. A lista de imports não prova quais serão
chamados antes do primeiro frame.

## UWP e fallback

Inspecionar o mesmo snapshot após payload positivo, sem segunda imagem nem
segunda leitura USB. `phase5-link.jsonl` registra resultado por título;
payload não pronto ou erro de inspeção gera falha explícita, não oculta
metadados/ícones. Summary exige pelo menos um título e todos positivos.
Evento `guest-link` registra erro de escrita. Manter NOT BOOTED na UI.

Todos os caminhos declaram `guest_executed=0`, `ready_for_boot=0`.
Sucesso declara `imports_resolved=0`, `relocations_applied=0` e
`loader_linked=0`: nenhum código comercial é chamado, nenhuma imagem recebe
permissão executável. Relatórios podem incluir nomes/NIDs, nunca bytes do jogo.

## Testes e consequência

Fixture raw/SELF autoral com blocos reordenados, PT_DYNAMIC como subrange,
dois imports, RELATIVE/JUMP_SLOT e PT_TLS BSS-only. Oracle independente e
testes de truncamento em todo tamanho de arquivo, nomes/offsets inválidos,
duplicatas, ranges/gaps de targets, alinhamento TLS e formatos de tabelas.
Startup UWP incorpora oracle ao probe existente, sem ampliar Diagnostics.

Oracle local no Deltarune do usuário: 580 símbolos, 571 funções e 7 objetos
indefinidos; 10.773 relocations (10.125 relativas, 571 jump slots, 77 símbolos),
todos targets conhecidos em range. PT_TLS principal vazio. Isso não dispensa
TLS dos módulos dependentes nem teste no Xbox. Procedimento no documento
PHASE5_VALIDATION; 5D permanece aberta.

Próximas dependências continuam reais: resolver NID/ID/versão contra módulos
e HLE, aplicar relocations com overflow/permissões, startup/init arrays,
filesystem/controle guest, TLS Orbis e faults/unwind não leafs. Não usar
stubs que retornam sucesso silencioso para avançar boot artificialmente.
