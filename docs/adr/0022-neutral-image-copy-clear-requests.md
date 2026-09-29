# ADR 0022 — Pedidos neutros para cópia de região e clear de imagem

- Status: aceito como fechamento de implementação da Fase 2J
- Data: 2026-09-29

## Contexto

Depois das transições, criação e transferências buffer/imagem neutras, o
`DepthStencilCopy` do rasterizer ainda montava `vk::ImageCopy` diretamente, e
`Image::Clear` recebia `vk::ClearValue`. Esses caminhos impediam a fronteira
de comandos de imagem de expressar pedidos sem tipos Vulkan.

## Decisão

Adicionar `ImageCopyRequest` com subrecursos, aspecto e extensão, e
`ColorClearRequest` com os bits de cada componente. `Rasterizer` envia os
pedidos a `Image::CopyRegion` e `Image::Clear`. O adaptador `Image` faz as
transições necessárias e traduz os pedidos para comandos Vulkan. Os bits do
clear são preservados para formatos inteiros e de ponto flutuante.

## Consequências

- As operações essenciais de imagem cruzam a fronteira por pedidos neutros:
  criação, transição, transferência, cópia de região e clear.
- Operações internas de `Image` podem continuar usando Vulkan. Isolar handles
  Vulkan nos caches é o trabalho da Fase 2K, não desta decisão.
- A implementação do 2J está fechada no escopo acordado; a regressão do
  renderer Vulkan e o trace replay pertencem à Fase 2L. Isso não prova boot
  de jogo no Xbox nem implementa o backend D3D12.
