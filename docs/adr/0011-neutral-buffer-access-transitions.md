# ADR-0011: transições de acesso a buffers independentes da API gráfica

## Estado

Aceito como corte da Fase 2C.

## Contexto

`VideoCore::Buffer::GetBarrier` mantinha o acesso e o estágio anteriores como
tipos Vulkan. Isso misturava a decisão de que houve uma mudança de uso com a
construção da barrier nativa. O futuro backend D3D12 precisa observar a mesma
sequência de usos do guest, mas produzir sua própria sincronização.

## Decisão

Introduzir `VideoCore::BufferAccess` e `VideoCore::BufferSyncState`. O estado
neutro compara acessos consecutivos e devolve uma transição somente quando o
uso muda. A transição carrega estado anterior, estado seguinte, offset e
tamanho até o fim do buffer. Offsets fora do buffer não alteram o estado.

O buffer Vulkan traduz cada estado para os mesmos pares de máscara de acesso
e estágio que eram usados antes da mudança. O estado inicial preserva a
máscara ampla de leitura/escrita e transferência da implementação anterior.

## Consequências

- A ordem das transições pode ser testada sem SDK gráfico.
- O backend Vulkan mantém a construção de `vk::BufferMemoryBarrier2`.
- Ainda não existe alocador neutro de buffers, nem mapeamento D3D12 de estados.
- Hazards de imagens e sincronização entre queues permanecem fora deste corte.
