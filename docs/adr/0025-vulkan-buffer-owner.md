# ADR 0025 — Posse do buffer no adaptador Vulkan

- Status: aceito como terceiro corte da Fase 2K
- Data: 2026-09-29

## Contexto

`VideoCore::UniqueBuffer` possuía diretamente `vk::Buffer`, `VmaAllocation`
e endereço de GPU. `StreamBuffer` chamava funções VMA para sincronizar faixas
de memória mapeada. Essa posse nativa ainda residia no cache de buffers.

## Decisão

Mover criação, destruição, mapeamento, consulta de coerência, endereço de GPU,
flush e invalidação para `Vulkan::BufferResource`. `VideoCore::Buffer` guarda
um `unique_ptr` opaco e delega a ele essas operações. O descritor neutro
`BufferDesc` continua sendo a entrada de criação.

## Consequências

- A alocação VMA e seu ciclo de vida ficam em `renderer_vulkan`.
- A criação conserva as mesmas flags de uso, preferência de memória e regra
  para buffer device address. Há uma pequena alocação adicional por buffer.
- `Buffer::Handle()` e outras APIs dos caches ainda usam `vk::Buffer` ou tipos
  de barrier. Este corte **não conclui** o isolamento da Fase 2K e não adiciona
  D3D12. A validação visual Vulkan continua necessária na Fase 2L.
