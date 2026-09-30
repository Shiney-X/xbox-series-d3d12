# Roadmap

## Fase 0 — Viabilidade UWP

**Status: concluída no Xbox Series S em 19 de setembro de 2026.**

- [x] Criar o scaffold do repositório.
- [x] Implementar probe Win32 de capabilities.
- [x] Implementar probe Win32 de memória virtual.
- [x] Implementar probe Win32 de memória executável.
- [x] Implementar probe de dispositivo D3D12.
- [x] Adicionar template de manifest com `codeGeneration`.
- [x] Criar host UWP x64 para executar os mesmos probes.
- [x] Gerar pacote de sideload assinado no CI.
- [x] Implantar e executar no Xbox Series S sem debugger.
- [x] Implementar aliases e placeholders no probe de memória.
- [x] Validar aliases e placeholders no Xbox Series S em perfil Game.
- [x] Apresentar um clear D3D12 por swapchain UWP.
- [x] Implementar triângulo D3D12 com root signature e PSO.
- [x] Validar o triângulo D3D12 no Xbox Series S em perfil Game.
- [x] Substituir o probe DXBC/Shader Model 5 por DXC/DXIL em runtime.
- [x] Validar DXC/DXIL no Xbox Series S em perfil Game.
- [x] Implementar medição controlada de pressão de memória.
- [x] Instrumentar suspensão e retomada com reapresentação D3D12.
- [x] Validar pressão de memória no Series S em perfil Game.
- [x] Validar suspensão, retomada no mesmo processo e nova apresentação D3D12.
- [x] Confirmar tela verde após tratar `IDXGIDevice3::Trim` como capability opcional.

### Critério de saída

A fase termina somente quando os resultados brutos do console estiverem
registrados. Falha irrecuperável de mapeamento ou execução bloqueia o port UWP.

O critério foi cumprido em perfil **Game** no Xbox Series S com sistema
`10.0.26100.9426`. CPU, memória virtual, aliases, código executável, limite de
5 GiB, D3D12/DXIL, apresentação, persistência e suspensão/retomada passaram.
Consulte [`docs/results`](results/README.md).

## Fase 1 — Integração do upstream

**Status: integração base, shell Xbox, biblioteca USB e metadados de jogo
validados no console; imagens da biblioteca em desenvolvimento.**

- [x] Definir estratégia de importação preservando histórico Git.
- [x] Registrar revisão upstream de referência.
- [x] Integrar `v.0.18.0` na raiz e resolver colisões da Fase 0.
- [x] Obter build Windows desktop sem regressões.
- [x] Isolar frontend/UI desktop do host UWP.

### Fase 1B — Interface inicial no Xbox

- [x] Criar uma bridge UWP compilada contra tipos reais do core upstream.
- [x] Substituir a tela do triângulo por um shell D3D12 nativo.
- [x] Adicionar navegação por direcional, A e B.
- [x] Exibir seções Jogos, Configurações e Diagnósticos.
- [x] Persistir o resultado da bridge em `LocalState/phase1-core.jsonl`.
- [x] Validar desenho, navegação e retomada no Xbox Series S em perfil Game.

### Fase 1C — Acesso USB à biblioteca no Xbox

- [x] Medir `FolderPicker` com e sem usuário e USB no Xbox.
- [x] Registrar que o seletor abre, mas não enumera nenhuma origem no console.
- [x] Substituir o seletor por `KnownFolders.RemovableDevices`.
- [x] Detectar assincronamente o primeiro dispositivo removível.
- [x] Expor estados de varredura e acesso USB na tela Games.
- [x] Persistir diagnóstico em `LocalState/phase1-library.jsonl`.
- [x] Validar `KnownFolders.RemovableDevices` no Xbox Series S.
- [x] Implementar navegador próprio para as pastas do USB.
- [x] Validar navegação e seleção de pasta no Xbox Series S.
- [x] Persistir e restaurar a pasta escolhida por caminho relativo ao USB.

### Fase 1D — Descoberta de jogos

- [x] Identificar candidatos por `eboot.bin` e `sce_sys/param.sfo`.
- [x] Interpretar `TITLE`, `TITLE_ID` e `APP_VER` com estruturas PSF do core.
- [x] Limitar profundidade, diretórios e quantidade de resultados.
- [x] Exibir a lista textual de jogos no shell D3D12.
- [x] Persistir o resultado em `LocalState/phase1-library-scan.jsonl`.
- [x] Validar um dump próprio extraído no Xbox Series S.

