# ADR 0048 — Workers autorais e contexto host TLS no UWP

Status: implementado; validação no Xbox pendente.

## Contexto e dependências

O round-trip 5D da PR 47 passou no Series S: retorno HLE 42, rejeições,
unwind/cleanup e 25 probes com retomada sem alertas, sessão
`134357001568553969-6812`. O próximo gate combina concorrência e isolamento
de contexto. A implementação Windows upstream em `src/core/tls.cpp` usa
TlsAlloc/SetValue/GetValue, mas depende de TCB/pthreads e patches CPU ainda
não integrados ao UWP. Não reutilizar esses módulos parcialmente como se
um slot TLS host implementasse TLS Orbis.

## Decisão

- Duas workers Windows com stack reservada de 256 KiB cada; cada worker
  possui imagem autoral, bridges e tabelas de unwind independentes.
- Uma chave TLS host por execução do probe, sem singleton/global mutável.
- Cada thread vincula seu próprio FixtureThreadContext (owner, value, calls).
- O thunk HLE passa a chave como terceiro argumento Win64 oculto R8.
  Os dois argumentos SysV continuam operation/argument; o guest não escolhe
  a chave nem recebe ponteiro para contexto host.
- Callback recupera o contexto por TlsGetValue e verifica owner contra
  GetCurrentThreadId. Contexto ausente, owner incorreto e chave inválida são
  rejeitados antes do dispatcher. Sem exceções C++ cruzando o boundary.
- Serviços autorais 2/3/4 escrevem/leem valor e identificam a worker.
  Não são funções Orbis nem códigos de erro Sony.

Cada worker grava 100 ou 101 através da HLE, sinaliza ready e aguarda release.
O coordenador espera ambos os ready antes de liberar a leitura. Cada worker
confirma seu valor e owner, rejeita leitura com argumento inválido e confirma
que o valor não mudou. As quatro chamadas aritméticas anteriores e os faults
5C permanecem gates em **ambas** as workers.

## Lifetime e falhas

RAII limpa o binding TLS também em retornos antecipados. Ready é sinalizado
na saída para não prender o coordenador se a preparação falhar; sucesso
exige os resultados de ambas, não apenas os sinais. Rendezvous tem timeout
de 10 segundos. Falha na criação de uma thread libera/junta a outra.

Após join, fechar handles de threads/eventos e liberar a chave; a thread
coordenadora precisa continuar sem contexto. Não usar TerminateThread ou
liberar estado/código ainda executável. Falha de join mantém estado, eventos,
handles e chave residentes e falha o probe. Remoção de tabela falha de forma
segura conforme ADR 0046. Join usa espera definitiva apenas para o código
fixo autoral, não para guest arbitrário; este teste não é sandbox de timeout.

## Verificação

Teste portátil valida serviços e isolamento entre dois objetos de contexto,
além dos bytes reais da bridge e terceiro argumento no Linux x86-64.
Esse teste **não** certifica TLS Windows ou concorrência UWP. O teste nativo
Windows executa as duas workers/TLS/rendezvous e o mesmo probe vai ao MSIX.
O Series S deve confirmar cleanup, isolamento e retomada nos três logs habituais.

## Limites e próximos requisitos

TLS host não substitui PT_TLS, TCB/DTV, GS-relative access, canary/errno guest,
patches CPU ou pthreads Orbis. Não há criação de threads por guest, scheduler,
fibers, cancelamento, guest stack própria, filesystem ou scePad nesse gate.
Não há boot de jogo nem unwind geral de funções guest não leaf. A 5D continua
aberta. Aumentar testes para esses caminhos antes de executar conteúdo geral.

## Referências

- [TlsAlloc](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-tlsalloc).
- [TlsSetValue](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-tlssetvalue).
- `src/core/tls.cpp` e `src/core/tls.h` do upstream v0.18.0.
- ADRs 0046 e 0047: unwind e round-trip HLE autoral.
