# ADR 0026 — Recurso de buffer nas transferências de imagem

- Status: aceito como quarto corte da Fase 2K
- Data: 2026-09-29

## Contexto

Mesmo após a posse de buffers passar a `Vulkan::BufferResource`, upload,
download, cópia intermediada e tiling ainda transportavam `vk::Buffer` pelas
assinaturas de `Image` e `TileManager`. O detiling também criava um buffer
temporário com VMA diretamente no cache de textura.

## Decisão

Essas operações recebem referências ao proprietário `BufferResource`. A
extração do handle ocorre somente no código que emite comandos Vulkan.
`TileManager` cria o scratch pelo mesmo proprietário, preserva as flags de uso
e a política anterior de alocação sem limite de orçamento, e adia sua liberação
até o tick da GPU. Para o resultado de detiling, devolve uma referência não
proprietária cujo tempo de vida é garantido pelo buffer original ou pela
operação adiada do scheduler.

## Consequências

- As entradas de transferência de `Image` não expõem mais `vk::Buffer`.
- O buffer temporário não exige destruição VMA manual no cache de textura.
- A regra de vida útil depende do scheduler, como antes; regressão Vulkan com
  jogo continua necessária.
- `BufferCache`, `Image` e `TileManager` ainda incluem tipos de barrier,
  comandos e formatos Vulkan. Isto não encerra a 2K nem implementa D3D12.