### Fase 1E — Imagens da biblioteca

- [x] Localizar `sce_sys/icon0.png` sem tornar o arquivo obrigatório.
- [x] Limitar o PNG codificado, dimensões de origem e saída BGRA8.
- [x] Decodificar com `Windows.Graphics.Imaging` para no máximo 256×256.
- [x] Criar SRV, upload buffer e textura D3D12 para o jogo selecionado.
- [x] Apresentar fallback quando o ícone estiver ausente ou inválido.
- [x] Persistir diagnóstico em `LocalState/phase1-library-icons.jsonl`.
- [x] Validar o ícone de um dump próprio no Xbox Series S.

A Fase 1E foi validada em 27 de setembro de 2026 no Series S em perfil
**Game**. O `icon0.png` de Sonic Mania foi reduzido para 256×256, enviado para
a textura selecionada e apresentado com `selected_icon_presented=true`. O
journal confirmou suspensão e retomada sem falha.

Nesta etapa, Jogos detecta o primeiro dispositivo removível, navega por suas
pastas e extrai metadados de dumps reconhecidos, mas ainda não inicia títulos.
A bridge inicial prova que o mesmo MSIX compila e executa código da árvore
upstream; ela não equivale a portar todos os subsistemas do emulador para UWP.

## Fase 2 — Desacoplamento gráfico

**Status: Fase 2J implementada; Fase 2K em andamento (sétimo corte).**

- [x] Introduzir o primeiro contrato neutro para coerência de memória GPU.
- [x] Remover `Vulkan::Rasterizer` de `Core::MemoryManager` e `PageManager`.
- [x] Adicionar teste unitário independente de Vulkan/D3D12 para o contrato.
- [x] Separar submissão PM4 de draw/dispatch do rasterizer Vulkan por um contrato
      de comandos expresso em valores do guest.
- [x] Extrair o estado de transição de buffers para um componente neutro,
      mantendo a emissão de barriers no backend Vulkan.
- [x] Descrever usos e preferências de alocação de buffers sem flags Vulkan,
      traduzindo-os somente na criação do recurso Vulkan.
- [x] Separar geometria e swizzle das image views em um descritor neutro.
- [x] Preservar a identidade de formato das imagens do guest antes da conversão Vulkan.
- [x] Preservar o formato original das image views e centralizar sua conversão Vulkan.
- [x] Descrever capacidades de uso das imagens sem flags Vulkan.
- [x] Separar o rastreamento de transições de imagens da emissão de barriers Vulkan.
- [x] Permitir que o cache solicite transições de imagem em estados neutros nos
      caminhos de download e resolução de overlap.
- [x] Remover os pedidos de transição Vulkan da API de `Image`, preservando a
      tradução de barriers no adaptador existente.
- [x] Passar um descritor neutro de criação de imagem entre o cache e o
      adaptador Vulkan.
- [x] Introduzir contratos neutros de recursos, comandos e sincronização para
      criação, transições, transferências, cópia de região e clear de imagens.
- [ ] Remover handles Vulkan de `amdgpu`, `buffer_cache` e `texture_cache`.
- [ ] Manter o backend Vulkan funcional.
- [ ] Criar testes de contrato e trace replay.

Na Fase 2B, `AmdGpu::Liverpool` entrega draw, dispatch, cópias, sincronização
e marcadores ao `VideoCore::GpuCommandSink`. O rasterizer Vulkan implementa o
contrato atual. A interface ainda não fornece descritores de recursos nem
pipelines para D3D12; esses elementos continuam como trabalho da Fase 2.

Na Fase 2C, `VideoCore::BufferSyncState` acompanha a sequência de acessos a um
buffer sem tipos Vulkan. O `Buffer` atual converte cada transição para
`vk::BufferMemoryBarrier2`. Isso ainda não remove handles Vulkan do cache nem
implementa barriers D3D12.

Na Fase 2D, `VideoCore::BufferDesc` representa endereço do guest, tamanho,
preferência de memória e usos de buffer de forma independente da API gráfica.
O buffer Vulkan converte os usos para `vk::BufferUsageFlags` durante a criação.
Imagens, views e alocação D3D12 ainda estão pendentes.

Na Fase 2E, `VideoCore::ImageViewDesc` contém tipo de view, faixa de mip/layers,
swizzle, LOD mínimo e uso storage sem tipos Vulkan. `ImageViewInfo` mantém o
formato `vk::Format` por enquanto; a criação da view converte o swizzle do
guest para Vulkan no último momento.

