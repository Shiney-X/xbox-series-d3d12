<!-- SPDX-FileCopyrightText: 2026 Shiney-X and xbox-series-d3d12 contributors -->
<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# ADR 0045 — Executar leaf SysV autoral antes do entry Orbis

Status: implementação inicial; Windows/UWP e hardware são gates separados.

## Decisão

Executar somente um ELF cru gerado pelo projeto, com função leaf x86-64
SysV, dois argumentos inteiros e resultado 42. Reutilizar a validação/staging
5B. Não chamar `RunMainEntry`, o loader desktop ou binários de jogo.

Uma bridge de bytes autorais adapta Win64 RCX/RDX para SysV RDI/RSI,
alinha a stack e preserva não voláteis adicionais do Windows (RSI/RDI e
XMM6..15). Os demais GPRs não voláteis são compartilhados pelos dois ABIs.
A fixture clobbera RSI/RDI/XMM6, verifica alinhamento e usa a red zone.
Não é chamada genérica variádica, ABI de agregados ou startup de processo.

Usar thread Windows dedicada e sua stack gerenciada pelo OS, mantendo
limites válidos para dispatch de exceções. Troca manual de RSP para buffer
e alteração manual de TEB foram evitadas nesta etapa. Reserva solicitada
256 KiB; não representa orçamento/stack final de um jogo.

## Memória e faults

Imagem limitada de 32 KiB mais extremos não comprometidos; somente páginas
necessárias ficam RX/RW. Bridge própria RW→RX, FlushInstructionCache,
VirtualQuery e nenhuma alocação RWX. Endereços guest permanecem metadados;
a fixture não exige relocations ou acesso absoluto ao seu endereço virtual.

O primeiro build AppContainer rejeitou Add/RemoveVectoredExceptionHandler
(C3861); o protótipo VEH foi removido, sem redeclarar APIs fora da família UWP.
Usar helper nativo SEH e uma leaf Win64 auxiliar do mesmo ELF autoral, chamada
diretamente, sem passar pela bridge SysV. A leaf lê o endereço reservado e não
altera RSP/não voláteis; unwind de leaf usa o retorno para o frame nativo
com metadata do compilador. O filtro só trata RIP/read/endereço esperados.

Sete controles negativos rejeitam registros fora do escopo. Reexecutar a soma
SysV depois do fault Win64. Isso **não** valida faults/unwind através da
bridge SysV; esse caminho permanece bloqueio antes de guest geral.

Consultar mitigações antes de executar; não desativar CFG/dynamic-code policy.
APIs devem compilar/linkar no SDK AppContainer e ser testadas em hardware;
O caminho final usa SEH compilado, já empregado no probe de executable-memory.

## Referências e validação

- [VirtualProtectFromApp](https://learn.microsoft.com/en-us/windows/win32/api/memoryapi/nf-memoryapi-virtualprotectfromapp): RX exige codeGeneration, RWX não permitido e cache deve ser sincronizado.
- [GetCurrentThreadStackLimits](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-getcurrentthreadstacklimits): limites da stack da thread gerenciada pelo sistema.
- [AddVectoredExceptionHandler](https://learn.microsoft.com/en-us/windows/win32/api/errhandlingapi/nf-errhandlingapi-addvectoredexceptionhandler): registro/remoção; sua lista desktop não comprova compatibilidade UWP.
- [x64 calling convention](https://learn.microsoft.com/en-us/cpp/build/x64-calling-convention): leafs sem estado não volátil alterado não exigem metadata de unwind; não generalizar à bridge não leaf.
- [try-except](https://learn.microsoft.com/en-us/cpp/cpp/try-except-statement): filtro local de SEH compilado.

Teste portátil compara o layout e executa a mesma bridge Win64→SysV em
Linux x64 com dois resultados distintos. Não testa APIs Windows/faults.
CTest Windows executa o probe exato do UWP, incluindo fault e proteções.
O MSIX comprova compilação AppContainer; `phase5-execution.jsonl` no console
é o gate de funcionamento Xbox. Tudo mantém `game_executed=0;game_frame=0`.
