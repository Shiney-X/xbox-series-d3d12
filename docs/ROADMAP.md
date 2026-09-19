# Roadmap

## Fase 0 — Viabilidade UWP

**Status: concluída no Xbox Series S em 19 de setembro de 2026.**

- [x] Criar o scaffold do repositório.
- [x] Implementar probe Win32 de capabilities.
- [x] Implementar probe Win32 de memória virtual.
- [x] Implementar probe Win32 de memória executável.
- [x] Implementar probe de dispositivo D3D12.
- [x] Adicionar template de manifest com `codeGeneration`.
- [x] Criar host UWP x64 para executar os mesmos probes.
- [x] Gerar pacote de sideload assinado no CI.
- [x] Implantar e executar no Xbox Series S sem debugger.
- [x] Implementar aliases e placeholders no probe de memória.
- [x] Validar aliases e placeholders no Xbox Series S em perfil Game.
- [x] Apresentar um clear D3D12 por swapchain UWP.
- [x] Implementar triângulo D3D12 com root signature e PSO.
- [x] Validar o triângulo D3D12 no Xbox Series S em perfil Game.
- [x] Substituir o probe DXBC/Shader Model 5 por DXC/DXIL em runtime.
- [x] Validar DXC/DXIL no Xbox Series S em perfil Game.
- [x] Implementar medição controlada de pressão de memória.
- [x] Instrumentar suspensão e retomada com reapresentação D3D12.
- [x] Validar pressão de memória no Series S em perfil Game.
- [x] Validar suspensão, retomada no mesmo processo e nova apresentação D3D12.
- [x] Confirmar tela verde após tratar `IDXGIDevice3::Trim` como capability opcional.

### Critério de saída

A fase termina somente quando os resultados brutos do console estiverem
registrados. Falha irrecuperável de mapeamento ou execução bloqueia o port UWP.

O critério foi cumprido em perfil **Game** no Xbox Series S com sistema
`10.0.26100.9426`. CPU, memória virtual, aliases, código executável, limite de
5 GiB, D3D12/DXIL, apresentação, persistência e suspensão/retomada passaram.
Consulte [`docs/results`](results/README.md).

## Fase 1 — Integração do upstream

**Status: integração base, shell Xbox e acesso inicial ao USB validados no
console; navegador de diretórios em desenvolvimento.**

- [x] Definir estratégia de importação preservando histórico Git.
- [x] Registrar revisão upstream de referência.
- [x] Integrar `v.0.18.0` na raiz e resolver colisões da Fase 0.
- [x] Obter build Windows desktop sem regressões.
- [x] Isolar frontend/UI desktop do host UWP.

### Fase 1B — Interface inicial no Xbox

- [x] Criar uma bridge UWP compilada contra tipos reais do core upstream.
- [x] Substituir a tela do triângulo por um shell D3D12 nativo.
- [x] Adicionar navegação por direcional, A e B.
- [x] Exibir seções Jogos, Configurações e Diagnósticos.
- [x] Persistir o resultado da bridge em `LocalState/phase1-core.jsonl`.
- [x] Validar desenho, navegação e retomada no Xbox Series S em perfil Game.

### Fase 1C — Acesso USB à biblioteca no Xbox

- [x] Medir `FolderPicker` com e sem usuário e USB no Xbox.
- [x] Registrar que o seletor abre, mas não enumera nenhuma origem no console.
- [x] Substituir o seletor por `KnownFolders.RemovableDevices`.
- [x] Detectar assincronamente o primeiro dispositivo removível.
- [x] Expor estados de varredura e acesso USB na tela Games.
- [x] Persistir diagnóstico em `LocalState/phase1-library.jsonl`.
- [x] Validar `KnownFolders.RemovableDevices` no Xbox Series S.
- [x] Implementar navegador próprio para as pastas do USB.
- [ ] Validar navegação e seleção de pasta no Xbox Series S.
- [ ] Enumerar uma biblioteca real a partir da pasta escolhida.

Nesta etapa, Jogos detecta o primeiro dispositivo removível e navega por suas
pastas, mas ainda não enumera jogos nem inicia títulos. A bridge inicial prova
que o mesmo MSIX compila e executa código da árvore upstream; ela não equivale
a portar todos os subsistemas do emulador para UWP.

## Fase 2 — Desacoplamento gráfico

- [ ] Introduzir contratos neutros do renderer.
- [ ] Remover handles Vulkan de `amdgpu`, `buffer_cache` e `texture_cache`.
- [ ] Manter o backend Vulkan funcional.
- [ ] Criar testes de contrato e trace replay.

## Fase 3 — Backend D3D12

- [ ] Device, queues, command lists e fences.
- [ ] Alocador de recursos e residency.
- [ ] Descriptors e root signatures.
- [ ] Barriers, copies, clears e resolves.
- [ ] Graphics/compute PSOs e cache persistente.
- [ ] Swapchain UWP.

## Fase 4 — Shaders

- [ ] SPIR-V para HLSL via SPIRV-Cross.
- [ ] HLSL para DXIL via DXC.
- [ ] Reflection e remapeamento de bindings.
- [ ] Corpus golden Vulkan/D3D12.
- [ ] Avaliar emissor HLSL direto a partir do IR.

## Fase 5 — Plataforma e primeiro frame

- [ ] Filesystem e importação de conteúdo.
- [ ] Áudio e gamepad.
- [ ] Threads, TLS e tratamento de exceções.
- [ ] Persistência de configurações e caches.
- [ ] Processar um command buffer PM4 mínimo.
- [ ] Apresentar o primeiro frame.
