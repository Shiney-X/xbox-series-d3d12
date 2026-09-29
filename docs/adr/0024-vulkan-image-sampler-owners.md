# ADR 0024 — Posse de imagens e samplers no adaptador Vulkan

- Status: aceito como segundo corte da Fase 2K
- Data: 2026-09-29

## Contexto

`Image::BackingImage` continha um `UniqueImage` com `vk::Image` e alocação VMA.
`Sampler` possuía `vk::UniqueSampler` e `TextureCache::GetSampler` devolvia o
handle nativo. A posse de recursos Vulkan ainda atravessava diretamente as
entradas dos caches.

## Decisão

Mover a alocação e liberação de imagens para `Vulkan::ImageResource`, que também
é usado pelo passe FSR. `Image::BackingImage` guarda um `unique_ptr` ao recurso;
`Image::Native()` fornece ao adaptador a imagem ativa. Mover a criação do
sampler para `Vulkan::SamplerResource`; a entrada do cache guarda um
`shared_ptr` opaco e `TextureCache::GetSampler` devolve uma referência com
lifetime estável para o rasterizer.

## Consequências

- A posse de `vk::Image`/VMA e `vk::UniqueSampler` fica em `renderer_vulkan`.
- Os caminhos de criação e as opções nativas permanecem equivalentes. Há uma
  alocação pequena por imagem e por sampler para os objetos do adaptador.
- `ImageInfo` ainda guarda formato Vulkan e várias APIs dos caches ainda
  recebem buffers, barreiras ou comandos Vulkan. Buffers e superfícies restantes
  pertencem ao próximo corte do 2K; isto não implementa D3D12.
- Teste de tipo e compilação protegem a fronteira; regressão visual Vulkan
  ainda é necessária para validar jogos.
