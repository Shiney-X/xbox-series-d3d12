# ADR 0047 — Boundary autoral guest→host no UWP

Status: implementado; validação no Xbox pendente.

## Contexto e viabilidade

A PR 46 foi validada no Series S: soma 42 antes/depois do fault SysV,
unwind/remoção positivos, 25 probes e apresentação após retomada na sessão
`134356994903973974-1208`. Esse gate não autoriza executar funções PS4 gerais.
O próximo requisito é demonstrar uma chamada na direção inversa sem depender
do loader desktop, sysmodules ou um jogo.

## Decisão

Uma entrada ELF autoral faz tail-jump indireto a um thunk fixo. O harness
escreve seu próprio endereço no segmento RW em `0x4200`, antes da execução.
Não há resolução de endereço/NID vindo de arquivo externo. A entrada não
altera RSP nem registradores não voláteis: não introduz frame guest não leaf.

O thunk converte dois argumentos inteiros RDI/RSI em RCX/RDX, reserva 40 bytes
(shadow space e alinhamento), chama um callback nativo `noexcept`, restaura RSP
e retorna para a bridge 5C. O serviço autoral implementa apenas incrementação:
operação 1, argumento 41, resultado 42. Operação desconhecida retorna UINT64_MAX;
overflow retorna UINT64_MAX-1. Esses códigos são exclusivos da fixture,
**não** erros Orbis. Não recebe ponteiros nem executa IO.

Código e metadados do thunk possuem alocação própria RW→RX. Uma segunda
RUNTIME_FUNCTION registra UNWIND_INFO versão 1 (ALLOC_SMALL, 40 bytes).
Lookup e RtlVirtualUnwind verificam RIP/RSP com contexto sintético no call-site,
não no epílogo. A tabela é removida antes de liberar a alocação; falha mantém
código residente e falha o probe, como na 5C.

## Implementação e testes

- `guest_hle_fixture.h`: fixture, emissor fixo e dispatcher portátil sem estado.
- Probe UWP existente: round-trip real, operação desconhecida, overflow e nova
  chamada após rejeição; mantém faults/proteções/cleanup da 5C.
- Teste Linux x86-64: executa os mesmos bytes com callback `ms_abi`.
- Teste nativo Windows e build AppContainer: gates separados do hardware.
- Relatório existente `phase5-execution.jsonl`; nenhuma linha adicional na UI.

## Limites e próximos requisitos

Não é resolver de imports, syscall interception, HLE upstream ou boot.
Somente dois inteiros, sem argumentos na stack, varargs, floats ou agregados.
Não testa fault dentro do callback, reentrada, TLS, errno, stack Orbis,
threads guest, cancelamento, filesystem ou scePad. Não há exceções C++ cruzando
o boundary. Frames guest não leaf e startup continuam bloqueios explícitos.
A 5D permanece aberta até integrar e testar seus demais requisitos.

## Referências

- [ABI x64 Microsoft](https://learn.microsoft.com/en-us/cpp/build/x64-calling-convention).
- [Unwind x64 Microsoft](https://learn.microsoft.com/en-us/cpp/build/exception-handling-x64).
- ADR 0046: lifetime e unwind da bridge de entrada.
