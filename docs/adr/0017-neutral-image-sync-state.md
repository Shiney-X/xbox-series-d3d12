# ADR 0017 — Rastreamento neutro de transições de imagens

- Status: aceito
- Data: 2026-09-28

## Contexto

O cache mantinha `vk::ImageLayout`, máscaras de acesso e estágios Vulkan como
estado de cada backing image. A mesma função decidia quais mips/layers
precisavam de transição e montava barriers Vulkan. Essa lógica não era
reutilizável por um futuro backend D3D12.

## Decisão

Mover o rastreamento para `VideoCore::ImageSyncState`, com layouts, acessos e
estágios semânticos. O componente aceita transições totais ou parciais e
devolve descrições contendo estado anterior, próximo estado e subrecursos.
`Image::GetBarriers` faz a tradução bidirecional entre a API Vulkan atual e
essas descrições, emitindo os mesmos `vk::ImageMemoryBarrier2`.

Preservar a política anterior de hazards: escrita de transferência, shader ou
memória força barrier repetida; alteração apenas de estágio não. Alterar essas
regras para attachments exige validação separada e não faz parte deste corte.

## Consequências

- O estado por backing image e por subrecurso já não contém tipos Vulkan.
- Chamadores de `Image::Transit` ainda usam parâmetros Vulkan; uma interface
  neutra de comandos de transição será tratada na fronteira de backend.
- A lista de transições usa armazenamento inline no caso comum, e testes cobrem
  transições totais, parciais e escritas repetidas.
- Não há backend D3D12 nem alteração no MSIX do Xbox nesta decisão.
