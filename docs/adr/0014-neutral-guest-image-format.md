# ADR 0014 — Preservar o formato original das imagens do guest

- Status: aceito
- Data: 2026-09-27

## Contexto

`ImageInfo` guardava apenas `vk::Format`. Essa conversão perde a distinção entre
certos formatos do guest e obriga um backend futuro a reconstruir a origem do
recurso a partir de um formato Vulkan. Em particular, a conversão atual de
VideoOut mapeia dois formatos sRGB para o mesmo formato RGBA interno.

## Decisão

Adicionar `VideoCore::ImageFormatDesc`, uma variante neutra com os casos
surface, depth/stencil e VideoOut, além do estado vazio para imagens auxiliares.
O caso surface registra data format, number format e a intenção de reinterpretar
como depth. Cada construtor de `ImageInfo` preenche esse descritor antes de
resolver seu `vk::Format` pelo mapeamento Vulkan já usado. O comportamento
observável do renderer Vulkan não muda neste corte.

## Consequências

- Um backend D3D12 terá acesso aos códigos originais do guest, sem depender de
  uma conversão reversa de `vk::Format`.
- `pixel_format`, `IsCompatible`, `ImageViewInfo` e a alocação ainda dependem de
  Vulkan. A retirada desses vínculos requer decisões próprias sobre
  compatibilidade, typeless resources, swizzles e formatos de view D3D12.
- Este ADR não afirma suporte a jogos, compilação UWP nem equivalência entre
  formatos Vulkan e DXGI; a validação atual é compilação dos objetos afetados.
