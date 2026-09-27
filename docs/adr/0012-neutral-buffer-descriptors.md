# ADR-0012: descritores neutros para criação de buffers

## Estado

Aceito como corte da Fase 2D.

## Contexto

O construtor de `VideoCore::Buffer` recebia `vk::BufferUsageFlags`. Portanto,
mesmo a decisão sobre quais usos o emulador exige era expressa na API Vulkan.
Para um backend D3D12, a intenção de uso precisa chegar antes da escolha de
flags e estados específicos do backend.

## Decisão

Introduzir `VideoCore::BufferDesc` com preferência de memória, endereço do
guest, tamanho e combinação de `BufferUsage`. A criação de buffers do cache,
do gerenciador de faults e de captura de tela passa esse descritor. O buffer
Vulkan converte os usos para os mesmos `vk::BufferUsageFlagBits` anteriores.

`BufferUsage::DeviceAddress` preserva o pedido explícito de BDA feito pelos
buffers do guest. Buffers utilitários usam endereço do guest igual a zero.

## Consequências

- Chamadores deixam de escolher flags Vulkan ao criar `VideoCore::Buffer`.
- A implementação Vulkan continua usando VMA e mantém os usos existentes.
- O descritor não define alocação, residency, views, imagens ou mapeamento
  D3D12; esses itens continuam como próximos cortes da Fase 2 e Fase 3.
