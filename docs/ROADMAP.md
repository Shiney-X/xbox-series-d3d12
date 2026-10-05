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

**Status: Fases 2J e 2K integradas; ferramentas de regressão da Fase 2L em
validação; comparação visual de jogo real pendente.** DELTARUNE `CUSA15250`
foi observado em gameplay no shadPS4 SDL/Vulkan após o merge da 2L. É um
smoke test funcional; uma única captura de janela não constitui o par de
frames game-only exigido para fechar a regressão visual.

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
- [x] Remover declarações de handles e outros tipos `vk::` dos cabeçalhos de
      `amdgpu`, `buffer_cache` e `texture_cache`.
- [x] Manter o backend Vulkan funcional.
- [x] Criar testes de contrato e replay sintético das transições neutras.
- [x] Comparar capturas reais do renderer Vulkan antes/depois da 2K.

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

No fechamento da 2K, `Buffer::Handle`, `Image::GetImage`, os formatos nativos
armazenados em `ImageInfo`/`ImageViewInfo` e as listas de barriers Vulkan saem
dos cabeçalhos dos caches. Pedidos de buffer usam `BufferTransition`; pedidos
de imagem usam `ImageTransition`, e a emissão das barriers fica no adaptador
Vulkan. `Vulkan::ImageNativeState` guarda as propriedades nativas da imagem.
A chave das views passa a usar o formato original do guest, podendo criar
views distintas onde a chave Vulkan antiga as reutilizava. Os `.cpp` dos
caches ainda implementam comandos Vulkan, e as referências opacas aos recursos
Vulkan ainda serão substituídas por contratos de backend no trabalho D3D12.
Este marco fecha a fronteira **de tipos nativos**, não a implementação de um
backend D3D12. Regressão visual e trace replay pertencem à 2L.

Os marcos planejados para encerrar a Fase 2 são: **2J** (fronteira de comandos e
recursos entre caches e backend), **2K** (isolar handles Vulkan nessa fronteira)
e **2L** (testes de contrato, trace replay e regressão do renderer Vulkan).
Esses nomes agrupam trabalho técnico; um marco pode precisar de mais de uma PR.
O procedimento e os critérios de evidência da 2L estão em
[`PHASE2L_VALIDATION.md`](PHASE2L_VALIDATION.md). O trace atual é sintético;
o marco só se encerra após a comparação visual de uma cena de jogo real.

## Fase 3 — Backend D3D12

- [x] 3A: extrair a posse de device, fila direta, criação de command lists e
      fence do shell UWP para um contexto D3D12 reutilizável no host.
- [x] Validar 3A em MSIX no Xbox: abrir shell, renderizar ícone, suspender,
      sair ao Dev Home e reabrir em novo processo.
- [x] 3B: submeter command lists com tickets de fence, usar dois contextos de
      frame e drenar a fila ao alterar ícones, suspender ou encerrar o shell.
- [x] Validar 3B no Xbox com navegação repetida, uploads de ícones e retomada;
      conferir contadores de submissão e reutilização em `phase0-results.jsonl`.
- [x] 3C: centralizar buffers/texturas committed, posse RAII e contabilidade
      por heap com teto de memória do host; integrar o upload de ícones.
- [x] Validar 3C no Xbox: alternar ícones, voltar à Home e conferir que o
      orçamento dos recursos liberados retorna a zero no relatório.
- [x] 3D: descriptors com slots limitados, root signatures e PSOs graphics
      do host/compute com cache em memória por device.
- [x] Validar 3D no Xbox: interface/ícones sem regressão, dispatch/readback
      sintético aprovado e suspensão/retomada com os novos componentes.
- [x] 3E: encoder com estados explícitos, transition/UAV barriers, cópias
      de buffers/texturas, clears de cor/depth D32 e resolve MSAA de cor.
- [x] Validar 3E no Xbox: autoteste de transferências/readback, ícones,
      apresentação e suspensão/retomada sem regressão.
- [x] 3F: consumidor inicial de `VideoCore::GpuCommandSink` para DMA de
      buffers/sincronização, importação linear BGRA e preview UWP sintético.
- [x] Validar 3F no Xbox: readbacks do bridge, preview verde/laranja em
      Diagnostics, ícones e suspensão/retomada com o frame preservado.

O contexto 3A reorganiza recursos **já usados pelo shell**. Ainda não recebe
comandos PM4 do shadPS4, não implementa cache de recursos/pipelines de jogo
e não permite iniciar um título no Xbox. O checklist geral permanece aberto
até existir integração com o renderer do emulador.

