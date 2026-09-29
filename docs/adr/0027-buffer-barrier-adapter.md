# ADR 0027 — Tradução de barriers de buffer no adaptador Vulkan

- Status: aceito como quinto corte da Fase 2K
- Data: 2026-09-29

## Contexto

`BufferSyncState` já decidia transições com estados neutros, porém
`VideoCore::Buffer::GetBarrier` ainda traduzia esses estados para acessos e
estágios Vulkan e devolvia `vk::BufferMemoryBarrier2`.

## Decisão

`Buffer::Transition` passa a devolver apenas `BufferTransition`. O adaptador
`Vulkan::GetBufferBarrier` obtém essa decisão, traduz os estados exatamente
como antes e produz a barrier para os chamadores Vulkan existentes.

## Consequências

- A política de transição permanece no cache; o mapeamento de estágios e
  acessos fica no renderer Vulkan.
- Os chamadores preservam a sequência anterior de transições e comandos.
- Os caches ainda montam e submetem barriers Vulkan em outros pontos. Este
  corte não conclui a Fase 2K e não implementa D3D12. A regressão visual
  Vulkan continua necessária.
