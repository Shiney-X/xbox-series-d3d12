# ADR-0013: descrição neutra da geometria de image views

## Estado

Aceito como corte da Fase 2E.

## Contexto

`ImageViewInfo` guarda dados do guest junto de `vk::Format` e
`vk::ComponentMapping`. O swizzle nasce em `AmdGpu::Image::DstSelect`, mas era
traduzido para Vulkan antes de o descritor da view ser armazenado no cache.

## Decisão

Introduzir `VideoCore::ImageViewDesc` com tipo de imagem, faixa de
subrecursos, swizzle do guest, LOD mínimo e intenção storage. `ImageViewInfo`
herda esses campos e conserva o formato Vulkan exigido pela implementação
atual. O backend Vulkan traduz o swizzle ao criar `vk::ImageView`.

O swizzle padrão passa a ser explicitamente RGBA; isso equivale ao
`vk::ComponentMapping` padrão anterior, que selecionava cada componente por
identidade. A captura de tela continua forçando o alfa para um.

## Consequências

- A geometria e o swizzle da view podem ser consumidos por outro backend sem
  incluir tipos Vulkan.
- O backend Vulkan mantém a seleção de formato e a criação da view.
- Compatibilidade de formatos e recursos de imagem completos continuam fora
  deste corte.