O planejamento agrupa a Fase 3 em seis blocos, 3A–3F. Na 3C, o teto de
64 MiB é uma política do host para recursos sob posse do alocador. A
contabilidade usa `GetResourceAllocationInfo`; não mede RAM total, VRAM
física ou o budget de residência do driver. Recursos committed usam
residência implícita, sem eviction/subalocação. Swapchain, descriptors,
pipelines, dados de CPU e caches Vulkan estão fora dessa contagem. A
integração com o renderer PS4 ainda precisa tratar sua política de memória
e vida útil; o trabalho no host não encerra esses requisitos do emulador.

A 3D possui cache de até 64 entradas por categoria (roots, graphics e
compute), com chaves por conteúdo e sem persistência em disco. O subconjunto
graphics inicial é o do host: triângulos, um color target, sem vertex inputs,
depth ou MSAA. Estados de jogo ainda exigem ampliar API/chave na integração.
O autoteste de compute executa um shader HLSL sintético, não código PS4.

A 3D foi validada no Series S em 2 de outubro de 2026. A captura da sessão
`134354436005173693-1712` aprovou todos os 14 probes: compute/readback,
duas roots, um PSO graphics, um compute e quatro hits. Ao suspender, o
ticket 18 estava concluído, com 13 frames/11 reutilizações e zero recursos
vivos/falhas. O journal também registrou duas retomadas com apresentação
na sessão anterior `134354435676283705-3048`, além da nova abertura.

Na 3E, os estados são uniformes por recurso e locais à gravação em fila
direta. Cópias de textura/resolve são limitadas a uma textura 2D com um mip,
uma camada e uma plane; cópias usam footprint completo validado. Não há
enhanced/split/aliasing barriers, copy queue, stencil clear ou rastreamento
global de subresources. Essas restrições são explícitas, não caminhos mock.
O autoteste no Xbox inclui clear/resolve MSAA com readback quando suportado;
o relatório não aprova o gate de resolve se esse suporte estiver ausente.

A 3E foi validada no Series S em 2 de outubro de 2026, sessão
`134354448962723818-4692`. Todos os 16 probes passaram: resolve MSAA 4×,
cópias/readbacks, clears de cor/depth, 53 transições, nenhuma rejeição e
nenhuma falha de alocação. Após voltar à Home, os recursos vivos estavam
zerados; o pico foi 4915200 bytes. Foram apresentados 20 frames, com 18
reutilizações e ticket 27 concluído antes de suspender. A retomada no mesmo
processo reapresentou o shell e também passou.

A 3F fecha o último bloco **da fundação experimental do host** depois do
teste no console. O adapter implementa a interface real do VideoCore, mas
a fixture chama essa interface diretamente: `liverpool_bound=0`. Não
compila o Liverpool no UWP nem conecta draw/dispatch guest, GDS, MMU ou
caches de memória PS4. Comandos não suportados falham explicitamente.
O preview é produzido por fill/copy DMA na GPU, não por um jogo. Tradução
de shaders fica na Fase 4 e boot/PM4/primeiro frame na Fase 5; o backend
completo e sua integração com o runtime guest continuam pendentes.

A 3C foi validada no Series S em 2 de outubro de 2026, na sessão
`134354421747283697-2788`. Após alternar ícones e voltar à Home, todos os
probes passaram: 34 recursos criados, pico de 524288 bytes (512 KiB),
nenhuma falha e zero bytes/recursos vivos em todos os heaps contabilizados.
Foram apresentados 69 frames, com 67 reutilizações de allocator e ticket
final 108 concluído antes da suspensão. A reapresentação após retomada no
mesmo processo também passou.

O teste da 3A de 2 de outubro de 2026 passou todos os probes no Series S,
detectou dois jogos e dois ícones válidos e apresentou o ícone selecionado.
Os journals registraram três processos com suspensão; não contêm evento
`resume`, portanto não comprovam retomada no mesmo processo para essa build.
A 3B remove a espera completa ao fim de cada apresentação. A verificação
Windows usa D3D12/WARP com cópia e readback, e o teste de console está descrito
em [`PHASE3_VALIDATION.md`](PHASE3_VALIDATION.md).

