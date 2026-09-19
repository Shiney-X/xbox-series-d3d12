# ADR-0005: bridge incremental do core e shell UWP nativo

- **Status:** aceito
- **Data:** 2026-09-19

## Contexto

O probe da Fase 0 provou D3D12, DXIL, memória executável e ciclo de vida no
Xbox Series S, mas o MSIX ainda não continha nenhum código do shadPS4 upstream.
Compilar o emulador inteiro como UWP em uma única alteração esconderia quais
dependências e APIs falham no sandbox. A interface também precisa existir no
console desde o início, sem depender do frontend Qt/SDL de desktop.

## Decisão

O host Xbox permanece um aplicativo UWP x64 `CoreApplication` e desenha sua
interface diretamente pelo renderer D3D12 já validado. A primeira bridge inclui
e valida tipos reais do core upstream: wrappers endian e estruturas PSF. Seu
estado é apresentado na tela e persistido em `phase1-core.jsonl`.

O shell expõe inicialmente três seções:

- **Jogos**, futura biblioteca e ponto de inicialização dos títulos;
- **Configurações**, futura persistência das opções compatíveis;
- **Diagnósticos**, estado da bridge, host UWP, D3D12/DXIL e probes.

A navegação usa os `VirtualKey` de gamepad fornecidos por `CoreWindow`. Nesta
etapa não há Qt, SDL, WebView nem versão mock para desktop.

## Consequências

- Cada incremento passa a ser testável no Xbox dentro do pacote que evoluirá
  para o port, reduzindo o risco de uma migração monolítica.
- A bridge inicial prova compatibilidade de compilação e ABI de uma fatia do
  upstream; não significa que loader, kernel, JIT ou video core já sejam UWP.
- O texto usa um atlas 5x7 desenhado por retângulos D3D12 para evitar adicionar
  uma dependência gráfica antes de estabilizar o host.
- O próximo incremento será o seletor UWP de pasta e a persistência do token de
  acesso, seguido pela enumeração de metadados de jogos.
