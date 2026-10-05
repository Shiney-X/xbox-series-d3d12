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

VEH local ao escopo do probe e filtrado por TLS, RIP, leitura e endereço
reservado esperado. Ao fault conhecido, CONTEXT aponta ao epílogo próprio
com RSP salvo. Sem unwind pelo código emitido e sem absorver outras falhas.
Remover o handler antes de liberar as alocações. Executar novamente a soma
após a recuperação para comprovar que a boundary retorna ao host.

Consultar mitigações antes de executar; não desativar CFG/dynamic-code policy.
APIs devem compilar/linkar no SDK AppContainer e ser testadas em hardware;
documentação desktop de VEH não é garantia de suporte Xbox.

## Referências e validação

- [VirtualProtectFromApp](https://learn.microsoft.com/en-us/windows/win32/api/memoryapi/nf-memoryapi-virtualprotectfromapp): RX exige codeGeneration, RWX não permitido e cache deve ser sincronizado.
- [GetCurrentThreadStackLimits](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-getcurrentthreadstacklimits): limites da stack da thread gerenciada pelo sistema.
- [AddVectoredExceptionHandler](https://learn.microsoft.com/en-us/windows/win32/api/errhandlingapi/nf-errhandlingapi-addvectoredexceptionhandler): registro/remoção; sua lista desktop não comprova compatibilidade UWP.

Teste portátil compara o layout e executa a mesma bridge Win64→SysV em
Linux x64 com dois resultados distintos. Não testa APIs Windows/faults.
CTest Windows executa o probe exato do UWP, incluindo fault e proteções.
O MSIX comprova compilação AppContainer; `phase5-execution.jsonl` no console
é o gate de funcionamento Xbox. Tudo mantém `game_executed=0;game_frame=0`.
