# ADR-0009: contrato neutro de coerência de memória da GPU

## Estado

Aceito como primeiro corte da Fase 2A.

## Contexto

`Core::MemoryManager` e `VideoCore::PageManager` dependiam diretamente de
`Vulkan::Rasterizer`. Essa dependência levava um tipo do backend até o núcleo de
memória, embora esses componentes usem somente endereços virtuais do guest para
notificar mapeamentos, desmapeamentos e faults de página.

Copiar toda a superfície do rasterizer para uma interface comum criaria uma API
abstrata grande demais antes de conhecermos as necessidades do D3D12. Também
misturaria coerência de memória com comandos de draw, dispatch e apresentação.

## Decisão

Introduzir `VideoCore::GpuMemoryTracker`, um contrato pequeno expresso apenas em
endereços e tamanhos do guest. Ele cobre:

- invalidação e leitura de memória observada pela GPU;
- consulta de uma faixa mapeada;
- notificações de mapeamento e desmapeamento.

O rasterizer Vulkan implementa esse contrato. `MemoryManager` e `PageManager`
dependem somente da interface neutra e deixam de incluir o cabeçalho Vulkan.
Nenhum handle `vk::*` ou `ID3D12*` faz parte do contrato.

## Consequências

- Um futuro backend D3D12 pode fornecer seu próprio rastreador sem alterar o
  gerenciador de memória do guest.
- O comportamento do backend Vulkan permanece inalterado; a mudança é somente
  de dependência e despacho virtual.
- Liverpool, caches, draw/dispatch e apresentação continuam acoplados ao Vulkan
  e serão separados em cortes posteriores da Fase 2.
- Um teste de contrato independente de API gráfica verifica herança, destrutor
  virtual e despacho das operações.
