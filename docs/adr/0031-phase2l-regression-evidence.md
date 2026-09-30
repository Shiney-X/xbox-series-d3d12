# ADR 0031 — Evidência de regressão da Fase 2L

- Status: proposto; validação visual pendente
- Data: 2026-09-30

## Contexto

A Fase 2K retirou tipos Vulkan dos cabeçalhos de cache, mas compilação não
prova que as imagens apresentadas permanecem iguais. Não há backend D3D12
funcional nem runner de jogos na CI capaz de produzir um frame determinístico.

## Decisão

- Manter um trace sintético, versionado e reproduzido pelos contratos neutros
  de sincronização em teste C++ independente de renderer. O golden registra
  transições concretas, inclusive pedidos sem barrier e escrita repetida.
- Fornecer um comparador PNG sem dependências externas, com métricas e limites
  explícitos, para capturas **game-only** de builds Vulkan antes/depois da 2K.
- Exigir evidência manual de um título real antes de declarar a regressão visual
  concluída. A CI verifica a ferramenta de comparação, não substitui a captura.

## Consequências

O trace protege a semântica compartilhada, mas não é captura de PM4 de jogo.
O comparador é propositalmente estrito e requer cenas alinhadas; pixels
animados e diferenças de resolução não são aprovação automática. A Fase 2L
permanece aberta até a evidência visual ser revista. Nenhum MSIX precisa ser
reinstalado para esta validação desktop.
