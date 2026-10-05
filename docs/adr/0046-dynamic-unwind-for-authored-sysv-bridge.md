<!-- SPDX-FileCopyrightText: 2026 Shiney-X and xbox-series-d3d12 contributors -->
<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# ADR 0046 — Unwind da bridge SysV sem VEH

Status: implementação; builds Windows/AppContainer e Xbox são gates separados.

## Contexto e decisão

5C inicial passou no Xbox, mas tratava fault somente em leaf Win64 separada.
O SDK UWP não expõe Add/RemoveVectoredExceptionHandler. Não redeclarar esses
símbolos ou carregar APIs desktop à força. O header oficial declara
RtlAdd/DeleteFunctionTable para APP/SYSTEM, e Lookup/VirtualUnwind para APP.
Isso autoriza testar a superfície do SDK, não presumir funcionamento Xbox.

Registrar uma RUNTIME_FUNCTION com UNWIND_INFO versão 1 para a bridge fixa:
push RDI/RSI, alocação de 296 bytes e saves XMM6–15. Gerar code offsets de
instruções conhecidos, opcodes em ordem reversa e offsets escalados segundo
o formato x64. Fixture code, RUNTIME_FUNCTION e metadados têm offsets
separados na alocação da bridge; ela passa RW→RX antes do registro.

Verificar lookup e RtlVirtualUnwind em contexto sintético no ponto de retorno
da chamada guest, com canários para todos os XMM salvos, RSI/RDI, RSP e RIP.
Só depois chamar a leaf SysV autoral que clobbera RSI/RDI/XMM6 e lê o guard.
A leaf não modifica RSP ou não voláteis SysV. O unwind de leaf chega à bridge;
o unwind registrado chega ao frame nativo SEH com metadata do compilador.
Reexecutar soma 42 após a captura. Manter teste Win64 anterior como regressão.

## Lifetime e limites

- API/tabela têm retorno verificado; não desligar mitigações do processo.
- Desregistrar antes de liberar memória. Em falha de remoção, falhar probe
  e manter a alocação da tabela/código residente para evitar dangling pointer.
- O filtro continua aceitando somente RIP/read/endereço esperado no escopo
  nativo da chamada; sete controles negativos por perfil rejeitam outros casos.
- Não há handler global VEH e não há execução de arquivos externos.
- A captura aborta a chamada; não retoma um guest depois de corrigir um fault.
- `sysv_fault_unwind_supported=1` exige `unwind_scope=authored_leaf_and_fixed_bridge`:
  não equivale a suporte a unwind de funções guest gerais.
- Guest não leaf, prólogos/epílogos arbitrários, frames Orbis, guest→host,
  TLS/FS/GS, callbacks e interpretação de unwind guest continuam pendentes.
- Não é integração do loader/JIT upstream nem boot de Deltarune.

## Evidência exigida

Oracle portátil de 52 bytes de UNWIND_INFO, layout de instruções e execução
normal da bridge. ASan/UBSan nesses testes. Windows CTest executa lookup,
oracle de contexto, fault Win64, fault SysV e cleanup do mesmo probe UWP.
O MSIX deve compilar com WINAPI_FAMILY_APP. Console deve exportar relatório
com canários/proteções/filtros/remoção positivos e regressão após retomada.

## Fontes primárias

- [Header oficial winnt.h](https://github.com/microsoft/win32metadata/blob/main/generation/WinSDK/RecompiledIdlHeaders/um/winnt.h): partitions APP de RtlAdd/DeleteFunctionTable e RtlVirtualUnwind.
- [RtlAddFunctionTable](https://learn.microsoft.com/windows/win32/api/winnt/nf-winnt-rtladdfunctiontable): registro dinâmico e lifetime da tabela.
- [x64 exception handling](https://learn.microsoft.com/en-us/cpp/build/exception-handling-x64): UWOP_ALLOC_LARGE, PUSH_NONVOL e SAVE_XMM128.
- [x64 calling convention](https://learn.microsoft.com/en-us/cpp/build/x64-calling-convention): unwind de leaf versus não leaf e não voláteis Windows.