A 3B foi validada no Series S em 2 de outubro de 2026: todos os probes
passaram, com 45 frames, 52 listas submetidas, 43 reutilizações de allocator
e sete esperas bloqueantes. O fence completou o último ticket sinalizado
(65) e a suspensão confirmou `gpu_drained=1`. O journal desta build contém
duas suspensões e uma retomada na mesma sessão, com reapresentação aprovada.
Dois jogos e dois ícones válidos foram detectados, com o selecionado exibido.

## Fase 4 — Fundação inicial de shaders (4A–4E)

**Escopo inicial implementado e validado no Series S até 4D; a 4E consolida
os gates de regressão. Isso não declara suporte a shaders de jogos.**

- [x] 4A: caminho inicial SPIR-V compute para HLSL via SPIRV-Cross,
      compilado no UWP e conectado a DXC/dispatch/readback no Xbox.
- [x] Validar 4A no Series S: `d3d12-shaders`, Diagnostics e retomada.
- [x] 4B: compartilhar a ABI real `Shader::PushData` e adaptar seu layout
      SPIR-V a b0/space0 e 30 root constants, com reflection estrita/readback.
- [x] Validar 4B no Series S: todos os campos de PushData; retomada
  confirmada pelo teste manual do usuário.
- [x] 4C: ligar IR e emissor real `EmitSPIRV` ao UWP, sem renderer Vulkan
  ou singleton de settings; compute autoral com dispatch/readback.
- [x] Validar 4C no Series S: `SHAD EMITTER PASS` e regressão do host.
- [x] 4D: VS/PS emitidos de IR autoral, varying float4 e PushData por estágio
  em b0/space1 e b0/space2; draw/readback com dois conjuntos de constantes.
- [x] Validar 4D no Series S: `SHAD VS PS PASS` e 32 pixels corretos;
  journal registra resume anterior, sem apresentação pós-resume registrada.
- [x] 4E: consolidar regressões compute/VS/PS, validador de evidências
  com testes negativos em CI e critérios explícitos de fechamento.
- [x] Avaliar emissor HLSL direto a partir do IR: adiado por ADR 0042;
  não implementado nem medido como alternativa de performance.

### Extensões pendentes — não incluídas no fechamento inicial

Estes eram os itens amplos da Fase 4. São preservados, agora decompostos;
não foram concluídos nem descartados para encerrar a fundação.

- [x] Traduzir saída do emissor real com IR autoral compute e VS/PS.
- [ ] Traduzir saída do frontend/recompiler de shaders GCN reais de jogos.
- [x] HLSL/DXIL vertex/fragment com um varying e constantes por estágio.
- [ ] Recursos de jogos: vertex fetch, buffers, texturas, samplers e layouts adicionais.
- [x] Reflection/remapeamento restrito à ABI PushData e fixtures iniciais.
- [ ] Reflection/remapeamento dos layouts de recursos guest suportados.
- [x] Corpus sintético inicial com oracles independentes e testes negativos.
- [ ] Corpus golden Vulkan/D3D12 dos mesmos shaders/dados, incluindo guest.

As extensões devem ser retomadas conforme os primeiros shaders/recursos
alcançados pela integração guest. Fase 5 não pode pular esses bloqueios
nem tratar a conclusão inicial como garantia de boot ou primeiro frame.

A fundação 3A–3F foi validada no Series S. Na sessão 3F
`134354472178773688-6484`, todos os 17 probes passaram: buffer/textura
verificados, três apresentações em Diagnostics, 52 frames/50 reutilizações
e ticket 76 concluído. Suspensão/retomada no mesmo processo reapresentou o
shell. Zero falhas de alocação/rejeições; os 65536 bytes DEFAULT restantes
são o frame retido, com UPLOAD/READBACK zerados. O journal também confirmou
dois jogos e dois ícones válidos. A conclusão se limita à fundação do host,
não à integração completa com o renderer PS4.

A 4A usa SPIR-V autoral, não shaders extraídos de um jogo nem saída do
recompiler. Traduz no console, não gera HLSL antecipadamente no desktop.
O contrato inicial é compute com tamanho de grupo literal e uma imagem
R32_UINT em set 0/binding 0. O teste compara os readbacks do HLSL original e
traduzido aos valores independentes 100–103. Não há boot, PM4 ou tradução
de ISA PS4 neste bloco. Ver [validação da Fase 4](PHASE4_VALIDATION.md).

