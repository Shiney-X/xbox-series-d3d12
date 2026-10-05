# ADR 0056 — Sessão possuída de preparação para entrada Orbis

Status: implementação inicial; gates CI/hardware pendentes.

## Decisão e dependências

Concentrar num único bloco mapeamento real, dados de entrada/stack/TLS,
integração da ação por título, diagnóstico de imports e cleanup/lifecycle.
Não adicionar outro serviço HLE isolado. A sessão é preparação do boot,
**não uma tentativa de executar o entry**, e não conclui a 5D ou inicia 5E.

Referências primárias locais: `Linker::Execute`/`RunMainEntry` em
`src/core/linker.cpp`, `AllocateTlsForThread` e `threads/tcb.cpp`. O upstream
entra por jump com RDI=params, RSI=exit, stack misaligned (%16=8), dois qwords
de EntryParams no topo. Não substituir isso pela leaf retornável dos testes.
O runtime também depende de MMU, pthreads, libc/init e TLS, não só do arquivo.

Extrair os layouts existentes EntryParams/TCB/DTV para `core/runtime_layout.h`,
usado pelo upstream **e** UWP com asserts ABI. Não copiar ou alterar a semântica
do desktop; não chamar essa extração de integração de Linker::Execute.

## Implementação

`GuestStartupSession` reserva backing possuído com guards e relinka o snapshot
com bias da alocação real. Verifica cópia completa antes das proteções. Páginas
X guest tornam-se READONLY na preparação, não RX; outras são RW/RO/NOACCESS.
Rejeitar W+X também após arredondamento a páginas host de 4 KiB. RELRO em
página compartilhada RW ainda não é selado para entrada; só preparação.
O entry é endereço calculado, nunca chamado e sem método Execute público.

Stack de 128 KiB com guards, argc=1, argv0=/app0/eboot.bin, argv[1]=null,
EntryParams e cópia dos seus dois qwords em stack_top-24. Preparar dados
não certifica troca de stack, jump/noreturn ou unwind de funções gerais.

Preparar TLS principal apenas: até 16 MiB, align <=32, init image/BSS,
TCB após espaço alinhado a 32 e 64 bytes reservados, DTV de três entradas
(geração=1, max_index=1, módulo 1). Não é alocador/TCB constructor upstream:
thread/fiber/canary ficam nulos, FS/GS não muda, slot host não é vinculado e
TLS das dependências permanece pendente. Alinhamento maior falha, não é
silenciosamente reduzido. Verificar bytes iniciais/BSS e pointers por readback.

Registry de jogo permanece vazio. Relativas aplicadas em backing real não
resolvem imports. Informar contagens e primeiras oito chaves; imports são
primeiro bloqueio quando presentes, mas entry_boundary permanece sempre
bloqueado independentemente da contagem. Não prometer 5E após apenas preencher
registry ou ocultar faults gerais com stubs de sucesso.

## UI, storage e posse

A PREPARE usa StorageFile obtido pela varredura na pasta USB autorizada,
nunca abre caminho host arbitrário. Snapshot até 32 MiB, leitura completa,
size antes/depois e prefixo comparado; não é autenticação do arquivo nem
detecção completa de mudanças de mesmo tamanho fora do prefixo. A imagem
é limitada a 16 MiB; temporários e backing são limitados, mas não constitui
um budget completo de RAM/residência do processo.

Operação serial impede mudar seleção durante leitura. B cancela por geração;
suspensão invalida generation, e completion antiga retorna antes de mapear.
Próxima tentativa/back/rescan/suspend liberam imagem/stack/TLS, sem worker
guest a terminar. Em falha de VirtualFree preservar ownership e impedir
substituição por uma nova sessão. Destrutor tenta liberar backing restante.

Relatório `phase5-startup.jsonl` preserva até 16 eventos na execução corrente,
incluindo tentativas por título e releases, com session id; writer continua
síncrono conforme os outros relatórios. `passed` nas linhas de título mede
preparação, não boot. Telemetria separa blocker de imports, erro nativo,
image_retained/cleanup e guest_entry_called=0. O teste automático fica no
probe existente para não aumentar o painel Diagnostics.

## Gates

Portátil: layouts compartilhados/stack seed, argv, TLS init/BSS/TCB/DTV,
inputs inválidos sem writes e page-rounded W/X rejeitado. Nativo: mesma
classe usada pela UI prepara raw/SELF, bloqueia imports e libera recursos;
W+X rejeitado. Hardware: selecionar Deltarune/Sonic, preparar, voltar,
preparar novamente, suspender/retomar e reenviar relatório.

Antes da 5E ainda faltam: boundary de entrada Orbis/stack/faults gerais,
ligação do CPU runtime e MMU (incluindo seu modelo de endereços fixos),
TLS/thread binding e inicialização das dependências, registry/serviços
necessários ao startup. Estas são as prioridades seguintes, não todo HLE
do emulador. Não redistribuir binários comerciais ou módulos de firmware.
