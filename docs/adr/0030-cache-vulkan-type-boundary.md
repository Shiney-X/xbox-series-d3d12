# ADR 0030 — Fronteira de tipos nativos dos caches

- Status: aceito como implementação de fechamento da Fase 2K
- Data: 2026-09-29

## Contexto

Após a posse de recursos nativos ter sido extraída, os cabeçalhos de
`buffer_cache` e `texture_cache` ainda declaravam `vk::Buffer`, `vk::Format`,
`vk::ImageLayout`, listas de barriers e `vk::CommandBuffer`. Isso obrigava os
consumidores da política de cache a conhecer tipos da API gráfica ativa.

## Decisão

- `Buffer` expõe o proprietário Vulkan por referência opaca, não o handle nem
  o endereço de dispositivo em tipos Vulkan. O cache devolve pedidos de
  barrier expressos como buffer e `BufferTransition`; o renderer os traduz.
- `ImageInfo` e `ImageViewInfo` guardam a identidade do formato do guest. A
  conversão de formatos e a tabela de compatibilidade Vulkan ficam no
  adaptador. Views VideoOut usam uma conversão de apresentação própria.
- O estado nativo da imagem (aspect, amostras suportadas, uso e capacidades)
  reside em `Vulkan::ImageNativeState`. `Image` entrega layout e transições
  sem tipos Vulkan; `vk_image_barrier` os traduz e emite barriers.
- A emissão existente de cópias e clears continua nas implementações atuais.
  Este passo altera a fronteira de tipos, não cria um backend D3D12.

## Consequências

- Nenhum cabeçalho de `amdgpu`, `buffer_cache` ou `texture_cache` declara
  `vk::`. A implementação Vulkan ainda participa da criação dos objetos e
  precisa ser separada por interfaces de backend nas fases seguintes.
- A chave de view usa o formato original do guest. Dois formatos diferentes
  que antes mapeavam para uma mesma view Vulkan podem agora criar duas views.
  Isso evita colocar uma chave de formato nativo no contrato compartilhado,
  ao custo de possível memória/tempo adicional no cache de views.
- A equivalência visual e a regressão de jogos precisam ser verificadas na
  Fase 2L; compilação e testes de contrato não provam equivalência de frames.
