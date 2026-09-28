# ADR 0019 — Pedidos semânticos de transição de imagens

- Status: aceito como segundo corte da Fase 2J
- Data: 2026-09-28

## Contexto

O primeiro corte da Fase 2J permitiu ao cache pedir transições por estado
semântico, mas o rasterizer, o presenter e as operações internas de imagem
continuavam chamando `Image::Transit` com layouts e máscaras Vulkan. A
conversão reversa Vulkan → estado neutro ocorria dentro do adaptador.

## Decisão

Usar `ImageResourceState` em todos os pedidos de `Transit` e `GetBarriers` do
video core. Estados comuns de leitura, escrita e transferência são constantes
em `ImageStates`; os casos específicos declaram layout, acesso e estágio
explicitamente. Remover as sobrecargas de entrada Vulkan e a conversão
reversa. `Image` continua traduzindo os estados para barriers Vulkan.

Preservar os estágios anteriormente usados: pedidos de transferência usam
`Transfer`; os demais pedidos de `Transit` usam `GraphicsAndCompute`. Os
pedidos de `GetBarriers` usados por cópias e pelo blit de backing mantêm seus
estágios `Copy` e `FragmentShader`.

## Consequências

- O contrato de **pedido de transição** já não exige tipos Vulkan nos
  chamadores. O adaptador ainda produz `vk::ImageMemoryBarrier2`.
- Criação e cópias de recursos, views, scheduler e handles continuam nativos;
  esse corte não conclui o 2J nem inicia o backend D3D12.
- A compilação dos arquivos alterados e os testes de estado não substituem a
  regressão visual do renderer Vulkan. A nova PR precisa ser testada no Linux;
  nenhum teste de jogo no Xbox decorre desta mudança.