A 4A foi validada no Series S, sessão `134354583014913662-4508`:
18 probes passaram, incluindo tradução no Xbox, DXIL e readbacks 100–103
do shader original/traduzido. Ticket 26 concluído, 14 frames/12 reutilizações,
dois PSOs compute e sete hits. Suspensão/retomada no mesmo processo passou;
UPLOAD/READBACK zerados, zero falhas/rejeições e frame 3F DEFAULT de 65536
bytes retido. O contador de Diagnostics dessa última sessão foi zero;
a captura enviada separadamente confirmou SPIRV HLSL DXIL PASS e as faixas.

A 4B trata um bloqueio concreto do emissor: `DefinePushDataBlock` sempre
declara a ABI PushData. Seu tipo real é compartilhado sem carregar dependências
de recursos/logging no UWP. O teste ainda usa SPIR-V autoral com esse layout,
não chama `EmitSPIRV`: `upstream_emitter_linked=0` permanece explícito.
Ligar o emissor/IR e tratar SSBOs/capabilities continua no próximo gate;
não interpretar a 4B como suporte a shaders de jogos.

Evidência 4B: sessão `134354603362413650-5884`, 18 probes positivos,
readback 1066–1069, 3 roots/3 compute/9 hits, nenhum allocation failure
ou pedido rejeitado e UPLOAD/READBACK zerados. Captura mostra o PASS e
preview; journal registra suspend e relaunch, enquanto resume foi
confirmado separadamente pelo usuário, não inferido do journal.

A 4C compila o emissor upstream real e constrói um programa pequeno com
`IR::IREmitter`. Não usa GCN de jogo, frontend de tradução, resource tracking
de guest, PM4 ou runtime. Ver ADR 0040. A 4D validou bindings iniciais e
vertex/fragment; a 4E consolida regressão e fechamento do escopo inicial,
sem alegar comparação golden Vulkan/D3D12 ou compatibilidade com jogos.

Evidência 4C: sessão `134354649597873641-3192`, 20 probes positivos,
readback 100–103 do emissor real e journal confirmando suspend/resume na
mesma sessão. Captura confirma o painel legível, PASS e preview sintético.
A 4D limita os bindings gráficos a PushData por estágio e um varying;
texturas, samplers, SSBOs e vertex fetch guest continuam pendentes. Não
marcar os itens amplos de recursos de jogos/reflection como completos.

Evidência 4D: sessão de resultados `134354665468983635-5052`, 20 probes
positivos, draws vermelho/verde com 16 pixels cada e bindings VS/PS separados.
4 roots, 2 graphics, 4 compute e 14 hits; UPLOAD/READBACK=0, zero falhas
de alocação, frame DMA retido de 65536 bytes. Captura mostra SHAD VS PS PASS.
O journal registra resume em `134354665134013638-6320` e relaunch na sessão
de resultados; não contém apresentação após aquele resume. Não confundir
essas duas evidências nem exigir recompilar para repetir o teste de retomada.

## Fase 5 — Plataforma e primeiro frame

Planejamento inicial de 5A–5G: auditoria/preflight, loader controlado,
execução/ABI, HLE/TLS/input, boot Deltarune, integração gráfica e primeiro
frame reproduzível. São gates, não promessa de sete PRs ou sucesso do jogo.
Ver [procedimento e limites](PHASE5_VALIDATION.md) e ADR 0043.

- [x] 5A: auditar dependências do runtime e implementar inspeção read-only
  ELF/SELF de eboot no UWP, com relatório; não carrega nem executa guest.
- [x] Validar cabeçalhos 5A no Xbox: Sonic Mania CUSA07023 e Deltarune
  CUSA15250; SELF/Orbis, 11 program headers por arquivo, sem execução.
- [x] Regressão 5B no Xbox: 23 probes, suspend/resume e apresentação após
  retomada na mesma sessão, sem alertas do validador.
- [x] 5B: implementar plano PT_LOAD e staging de fixture em buffer de dados,
  ranges, flags guest e limites; sem endereço/permissões host executáveis.
- [x] Validar 5B no Xbox: fixture 32 KiB com cópia/BSS corretos e planos
  válidos de dois PT_LOAD em Sonic Mania e Deltarune; payload SELF não carregado.
- [x] 5C: implementar fixture ELF autoral, boundary Win64/SysV leaf, proteção
  RX/RW/NOACCESS, thread dedicada e SEH de leaf Win64 separada.
- [x] Validar perfil inicial 5C no Xbox: soma SysV 42, fault Win64 separado,
  proteções/cleanup e 25 probes com apresentação após retomada, sem alertas.
- [x] Implementar metadados dinâmicos da bridge, oracle de contexto e fault
  da leaf SysV através da bridge; gate de build/hardware independente.
