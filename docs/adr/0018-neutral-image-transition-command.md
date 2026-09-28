# ADR 0018 — Comando neutro de transição de imagem

- Status: aceito como primeiro corte da Fase 2J
- Data: 2026-09-28

## Contexto

`ImageSyncState` já rastreia transições sem tipos Vulkan, mas os chamadores do
cache ainda precisavam expressar o estado desejado como layout e acesso Vulkan.
Isso impedia que a decisão de transição do cache fosse reutilizada por outro
backend.

## Decisão

Expor `Image::Transit(ImageResourceState, range)` e
`Image::GetBarriers(ImageResourceState, range)`. As operações de download e de
resolução de overlap do `TextureCache` passam a solicitar estados semânticos.
`Image` continua sendo o adaptador Vulkan: traduz a transição para barriers e
submete os comandos nativos. A assinatura Vulkan antiga é mantida para os
demais chamadores e delega à assinatura neutra.

## Consequências

- O caminho alterado mantém os mesmos layouts, acessos e estágios Vulkan.
- Este é apenas um corte inicial do 2J: cópias, criação de recursos, handles,
  views e a maioria dos chamadores ainda dependem de Vulkan.
- O teste visual com Deltarune no Linux foi relatado para a `main` anterior a
  este corte; a nova implementação ainda requer teste de regressão próprio.
- Não há backend D3D12 do emulador nem boot de jogo no Xbox nesta decisão.
