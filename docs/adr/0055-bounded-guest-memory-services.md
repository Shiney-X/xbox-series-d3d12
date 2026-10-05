# ADR 0055 — Fronteira de ponteiros e subset libc de memória

Status: implementado e validado no Series S no corpus autoral fechado.

## Viabilidade, dependências e decisão

Serviços HLE com ponteiros precisam autorizar endereço, tamanho e acesso
antes de usar memória nativa. Não passar ponteiros arbitrários diretamente
ao CRT nem tratar página RW arredondada como autorização de todo seu conteúdo.
Dependências: staging/link estritos raw/SELF, alocações possuídas pelo host,
bridge Win64/SysV, unwind dinâmico e contextos confiáveis. Não requer novos
sysmodules, dumps comerciais, Vulkan ou bibliotecas desktop no UWP.

Implementar três APIs reais do subset `libSceLibcInternal` com assinaturas
inteiras de três argumentos. Referência primária: implementação e registros
em `src/core/libraries/libc_internal/libc_internal_memory.cpp` e cabeçalho
correspondente. Não confundir essas funções com APIs libkernel de VM.

| API | NID upstream | Resultado no caso válido |
| --- | --- | --- |
| memcpy | Q3VBxCXhUHs | Ponteiro de destino; cópia exata |
| memset | 8zTFvBIAIN8 | Ponteiro de destino; valor convertido em byte |
| memcmp | DfivPArhucg | s32 negativo/zero/positivo; só o sinal é especificado |

O registry fechado usa biblioteca versão 1, módulo 1.1 e símbolos função.
Não cadastrar exports no scanner de jogos nem reduzir artificialmente imports
pendentes. Ainda não há integração do registry com o startup Orbis upstream.

## Implementação e invariantes

`GuestMemory` usa até 64 descritores imutáveis de spans pertencentes ao host,
com leitura e flag de escrita. São endereços de backing real, não bias
diagnóstico ou valores de ELF escolhidos pelo arquivo. Rejeitar ranges vazios,
sobrepostos ou com overflow; nunca unir ranges adjacentes. Descritores são
capabilities confiáveis: validar números não prova que um backing arbitrário
está mapeado. Só o host os constrói após alocar/mapear/verificar proteções.

Transferência limitada a 1 MiB nesta política experimental. Validar ambos
os intervalos por subtração antes de dereferenciar ou escrever; operação
inválida não altera destino. Não permitir escreve em RX/readonly, gaps,
padding fora do LOAD, stack host ou contexto/callback nativo. `memcpy`
com overlap não zero é rejeitado, inclusive source=dest: esse caso não é
definido pelo contrato C; não substituir por memmove. `memcmp` normaliza o
sinal e retorna valor de registro sign-extended, preservando seu s32 em EAX.

Zero bytes não dereferencia, mas exige ponteiro não nulo autorizado, podendo
ser one-past-end. Região contendo o endereço tem prioridade sobre o limite
da vizinha, respeitando readonly mesmo com zero bytes. Essa é política
restrita do harness, não promessa de aceitar argumentos inválidos do CRT.

Callback recebe três argumentos SysV RDI/RSI/RDX em Win64 RCX/RDX/R8;
contexto oculto é R9, não controlado pelo guest. Mover RDX para R8 antes
de reutilizar RDX. Thunk possui shadow/alignment de 40 bytes, CLD e unwind
registrado/verificado. Entradas guest leaf fixam terceiro argumento em 16,
0 ou UINT64_MAX e fazem tail-call pelo JUMP_SLOT resolvido. Código/thunks RX,
dados RW, gaps NOACCESS, cache flushed, nenhuma página RWX.

As duas fixtures têm execução serial, mapas fixos e lifetime do harness.
Não podem ocorrer munmap/mprotect concorrentes entre check e acesso.
Remover tabelas antes de liberar backing, preservando o recurso se o SO não
conseguir remover a tabela. Isso não resolve sincronização da MMU real.

## Rejeição e limites de execução

Resultado portátil tem flag valid separada do valor, inclusive memcmp zero.
No callback autoral inválido retorna zero **e incrementa contador de rejeição**;
o oracle exige precisamente a rejeição esperada. Não é libc de jogo retornando
sucesso, errno, nem mecanismo de recuperação de uma exceção guest arbitrária.
Antes de registrar para jogos, integrar essas falhas ao boundary de faults do
runtime, incluindo unwind de funções guest gerais. Não executar eboot neste PR.

Não implementar malloc/free, mmap/mprotect/munmap, heap PS4, TLS Orbis,
filesystem, concorrência, guest stack como range autorizado ou chamadas com
varargs/floating-point. O escopo é a fronteira de memória para HLE, não MMU.

## Verificação

Portátil: matriz de offsets/counts com bytes esperados completos, exato fim,
overflow/limite/readonly/zero/overlap, mapa inválido e vizinhas de permissões
diferentes. Link raw/SELF, três imports reais, relativa e mismatch de versão
transacional. Testes com ASan/UBSan e warnings como erros.

Nativo: 17 chamadas por formato (34 total), oito rejeições por formato (16
total), retorno de ponteiros, low byte de memset, memcmp nos três sinais,
cópia a partir de RX, rejeição de ponteiro host/gap/readonly/cruzamento lógico/
UINT64_MAX/overlap/tamanho, zero one-past-end. Comparar todo LOAD de dados e
código após cada chamada; verificar unwind dos quatro frames bridge/thunks e
cleanup. O oracle simulado de unwind não é um fault novo no callback.

Telemetria `guest_memory_*` em `phase5-execution.jsonl`; 25 probes preservados.
Manter `game_memory_exports_registered=0`,
`guest_memory_general_faults_supported=0`, `game_executed=0`, `game_frame=0`.
Gate de hardware e integração runtime são verificações separadas; 5D aberta.

## Evidência de hardware

Sessão `134357153616663927-6448`: 34 chamadas, 16 rejeições intencionais,
stage `complete`, calls/ranges/rejections/unwind_cleanup em 1. Os 25 probes
passaram, incluindo clock anterior, retomada e apresentação. Nova varredura
após resume aprovou os dois títulos sem mudança de imports/relativas/FNV.
Isso não valida faults gerais ou registra serviços para jogos.
