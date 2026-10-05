# ADR 0053 — Execução autoral através de import resolvido

Status: implementado e validado no Xbox (sessão `134357133173433935-3916`).

## Contexto

As ADRs 0047/0048 provaram o thunk HLE e isolamento host TLS, mas o runner
gravava seu endereço manualmente no slot da fixture. A ADR 0052 provou lookup
tipado/versionado e relocations com valores numéricos, sem chamar o destino.
Conectar esses dois gates antes de cadastrar HLE real para jogos.

## Decisão e fluxo

Criar corpus autoral fechado `MakeGuestHleImportFixture`: manter as folhas de
execução/fault existentes, acrescentar metadados dynamic e uma função global
undefined `fixture#A#B` de `libFixture`. Há uma relativa para o entry e um
JUMP_SLOT para a chamada tail-call. Worker zero usa ELF raw; worker um SELF
direto não criptografado/comprimido. Nenhum eboot comercial entra no runner.

Cada worker reserva sua imagem guardada e páginas próprias do bridge/thunk,
inicialmente RW. Seu export é construído internamente a partir da chave
autoral fixa e endereço do thunk pertencente à worker; não é callback direto.
O bias é `payload_address - guest_base`, conferido contra underflow/overflow.

Executar três gates negativos: registry vazio, versão incompatível e export
de objeto no lugar da função. Todos precisam rejeitar link estrito sem patches
e preservar a imagem original. Não executar resultados parciais/negativos.

O caminho positivo usa `StageGuestDataLink` estrito e exige um import matched,
uma relativa, um import aplicado, zero pendências, patches e demais bytes
verificados. Copiar somente esse resultado à alocação própria; readback da
imagem inteira e de ambos os slots. **Não gravar o GOT manualmente.**

Aplicar RX ao código, RW à página de dados e NOACCESS aos gaps, sem RWX.
Verificar proteções, instruction cache e registro/unwind dos bridges antes
de qualquer chamada. A relativa precisa apontar para o entry real, que é
chamado através desse endereço validado. A folha tail-call usa o JUMP_SLOT
resolvido para o thunk HLE. Testar retorno 42, operação/overflow rejeitados,
contexto TLS independente e continuidade após fault autoral esperado.

Lifetime continua RAII: remover tabelas antes de liberar código; em falha de
remoção preservar backing ainda referenciado pelo SO. TLS limpo, join e
handles liberados antes de destruir contextos. Não TerminateThread.

## Telemetria e limites

Sem novo relatório ou linha de Diagnostics. Acrescentar em
`phase5-execution.jsonl` campos agregados das duas workers:
`import_link_verified`, `import_slot_verified`, `import_relative_verified`,
`import_call_verified`, `import_missing_rejected`, `import_version_rejected`
e `import_type_rejected`. Todos devem valer 1. Scope
`closed_authored_raw_self`, binding `typed_resolver_jump_slot`,
`import_manual_patch=0` e `game_runtime_exports_callable=0`.

A primitiva de link continua numérica/data-only: seu sucesso não autoriza
executar qualquer arquivo. **Só o harness fechado conhece ownership,
protótipos e lifetime dos thunks e certifica a chamada autoral.** Não expor
seus exports ao scanner/biblioteca nem interpretar um NID comercial como
esse serviço de teste. Dois argumentos inteiros, sem varargs/FP/agregados ou
ponteiros guest. TLS é slot host, não TCB/DTV/FS/GS Orbis. Unwind segue restrito
às folhas e bridges fixos. Não usar esse gate para liberar funções guest gerais.

Testes portáteis raw/SELF conferem código preservado, ambos os slots e
rejeições em dados. Teste nativo Windows exige execução/cleanup nas duas
workers; validar também no Series S e após retomada. Jogos continuam com
zero exports runtime, imports pendentes e `game_executed=0;game_frame=0`.
Próximos gates: HLE Orbis real/ABI/serviços mínimos, VA/startup e faults/unwind
gerais. A 5D permanece aberta; não há tentativa de boot nesta entrega.

Evidência Series S: sete gates `import_*` em 1, retorno 42, workers 100/101,
IDs distintos, unwind/cleanup positivos, 25 probes e apresentação após resume
aprovados. Relocations/fingerprints/imports dos jogos preservados, zero exports
runtime e relatórios sem erro, inclusive na varredura após retomada.
