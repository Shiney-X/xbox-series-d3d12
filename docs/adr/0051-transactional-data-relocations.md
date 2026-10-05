# ADR 0051 — Relocations relativas/local64 transacionais em dados

Status: implementado; validação desse estágio no Xbox pendente.

## Viabilidade e dependências

O manifesto da PR 50 passou nos dois títulos reais no Series S. O Deltarune
requer 10.125 RELATIVE, 571 JUMP_SLOT e 77 outras de símbolo. Antes de ligar
HLE, precisamos escrever valores relativos de forma determinística sem
confundir estágio parcial com um processo pronto para executar.

Depende de C++20, snapshot StorageFile, planner/payload e manifesto Orbis.
Não introduz libs externas. Fórmulas dos casos suportados seguem
`Core::Linker::Relocate`: B+A para RELATIVE, S+A para 64, S para
GLOB_DAT/JUMP_SLOT. Não integra ainda o linker desktop ou seu resolver.

## Decisão

`InspectGuestLinkManifest(file, true)` coleta registros limitados de símbolos
e relocations; o modo de inventário padrão não aloca esses registros.
`StageGuestDataLink` recebe apenas snapshot/bias/política, e internamente
revalida parser e staging. Não aceita plano mutável, callback HLE, endereço
externo ou imagem caller-supplied. Há uma única imagem por título.

Bias de diagnóstico padrão `0x100000000` é **numérico**, não endereço alocado
no Xbox nem ponteiro host. Plano começa em p_vaddr mínimo alinhado; B não é
esse mínimo. Slot na imagem é `r_offset - plan.base`, valor RELATIVE é B+A.
Verificar overflow também em B+plan.base+image_size. Canonicalidade/reserva
real de VA permanece uma dependência futura do runtime.

Aplicar apenas:

- R_X86_64_RELATIVE, símbolo zero já exigido pelo manifesto.
- R_X86_64_64, GLOB_DAT e JUMP_SLOT para STB_LOCAL, seção definida normal,
  tipos NOTYPE/OBJECT/FUNC e valor/tamanho inteiros dentro de LOAD/RELRO.
  GLOB_DAT/JUMP_SLOT ignoram addend conforme o caminho upstream.

SHN_UNDEF permanece `pending_import_relocations`; global/weak definido,
seções reservadas/ABS e tipos de símbolo fora do perfil permanecem
`pending_symbol_bindings`. Não resolver símbolos globais por coincidência de
endereço: regras de módulo/preempção ainda faltam. TLS e PC32/32/32S conhecidos
permanecem `pending_type_relocations`; NONE não escreve. Tipo desconhecido
falha antes de qualquer escrita (inclui IRELATIVE/IFUNC não implementado).

Addend RELA é lido como bits two's complement: somar/subtrair magnitude com
checks uint64, incluindo INT64_MIN; sem signed overflow ou conversão
implementation-defined uint64→int64. Não validar B+A como ponteiro para
objeto: é um valor numérico, não certificação de backing/callability.

## Transação, permissões e verificação

Planejar todas as escritas antes de commit. Targets conhecidos vêm do
manifesto, inteiramente dentro de um LOAD/RELRO; incluir ranges das escritas
pendentes na detecção de overlaps RELA/JMPREL. Rejeitar duplicatas e
interseções parciais. Writes selecionadas precisam de PF_W sem PF_X, ou
PT_SCE_RELRO não executável, que pode ser corrigido antes da proteção final.
Não alterar código RX, não usar RWX e não aplicar mprotect/VirtualProtect.

Modo parcial aplica apenas os casos selecionados e deixa slots pendentes
**intactos**, não zerados. `valid=true` significa estágio parcial passou.
Modo estrito (`require_complete=true`) falha sem escrever se existir qualquer
pendência. `complete=true` significa apenas todas as relocations desse
arquivo tratadas; mesmo isso não resolve imports sem relocation, dependências,
TLS, inicializadores ou boot.

Após commit, readback compara todos os patches. Verificação **exata**, não
por fingerprint: todos os outros bytes devem corresponder ao backing físico
do snapshot, ou zero de BSS/gaps/padding. Sem clonar uma segunda imagem.
FNV-1a64 pré/pós serve só para diagnóstico, não autenticação. Em erro de
validação não escrever; em erro de readback restaurar os slots selecionados.
Não executar nem reutilizar como instância uma imagem com erro.

## UWP e limites

Usar o mesmo snapshot autorizado e descartar a imagem após cada título.
Continuam 32 MiB por arquivo, 64 MiB/oito snapshots por scan e 16 MiB por
imagem. Registros adicionam memória temporária, bounded pelos 131.072 símbolos
e 262.144 relocations por tabela; buffers de leitura e vetores podem elevar
o consumo instantâneo além do tamanho da imagem. Não criar cache permanente.

`phase5-relocations.jsonl` e evento `guest-relocations` distinguem escritas
em dados de boot. Falha não oculta títulos/ícones. Report payload carrega
`payload_phase=pre_relocation`: BSS/FNV anteriores ao commit, pois BSS pode
receber ponteiros. Manifesto mantém `relocations_applied=0` por ser inventário.
Nenhum novo item em Diagnostics; acrescentar oracle ao probe de loader.

## Verificação e módulos restantes

Fixture raw/SELF autoral verifica valor RELATIVE, slots importados intactos,
modo estrito sem writes e local64. Testes exercitam addends positivos/negativos,
INT64_MIN, overflow, bias zero/alto, RELRO read-only, target RX/read-only, gaps,
overlaps/duplicatas, local fora da imagem, global pendente, TLS, NONE, tipo
desconhecido e truncamentos em todo comprimento.

Oracle read-only local no Deltarune: 10.125 relativas aplicadas, 648 slots
dependentes de imports, zero local64, bytes não alvo verificados. FNV pré
`5509961483789055787`, pós `13996771465480706034`. Não gravar arquivo ou
executar o jogo; usar isso como referência para o teste real no Series S.

`loader_linked=0`, `imports_resolved=0`, `ready_for_boot=0`,
`guest_executed=0`, `game_frame=0` permanecem. Próximas dependências: resolver
NID/ID/versão, HLE/módulos, relocations restantes, VA real/permissões finais,
startup, filesystem/controle, TLS Orbis e faults/unwind não leafs. A 5D não
termina por esse gate passar e não há promessa de primeiro frame.
