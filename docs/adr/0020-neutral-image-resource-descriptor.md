# ADR 0020 — Descritor neutro para criação de imagens

- Status: aceito como terceiro corte da Fase 2J
- Data: 2026-09-28

## Contexto

O `TextureCache` enviava `ImageInfo` diretamente para `Image` na criação do
recurso. Essa estrutura reúne metadados do guest com `vk::Format`, enquanto a
alocação precisa de formato original, dimensões, subrecursos, usos e número de
amostras. A fronteira de criação de buffers já recebe `BufferDesc` neutro;
faltava o equivalente para imagens.

## Decisão

Introduzir `ImageResourceDesc` sem tipos Vulkan. `ImageInfo::ResourceDesc()`
gera esse pedido nas quatro rotas de criação do cache, inclusive recriação,
expansão e imagem auxiliar de stencil. O adaptador `Image` recebe o pedido
separadamente de `ImageInfo` e o usa para escolher formato suportado, flags,
usage, aspecto, tamanho, subrecursos e amostras Vulkan. Uma imagem auxiliar sem
formato mantém o comportamento anterior: não aloca backing nativo.

## Consequências

- A decisão de quais propriedades solicitar ao recurso é explícita na
  fronteira cache → adaptador; a tradução para Vulkan permanece no adaptador.
- `ImageInfo` continua presente por causa de metadados, compatibilidade,
  tracking e cópias; ele ainda contém `vk::Format`. O 2J não está concluído.
- O descritor não é um alocador D3D12, não introduz residency nem remove os
  handles Vulkan do cache. Essas partes exigem cortes posteriores.
- Teste unitário cobre um pedido válido e o caso auxiliar sem formato; regressão
  visual do renderer Vulkan continua necessária antes de considerar o corte
  validado em jogo.
