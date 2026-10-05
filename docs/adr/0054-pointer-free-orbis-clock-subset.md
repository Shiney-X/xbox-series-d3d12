# ADR 0054 — Subset Orbis de tempo sem ponteiros guest

Status: implementado e validado no Series S no corpus autoral fechado.

## Escopo e referências

Iniciar HLE com três funções reais de `libkernel` de assinatura `u64(void)`,
sem memória guest, varargs, floating-point ou chamadas bloqueantes:

| API | NID upstream | Unidade |
| --- | --- | --- |
| sceKernelGetProcessTime | 4J2sUJmuHZQ | Microssegundos desde epoch |
| sceKernelGetProcessTimeCounter | fgxnMeTNUtY | Ticks desde epoch |
| sceKernelGetProcessTimeCounterFrequency | BNowx2l588E | Ticks por segundo |

Referência primária local: registros e funções em
`src/core/libraries/kernel/time.cpp`, assinaturas em `time.h` e
`src/common/native_clock.*`. O upstream usa TSC; o adapter UWP usa um domínio
**virtual QPC**, não deve misturar as unidades com TSC direto.
[QPC suporta UWP](https://learn.microsoft.com/en-us/windows/win32/api/profileapi/nf-profileapi-queryperformancecounter);
[a documentação Microsoft distingue QPC de leituras TSC diretas](https://learn.microsoft.com/en-us/windows/win32/sysinfo/acquiring-high-resolution-time-stamps).

## Implementação e dependências

`KernelClock` é independente de Windows: frequency/epoch imutáveis, contador
`now - epoch` e conversão inteira para microssegundos. Aceitar frequência
positiva até `UINT64_MAX / 1000000`, limite aritmético do produto do resto.
Isso inclui frequências GHz sem fixar o clock do hardware. Dividir em segundos
inteiros/resto para evitar multiplicar o contador inteiro por 1.000.000.
Rejeitar backwards, serviço inválido, frequência inválida e overflow sem UB.
Não usar double, relógio civil/UTC ou quantidade fixa de ticks por segundo.

Adapter serial chama QueryPerformanceFrequency uma vez e inicializa epoch
QPC uma vez para as duas fixtures. Não remover tempo de suspensão: política
de pause temporal e persistência da epoch no runtime real continuam pendentes.
Falha de QPC/conversão marca o gate como falho, não stub de sucesso.

O namespace fixo do teste é libkernel versão 1, módulo libkernel 1.1, tipo
função. ELF/SELF autorais carregam três imports, três JUMP_SLOT e uma relativa;
link estrito verifica imagem e targets. Thunks separados `u64(void)` carregam
contexto host oculto em RCX para callback Win64. Argumentos RDI/RSI do guest
não são usados e não escolhem serviço/contexto/callback.

Continuam RW antes do commit, RX antes de chamar, dados RW/gaps NOACCESS,
flush de cache, tabelas unwind registradas e verificadas. Callback/contexto e
imagem têm lifetime do harness fechado. Remover tabelas antes de liberar;
em falha de remoção preservar backing referenciado pelo SO. Sem RWX.

## Verificação e limites

Portátil: unidades/valores conhecidos, frequência inválida, backwards,
overflow, ELF/SELF, três slots, relativa e versão incompatível sem writes.
Nativo: seis chamadas por formato, 12 total, bracket host QPC antes/depois,
monotonicidade e frequência coerentes, tipo de retorno u64, unwind/cleanup.
O gate entra no probe existente, sem nova linha de Diagnostics ou relatório.

Esses são serviços Orbis implementados num adapter restrito, **não o runtime
HLE upstream integrado inteiro**. Fixture autoral não é código comercial.
Os exports não entram no scanner de jogos: os três NIDs nem aparecem nos
inventários Sonic/Deltarune recebidos. Não prometer queda dos imports pendentes.

Não implementar/registrar `sceKernelReadTsc` ou `sceKernelGetTscFrequency` com
QPC, nem afirmar compatibilidade com instrução guest RDTSC. Resolver esse
domínio e seu tratamento no CPU runtime antes de liberar um jogo que o use.
Não certificar execução concorrente, TLS, errno, filesystem, inicializadores,
faults/unwind gerais ou boot por estes serviços passarem.

Telemetria `kernel_clock_*` fica em `phase5-execution.jsonl`; exigir serviço=3,
chamadas/unidades/unwind/cleanup positivos e frequency > 0.
`game_clock_exports_registered=0`, `kernel_clock_direct_rdtsc_compatible=0`,
`game_executed=0`, `game_frame=0` permanecem. A 5D continua aberta.

## Evidência no console

Sessão `134357143372603935-5336`: três serviços, calls/units/unwind/cleanup em
1, stage `complete` e frequência QPC de 1745500000 counts/s (não frequência
física da CPU nem certificação de TSC PS4). Os 25 probes passaram; apresentação
após retomada e inspeção dos dois títulos sem regressão. Não houve varredura
após resume nesta captura, nem teste da política temporal de suspensão.
