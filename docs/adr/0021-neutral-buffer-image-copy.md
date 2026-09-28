# ADR 0021 — Regiões neutras para transferência buffer/imagem

- Status: aceito como quarto corte da Fase 2J
- Data: 2026-09-28

## Contexto

Os caches de textura e buffer e o gerenciador de tiling montavam regiões
`vk::BufferImageCopy` para upload e download. Isso obrigava cada produtor de
pedidos de transferência a conhecer o formato de comandos Vulkan.

## Decisão

Introduzir `ImageBufferCopy`, que descreve offset, pitch, altura, mip, camadas
e extensão sem tipos gráficos nativos. Os caches e o gerenciador de tiling
passam essa estrutura a `Image::Upload` ou `Image::Download`. O adaptador
`Image` escolhe o aspect mask a partir da imagem e converte as regiões para
Vulkan antes de gravar o comando. O readback direto do cache de textura também
usa `Image::Download`.

## Consequências

- Os pedidos buffer/imagem atravessam uma fronteira independente da API.
- O handle `vk::Buffer` e as barreiras continuam específicos de Vulkan. A
  remoção desses handles pertence à Fase 2K.
- Cópias entre imagens e clears ainda não cruzam uma interface neutra; a
  Fase 2J continua em andamento.
- O teste unitário valida o descritor; a regressão visual no renderer Vulkan
  ainda precisa ser conferida antes de declarar a mudança validada em jogo.
