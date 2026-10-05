# ADR 0043 — Auditoria de execução guest e preflight UWP read-only

- Status: auditoria inicial e inspeção implementadas; teste Xbox pendente
- Data: 2026-10-03

## Auditoria do baseline

| Componente real | Evidência no código | Bloqueio/ação UWP |
| --- | --- | --- |
| Loader ELF/SELF | `src/core/loader/elf.cpp`, `elf.h`: IFile, SELF e tabelas ELF | Injetar acesso StorageFile/IFile e validar ranges antes de mapear; prefixo não substitui loader |
| Module/relocations | `src/core/module.cpp`: LoadModuleToMemory, LoadDynamicInfo, LoadSymbols | Limites/mapeamento, imports/NIDs e relocations; não certificar pelo SFO |
| Entrada guest | `src/core/linker.cpp`: RunMainEntry, assembly GNU, RDI/RSI e jump sem retorno | MSVC atual não suporta esse código/ABI; validar thunk/toolchain explicitamente, não apagar SysV |
| ABI | `src/common/types.h`: PS4_SYSV_ABI=sysv_abi; presets desktop usam clang-cl | Clang-cl desktop não prova AppContainer; manter host MSVC até validar boundary guest |
| Memória | `src/core/address_space.cpp`: RtlGetVersion via ntdll, VirtualAlloc2/CreateFileMapping2/MapViewOfFile3 | Substituir acesso desktop não permitido; auditar APIs e ranges no pacote real, orçamento UWP diferente de backing PS4 |
| TLS/CPU patches | `src/core/tls.cpp`, `cpu_patches.cpp`: TlsAlloc/SetValue, GS, Zydis/Xbyak | Separar TLS host de TCB guest e testar patches; compartilhar arquitetura x86-64 não basta |
| Exceções | `src/common/signal_context.cpp`: AddVectoredExceptionHandler | Validar AppContainer e contexto/red-zone, sem engolir faults como sucesso |
| HLE/bootstrap | `src/core/linker.cpp`, `src/emulator.cpp`: settings, filesystem, kernel/sysmodules e inicialização desktop | Não chamar Emulator desktop no shell; montar bootstrap UWP com dependências explícitas |
| GPU/input | Host tem D3D12 e gamepad da UI | Não conectado ainda a PM4 ou scePad guest; implementar ambos conforme o primeiro caminho do jogo |

Essa é uma auditoria por leitura, não prova de que cada API acima funciona
ou é proibida no build Xbox atual. Os probes de memória anteriores provam
operações específicas, não os ranges, backing e integrações completos do core.
Não classificar a CPU upstream apenas como JIT de x86-64 para x86-64: o
caminho nativo e patches/ABI/TLS precisam ser preservados e portados.

## Decisão e implementação 5A

Primeiro realizar preflight real, read-only, de `eboot.bin` pelo StorageFile
já obtido no navegador USB. Ler até 16 KiB por arquivo, em coroutine; não
mapear executable memory, não chamar Linker::Execute, não descriptografar.
Inspector C++20 sem Vulkan, SysV ou singleton desktop; leitura explícita
little-endian sem reinterpret_cast/struct desalinhada. SELF usa offsets
32 + segment_count × 32 para ELF embutido, como o loader upstream.

Checar magic, identidade SELF suportada, ELF64 LE x86-64, tipos iniciais,
e_ehsize/e_phentsize/e_phnum e bounds da tabela no tamanho informado do
arquivo, sem adição overflow. Registrar campos, não afirmar validade dos
program headers ainda não lidos. Prefixo SELF fora do limite tem erro
explícito; não aumentar alocação com base em tamanho declarado pelo arquivo.

Expor status em Games e relatório `phase5-preflight.jsonl`; falha de
preflight mantém o jogo visível. `loader_linked=0`, `guest_executed=0`,
`game_frame=0` ficam explícitos. O componente novo não substitui o loader
upstream, nem é um stub que retorna boot bem-sucedido.

## Fixture guest futura e teste de jogo

Construir executável autoral mínimo para a próxima sequência: PT_LOAD
controlados, sem imports/TLS inicialmente, código SysV que retorna/escreve
valor conhecido. Separar teste de função guest do entry point Orbis que
usa stack/bootstrap diferente e não retorna como função Win64 comum.
Adicionar chamada HLE e TLS/threads depois de validar o boundary.

Usar Deltarune como teste de integração quando houver execução guest;
seu resultado no Linux é referência de compatibilidade daquele host,
não prova no Xbox. Não ligar execução de binários arbitrários ao scan.
Primeiro frame só pode ser alegado depois de executar guest e consumir
seus comandos/shaders/recursos reais, não pelo preview sintético do host.

## Verificação

Parser portátil e CI Windows: fixtures autorais, truncamento, arquitetura,
identidade, offsets extremos, SELF com flags e limite do prefixo. UWP:
build e inspeção de arquivos reais no Xbox, com regressão da biblioteca,
ícones, B, Diagnostics e lifecycle. Validação de loader/ABI fica nos gates
seguintes, conforme PHASE5_VALIDATION.
