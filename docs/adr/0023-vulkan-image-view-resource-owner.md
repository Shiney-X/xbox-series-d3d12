# ADR 0023 — Posse da image view no adaptador Vulkan

- Status: aceito como primeiro corte da Fase 2K
- Data: 2026-09-29

## Contexto

`VideoCore::ImageView`, mantida pelo texture cache, possuía diretamente um
`vk::UniqueImageView`. O rasterizer e o presenter extraíam o handle do campo
público. Isso deixava a posse de um recurso nativo dentro da entrada do cache.

## Decisão

Mover a criação e a posse do handle para `Vulkan::ImageViewResource`. A entrada
`ImageView` mantém seus metadados e um `unique_ptr` ao recurso do adaptador.
O renderer Vulkan obtém o handle por `Native().Handle()`. A criação da view,
incluindo formato, aspecto, swizzle, min LOD e nome de debug, conserva a lógica
anterior.

## Consequências

- O cache não expõe mais `vk::UniqueImageView` como membro público, e a posse
  do handle fica em `renderer_vulkan`.
- Há uma alocação pequena por view para o objeto do adaptador; a transferência
  de posse continua por move, sem cópia do handle.
- `ImageViewInfo` ainda contém `vk::Format`, e outros handles Vulkan seguem nos
  caches. Portanto este é só o primeiro corte do 2K, não um backend D3D12.
- Compilação e teste de tipo verificam a fronteira; regressão visual Vulkan
  continua necessária antes de considerar o renderer validado.