Na Fase 2F, `VideoCore::ImageFormatDesc` guarda o formato original de surfaces,
depth/stencil ou VideoOut, inclusive a intenção de reinterpretar uma surface
como depth. `ImageInfo` ainda conserva `vk::Format` para o backend atual e o
resolve a partir desse descritor. Compatibilidade, views e alocação de imagens
continuam dependentes do Vulkan; nenhum formato D3D12 foi implementado.

Na Fase 2G, `ImageViewDesc` também preserva o formato original do guest.
`LiverpoolToVK::ImageFormat` é o ponto comum de conversão para imagens e views,
incluindo a regra atual de views storage sRGB→UNORM. A chave do cache Vulkan
continua baseada no formato nativo e na geometria, para não duplicar views
equivalentes. Alocação e sincronização de imagens continuam específicas do Vulkan.

Na Fase 2H, `ImageUsage` descreve transferências, sampling, attachments e
storage sem tipos nativos. `CachedImageUsage` mantém a política atual do cache
para imagens coloridas, depth e comprimidas. A criação converte essas
capacidades para `vk::ImageUsageFlags`; o feedback loop continua condicionado
ao suporte do dispositivo Vulkan. Estado de recurso e barriers ainda são Vulkan.

Na Fase 2I, `ImageSyncState` acompanha estado de layout, acesso e estágio de
imagens inteiras ou subrecursos em termos neutros. Ele decide quando uma
transição é necessária; `Image::GetBarriers` traduz o resultado para
`vk::ImageMemoryBarrier2`. Os chamadores Vulkan ainda passam layouts e máscaras
nativos na API anterior de `Image::Transit`; o contrato completo de backend segue
para a Fase 2J. O rastreador mantém a regra anterior para escritas repetidas e
tem testes de transição total, parcial e mudança apenas de estágio.

No primeiro corte da Fase 2J, `TextureCache` envia `ImageResourceState` ao
adaptador `Image` para download e resolução de overlap. A API Vulkan antiga
delega ao mesmo caminho. Esse passo inicia a fronteira de comandos, mas ainda
não neutraliza cópias, handles, views ou todo o cache.

No segundo corte do 2J, rasterizer, presenter e operações internas também
pedem transições via `ImageResourceState`. A conversão reversa de layouts,
acessos e estágios Vulkan foi removida da entrada de `Image`. A saída Vulkan,
os handles e os comandos de cópia continuam no adaptador atual. Ainda falta a
fronteira de recursos e comandos além de transições para concluir o 2J.

No terceiro corte do 2J, o cache constrói `ImageResourceDesc` com o formato do
guest, dimensões, subrecursos, usos e amostras. O adaptador Vulkan resolve o
formato e as flags nativas na criação. `ImageInfo` ainda circula nessa
fronteira para metadados e ainda contém `vk::Format`; não é isolamento completo
de recursos nem implementação D3D12.

No quarto corte do 2J, upload e download de imagens passam regiões
`ImageBufferCopy` sem tipos Vulkan pelos caches de textura, buffer e tiling.
O adaptador `Image` traduz as regiões para `vk::BufferImageCopy` ao emitir os
comandos. Cópias entre imagens, clears e handles de buffers ainda usam Vulkan;
o 2J permanece em andamento.

O quinto corte encerra a implementação planejada do 2J: cópia de região
imagem/imagem e clear de cor passam pedidos `ImageCopyRequest` e
`ColorClearRequest` sem tipos Vulkan. O adaptador `Image` emite os comandos
Vulkan. `Image::CopyImage`, `CopyMip` e `Resolve` já eram pedidos sem regiões
Vulkan externas; sua lógica interna permanece no adaptador. Os handles nativos
nos caches, os comandos de buffers e a regressão Vulkan continuam para 2K/2L.

No primeiro corte do 2K, a posse de `vk::UniqueImageView` sai de
`texture_cache::ImageView` e passa a `Vulkan::ImageViewResource`. O cache guarda
um ponteiro opaco ao recurso nativo; apenas o renderer Vulkan extrai o handle
para bindings e apresentação. `ImageViewInfo` ainda contém `vk::Format`, e
imagens, buffers, samplers e outros caminhos do cache ainda expõem tipos
Vulkan. O 2K não está concluído.

