# ADR 0029 — Gerenciador de faults no renderer Vulkan

- Status: aceito como sétimo corte da Fase 2K
- Data: 2026-09-29

## Contexto

`FaultManager` estava em `buffer_cache`, embora possuísse um pipeline compute,
layout de descriptors e emissão de comandos exclusivamente Vulkan. Isso
misturava a política de cache com a implementação do backend.

## Decisão

Mover o gerenciador para `renderer_vulkan` e para o namespace `Vulkan`. O
`BufferCache` passa a guardá-lo por ponteiro proprietário opaco e continua
chamando a mesma operação e recebendo o mesmo buffer de faults. A ordem de
construção, leitura de faults e sincronização não muda.

## Consequências

- A posse dos objetos Vulkan de faults fica explicitamente no renderer.
- Não há mudança esperada no comportamento de rasterização ou no formato dos
  dados de faults.
- `BufferCache` ainda depende do gerenciador Vulkan e expõe outras APIs
  nativas. Isto não encerra a 2K nem implementa D3D12.