- [x] Validar unwind da bridge com leaf SysV no Xbox: retornos 42,
  recuperação Win64/SysV, tabelas removidas e 25 probes com retomada.
- [ ] Resolver unwind/faults de funções guest gerais, não leafs e startup
  Orbis antes de executar guest geral; fixture não certifica esses caminhos.
- [ ] 5D: HLE/threads/TLS/filesystem e gamepad guest básico.
  - [x] Implementar chamada autoral guest→host→guest com thunk SysV/Win64,
    serviço inteiro limitado, rejeição de operação/overflow e unwind registrado.
  - [x] Validar esse round-trip HLE no Xbox: retorno 42, rejeições,
    unwind/cleanup e 25 probes com retomada; não equivale a imports/HLE Orbis.
  - [x] Implementar duas workers autorais concorrentes com contexto host TLS,
    chave oculta no thunk HLE, rendezvous e limpeza após join.
  - [x] Validar isolamento/cleanup de threads e contexto host TLS no Xbox:
    valores 100/101, IDs distintos, rejeições e 25 probes com retomada.
  - [x] Implementar leitura USB limitada e staging de dados PT_LOAD/RELRO via
    adapter SELF direto não criptografado/comprimido, com BSS e diagnóstico.
  - [x] Validar payloads reais Sonic/Deltarune no Xbox; staging não é boot,
    filesystem HLE ou integração do loader/runtime upstream.
  - [x] Implementar inventário limitado de dependências/imports, RELA/JMPREL
    e PT_TLS a partir do snapshot ELF/SELF, com fixture e gates negativos.
  - [x] Validar `phase5-link.jsonl` dos dois títulos no Xbox, incluindo retomada.
    Manifesto válido não resolve imports nem aplica relocations.
  - [x] Implementar relocations relativas e local64 em imagem de dados, com
    load bias numérico, preflight transacional, verificação exata e pendências.
  - [x] Validar `phase5-relocations.jsonl` no Xbox; endereços sintéticos não
    são mapeamentos executáveis e imports externos continuam sem resolver.
  - [x] Implementar normalização NID/ID/biblioteca/módulo/versão/tipo e registry
    numérico data-only, com vinculação transacional de fixtures raw/SELF.
  - [x] Validar `phase5-imports.jsonl` e oracle de imports no Xbox. O registry
    dos jogos continua vazio: isso não cadastra HLE nem executa o guest.
  - [x] Integrar resolver/JUMP_SLOT a chamada HLE autoral raw/SELF, com bias
    da alocação real, gates de RX/unwind/TLS e rejeições antes de executar.
  - [x] Validar execução via import resolvido nas duas workers no Xbox;
    não é registry runtime Orbis ou boot de jogo.
  - [x] Implementar subset pointer-free de tempo libkernel com NIDs upstream,
    contador/frequência QPC virtual e conversão checked de microssegundos.
  - [x] Validar chamadas aos três serviços Orbis de tempo via resolver em
    ELF/SELF autoral no Xbox. Não registrar esses exports para jogos ainda.
  - [x] Implementar fronteira de ponteiros guest com ranges autorizados,
    permissões, tamanhos checked e memcpy/memset/memcmp por imports reais.
  - [x] Validar esses três serviços em ELF/SELF autoral no Xbox: bytes/retornos,
    rejeição sem writes, ABI de três argumentos e unwind/cleanup. Não é MMU,
    mapeamento dinâmico ou tratamento geral de faults do runtime.
  - [x] Integrar sessão de preparação por título: bias da alocação real,
    imagem data-only possuída pelo host, stack, TCB/DTV do módulo principal,
    layouts compartilhados com upstream e bloqueio explícito antes do entry.
  - [ ] Validar preparação selecionada e liberação/retomada no Xbox; ação
    A PREPARE não chama o entry e não conclui 5D/5E. ADR 0056.
- [ ] 5E: tentar boot Deltarune e registrar o primeiro bloqueio real.
- [ ] 5F: consumir PM4, shaders e recursos necessários à primeira tela.
- [ ] 5G: primeiro frame real reproduzível, se os gates anteriores passarem.

- [ ] Filesystem e importação de conteúdo.
- [ ] Áudio e gamepad.
- [ ] Threads, TLS e tratamento de exceções.
- [ ] Persistência de configurações e caches.
- [ ] Processar um command buffer PM4 mínimo.
- [ ] Apresentar o primeiro frame.