No segundo corte do 2K, a imagem alocada por VMA e o sampler nativo passam a
ser possuídos por `Vulkan::ImageResource` e `Vulkan::SamplerResource`. O cache
guarda o recurso de imagem por ponteiro opaco e o sampler por `shared_ptr`;
somente o renderer/adaptador extrai `vk::Image` e `vk::Sampler`. O passe FSR
usa o mesmo proprietário de imagem Vulkan. Buffers, formatos nativos, barreiras
e outros caminhos do cache ainda dependem de Vulkan; o 2K segue em andamento.

No terceiro corte do 2K, `Vulkan::BufferResource` passa a possuir o buffer VMA,
o mapeamento, o endereço de GPU e a sincronização da memória mapeada. O cache
guarda o proprietário por ponteiro opaco. `Buffer::Handle()` e as APIs de cópia,
tiling e barriers ainda expõem tipos Vulkan para manter o renderer funcional;
portanto a remoção de todos os handles da fronteira permanece pendente. O
teste de fronteira protege a nova posse, mas não substitui regressão visual.

No quarto corte do 2K, upload, download e cópia intermediada por buffer de
`Image` recebem `Vulkan::BufferResource` em vez de `vk::Buffer`. O `TileManager`
passa a criar seus buffers temporários por esse proprietário e mantém sua vida
útil até o tick da GPU por operação adiada. O cache ainda contém barriers,
comandos e formatos Vulkan, então a fronteira completa e a regressão visual
seguem pendentes.

No quinto corte do 2K, `Buffer::Transition` entrega somente
`BufferTransition`, sem barrier nativa. `Vulkan::GetBufferBarrier` converte
acesso e estágio semânticos para `vk::BufferMemoryBarrier2` e anexa o handle
do recurso na emissão Vulkan. Os caches ainda constroem listas de barriers e
comandos Vulkan; a tradução foi centralizada, mas a fronteira não está
completa.

No sexto corte do 2K, os helpers exclusivos do Vulkan para blit e tiling
passam a `renderer_vulkan` e ao namespace `Vulkan`. `TextureCache` os guarda
por ponteiros proprietários opacos, sem expor seus pipelines, layouts ou
shaders no cabeçalho do cache. Restam formatos nativos e APIs de comandos e
barriers em caches; a 2K ainda não está encerrada.

No sétimo corte do 2K, o gerenciador do buffer de faults e de seu pipeline
compute passa a `renderer_vulkan`. `BufferCache` o guarda por ponteiro opaco;
seu cabeçalho não inclui mais o gerenciador nem expõe os objetos Vulkan de
pipeline. Este corte não retira os formatos nativos de imagens nem as APIs de
cópia e barriers dos caches.

Para encerrar 2K, ainda precisamos remover os **handles e pedidos nativos**
que atravessam as interfaces de `buffer_cache` e `texture_cache`: `Buffer::Handle`,
`Image::GetImage`, listas de barriers Vulkan, cópias com `vk::BufferCopy` e
formatos `vk::Format` usados como chaves do cache. As implementações podem
continuar Vulkan por dentro; a fronteira pública deve aceitar descritores
neutros e deixar a tradução para o backend. A prova de regressão visual e o
trace replay pertencem à 2L. Não há número fixo de PRs para fechar 2K; a
conclusão depende dessas fronteiras estarem removidas e compiladas.

Os marcos planejados para encerrar a Fase 2 são: **2J** (fronteira de comandos e
recursos entre caches e backend), **2K** (isolar handles Vulkan nessa fronteira)
e **2L** (testes de contrato, trace replay e regressão do renderer Vulkan).
Esses nomes agrupam trabalho técnico; um marco pode precisar de mais de uma PR.

## Fase 3 — Backend D3D12

- [ ] Device, queues, command lists e fences.
- [ ] Alocador de recursos e residency.
- [ ] Descriptors e root signatures.
- [ ] Barriers, copies, clears e resolves.
- [ ] Graphics/compute PSOs e cache persistente.
- [ ] Swapchain UWP.

## Fase 4 — Shaders

- [ ] SPIR-V para HLSL via SPIRV-Cross.
- [ ] HLSL para DXIL via DXC.
- [ ] Reflection e remapeamento de bindings.
- [ ] Corpus golden Vulkan/D3D12.
- [ ] Avaliar emissor HLSL direto a partir do IR.

## Fase 5 — Plataforma e primeiro frame

- [ ] Filesystem e importação de conteúdo.
- [ ] Áudio e gamepad.
- [ ] Threads, TLS e tratamento de exceções.
- [ ] Persistência de configurações e caches.
- [ ] Processar um command buffer PM4 mínimo.
- [ ] Apresentar o primeiro frame.
