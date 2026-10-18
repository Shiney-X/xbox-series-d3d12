<!-- SPDX-FileCopyrightText: 2026 Shiney-X and xbox-series-d3d12 contributors -->
<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# KytyPS5: análise e integração inicial

Fonte: [Shiney-X/KytyPS5](https://github.com/Shiney-X/KytyPS5), revisão
`cd0c360aa7b432645b915c19fd1ceed394378b75`, branch
`xbox-on-upstream-2026-10-17-c2173801`. Não seguir a branch mutável no build.

## O que foi analisado

Inventário dos 820 caminhos versionados (incluindo submódulos), documentação
README, XBOX_CHANGES, docs/d3d12-backend e src/uwp/README; leitura direcionada
do compilador DXIL, seleção de backend, features de shaders, integração UWP,
receita CI/dependências, import checker e pontos de memória/TLS/startup do loader.
Não é uma revisão linha a linha dos 820 caminhos nem dos submódulos. O backend
D3D12 sozinho possui 45 arquivos; não há alegação de certificação integral.

## Como funciona

Não há uma implementação geral de Vulkan-to-D3D12 neste fork. O backend é
escolhido no build: o PM4, os recursos guest, as bibliotecas e o recompiler
compartilhados alimentam as classes do backend selecionado. O caminho gráfico é:

`shader guest RDNA2 → IR Kyty → SPIR-V → Mesa/NIR → DXIL → D3D12`

O compilador usa uma revisão de Mesa com patch próprio para vincular os estágios
gráficos, gerar DXIL e reduzir operações não suportadas. O DXIL é validado e
assinado pelo dxil.dll do SDK. Isso não implementa syscalls, CPU/TLS ou libc PS4.
Os registros/formatos PS5 e IR Kyty não são os registros GCN/IR do shadPS4.

O fork documenta testes físicos no Series X e também várias mudanças ainda não
testadas. Essas afirmações não são resultados de nosso Series S. O dispositivo
e o validador devem ser consultados; não copiar limites do Series X como se
fossem medições deste console.

## O que esta integração realmente adiciona

- Patch Mesa do Kyty copiado sem alterações, com revisão/hash/licenças.
- Receita de build do tradutor adaptada e executada a partir de fonte fixa.
- Adaptador C++ independente das classes Kyty, que recebe SPIR-V do nosso
  recompiler e retorna somente DXIL validado/assinado. Compute ou VS/PS vinculados.
- Negociação do shader model contra device e validador; redução de 16 bits
  quando o device não reporta suporte nativo. Nenhum requisito SM 6.6 presumido.
- Integração CMake e MSBuild/UWP opcional por `XBOX_MESA_ROOT`; o CI habilita.
- Teste D3D12 de compute 2×2 e dois draws 4×4 com readback: IR autoral emitido
  pelo shadPS4 real, não shader extraído de um jogo nem primeiro frame de jogo.
- O caminho SPIRV-Cross continua independente; falha Mesa não vira sucesso
  através de fallback silencioso. O shell continua disponível para obter logs.
- Diagnóstico detalhado do teste de entrada nativa da PR 57: resultados raw,
  SELF, repetição e rejeição separados, inclusive erros das duas consultas de
  políticas. O bloqueio de segurança continua ativo se o teste falhar.

Os espaços de PushData continuam os do nosso contrato: compute b0/space0,
VS b0/space1 e PS b0/space2. O espaço 1001 é reservado para runtime Mesa;
programas que exigem esses dados não são executados por este teste inicial.
O caminho gráfico usa baseVertex/baseInstance zero explicitamente; isso não
autoriza ignorá-los em draws arbitrários. Não há substituição do IR shad pelo Kyty.

## O que não foi importado

| Área Kyty | Motivo / situação no shadPS4 |
|---|---|
| Loader/runtimeLinker e HLE Prospero | ABI, símbolos e contratos PS5 diferentes; exige adaptação Orbis, não cópia |
| PM4, register state e frontend RDNA2 | Preservar o frontend GCN do shadPS4 |
| Caches, descritores, page table BDA e renderer completo | Classes e layouts Kyty incompatíveis; faltam ligação Liverpool e recursos guest |
| Lazy memory/pager e patcher TLS/red-zone | Dependem do MMU, linker, funções guest e tratamento de faults Kyty |
| Desktop shims e stubs de serviços | Não esconder APIs ausentes nem simular boot bem-sucedido |
| Patching de bits LLVM para remover readnone | Não adotado; os probes iniciais não certificam atomics nem jogos gerais |
| UI, troféus, rede e co-op | Não aproximam o objetivo imediato de execução guest / primeiro frame |

Não foram copiados executáveis do Kyty, firmware, jogos, chave de assinatura ou
credenciais. Licenças e proveniência estão em third_party/kyty_spirv_to_dxil.

## Compilar e testar

Windows com SDK 26100, MSVC, Python 3.12, Meson/Ninja/Mako/PyYAML/packaging:

```powershell
./scripts/windows/Build-MesaTranslator.ps1 -OutputDirectory C:/deps/shad-mesa
$env:XBOX_MESA_ROOT = 'C:/deps/shad-mesa'
cmake --preset windows-msvc
cmake --build --preset windows-msvc-debug
ctest --preset windows-msvc-debug
./scripts/windows/Build-UwpPackage.ps1
```

O CI guarda o tradutor em cache por hash e publica o SDK com fonte/patch
identificados. O pacote deve conter spirv_to_dxil.dll, dxil.dll e as licenças.
O import checker do Mesa é estrito: não assume que compilar em Windows torne
qualquer API de desktop disponível em UWP. O teste nativo usa WARP; aprovação
dele NÃO substitui o teste físico no Series S.

No Xbox instalar o pacote como Game, abrir Diagnostics e executar Deltarune →
A BOOT TEST. Enviar phase0-results.jsonl (campo d3d12-mesa-dxil),
phase5-execution.jsonl, phase5-boot.jsonl e phase0-lifecycle.jsonl. Se a entrada
continuar bloqueada, os campos native_probe_raw_first_blocker,
native_probe_raw_first_shadow_policy_error e equivalentes devem revelar a
causa; não remover o gate para forçar o jogo.

## Ainda necessário até um frame real

Resolver o bloqueio físico de entrada; adaptar startup/libc, TLS, memória e
serviços Orbis efetivamente exigidos pelo título; integrar o processamento PM4
e recursos guest ao backend D3D12; traduzir shaders GCN reais com bindings e
semântica preservados; levar o flip VideoOut à apresentação. A tradução SPIR-V
é uma peça reutilizada importante, não o emulador completo. Não há garantia de
boot ou primeiro frame nesta integração, nem conclusão automática de fases.
