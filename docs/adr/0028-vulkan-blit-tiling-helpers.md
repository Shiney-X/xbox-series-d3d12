# ADR 0028 — Helpers de blit e tiling no renderer Vulkan

- Status: aceito como sexto corte da Fase 2K
- Data: 2026-09-29

## Contexto

`BlitHelper` e `TileManager` residiam em `texture_cache`, mas possuíam
pipelines, layouts, módulos de shader e lógica de comandos exclusivamente
Vulkan. O cabeçalho de `TextureCache` incluía seus tipos concretos por valor.

## Decisão

Mover as implementações para `renderer_vulkan` e as classes para o namespace
`Vulkan`. `TextureCache` mantém `unique_ptr` opacos a esses helpers e passa
referências somente aos caminhos que efetivamente emitem operações Vulkan.
Não mudar a ordem nem a semântica dos comandos de blit ou tiling.

## Consequências

- A posse de pipelines, layouts e shaders desses dois helpers deixa o
  cabeçalho do cache.
- Há duas alocações pequenas adicionais por instância de `TextureCache`.
- A implementação de `Image` e outras APIs dos caches ainda incluem tipos
  Vulkan. Este corte não encerra a Fase 2K nem fornece D3D12; regressão
  visual Vulkan continua necessária.
