# Roadmap

## Fase 0 — Viabilidade UWP

- [x] Criar o scaffold do repositório.
- [x] Implementar probe Win32 de capabilities.
- [x] Implementar probe Win32 de memória virtual.
- [x] Implementar probe Win32 de memória executável.
- [x] Adicionar template de manifest com `codeGeneration`.
- [ ] Criar host UWP x64 para executar os mesmos probes.
- [ ] Implantar e executar no Xbox Series S sem debugger.
- [ ] Implementar aliases e placeholders no probe de memória.
- [ ] Implementar triângulo D3D12 e compilação DXC em runtime.
- [ ] Medir pressão de memória, suspensão e retomada.

### Critério de saída

A fase termina somente quando os resultados brutos do console estiverem
registrados. Falha irrecuperável de mapeamento ou execução bloqueia o port UWP.

## Fase 1 — Integração do upstream

- [ ] Definir estratégia de importação preservando histórico Git.
- [ ] Registrar revisão upstream de referência.
- [ ] Obter build Windows desktop sem regressões.
- [ ] Isolar frontend/UI desktop do host UWP.

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
