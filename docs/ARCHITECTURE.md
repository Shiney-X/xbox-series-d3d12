# Arquitetura

## Objetivo

Executar o shadPS4 no Xbox Series S em Dev Mode com UWP x64 e Direct3D 12,
preservando o frontend AMD Liverpool/PM4 e o IR de shaders do upstream.

## Limites de plataforma

O baseline público é UWP/AppContainer. GDK/GameCore é um alvo separado,
condicionado a autorização da Microsoft e nunca deve introduzir SDKs ou material
sob NDA no repositório.

## Fluxo pretendido

```mermaid
flowchart LR
    ELF[ELF PS4] --> Loader[Loader e HLE]
    Loader --> CPU[Execução x86-64 nativa]
    CPU --> GNM[GNM]
    GNM --> PM4[PM4 / Liverpool]
    PM4 --> RHI[Contratos neutros do renderer]
    RHI --> VK[Backend Vulkan]
    RHI --> D3D[Backend D3D12]
    PM4 --> FE[Frontend GCN]
    FE --> IR[IR do shadPS4]
    IR --> SPV[SPIR-V]
    SPV --> VK
    SPV --> CROSS[SPIRV-Cross]
    CROSS --> HLSL[HLSL SM6]
    HLSL --> DXC[DXC]
    DXC --> DXIL[DXIL]
    DXIL --> D3D
```

## Princípios

### Compilador isolado da 4C (validado no Xbox)

O UWP linka a IR e `EmitSPIRV` reais como biblioteca AppContainer. A entrada
atual é IR autoral compute, não GCN de jogo. A emissão não consulta settings
desktop nem inclui BufferCache/Vulkan: opções de DMA/fetch vêm do chamador,
e constantes de endereçamento são compartilhadas em header sem API gráfica.
Vulkan preserva suas opções e parser; logging standalone do compilador tem
erros fatais propagados ao probe. Não há fallback para um binário fixture.
O teste passa por SPIRV-Cross/DXC e confere valores GPU em R32_UINT 2×2.
ABI, bindings e escopo continuam restritos à 4B; runtime de guest não está ligado.

### Graphics inicial da 4D (validado no Xbox)

O mesmo emissor real gera VS/PS de IR autoral. Reflection limita o contrato
a um varying float4 location 0 e Shader::PushData, sem vertex fetch ou
recursos guest. SPIRV-Cross remapeia PushData a b0/space1 (VS) e b0/space2
(PS); duas roots de constantes por estágio custam 60 DWORDs. Não expandir
esse layout indiscriminadamente: recursos futuros exigirão outro orçamento.
DXC compila vs_6_0/ps_6_0 no console. Um probe offscreen compartilhado com
WARP desenha vermelho e verde, trocando constantes e verificando todos os
pixels por readback. Diagnostics informa o resultado; seu preview continua
DMA sintético, não frame de jogo. Ver ADR 0041 e PHASE4_VALIDATION.

### Gate de regressão da 4E

O contrato inicial é testado em três níveis: emissão/reflection portátil,
GPU WARP/DXC/DXIL em CI e probes no Xbox. `scripts/validate_phase4.py`
confere os logs exportados, ABI, readbacks declarados, bindings e estados
do journal; não executa GPU nem atesta criptograficamente a origem dos logs.
Relaunch em outro processo não é resume, e um evento de resume isolado não
prova apresentação posterior. Captura e testes do console continuam necessários.
O corpus inicial usa resultados independentes, não comparações com Vulkan.
O corpus cross-backend de shaders guest continua bloqueando a alegação de
compatibilidade de jogos. ADR 0042 mantém SPIRV-Cross/DXC como caminho inicial.

1. Validar memória e execução antes do renderer.
2. Manter o backend Vulkan funcional durante o desacoplamento.
3. Abstrair semântica do guest, não criar uma cópia da API Vulkan.
4. Consultar capabilities D3D12 em runtime.
5. Tratar falhas de alocação como fluxo normal e mensurável.
6. Nunca depender de um limite observado somente com debugger conectado.

## Gates de viabilidade

Os probes são implementados em `src/phase0` sem dependência de console ou UI.
Dois hosts consomem a mesma biblioteca: o runner Win32 usado pelo CI e o host
UWP x64 instalado no Xbox. Isso evita comparar implementações diferentes ao
investigar divergências entre desktop e console.

### Gate A: memória virtual

- reserva e commit de grandes intervalos;
- endereços solicitados/fixos;
- proteções `NOACCESS`, `RW` e `RX`;
- mappings aliases/placeholder;
- tratamento de access violations.

### Gate B: código executável

O pacote UWP deve declarar `codeGeneration`. O probe deve comprovar transição
RW para RX, flush do instruction cache e chamada indireta de um trampoline x64.
Páginas simultaneamente graváveis e executáveis não fazem parte do desenho.

### Gate C: gráficos

- device e swapchain D3D12;
- shader compilado em runtime;
- root signature e PSO;
- apresentação sem debugger;
- DRED e coleta de logs em falha.

O host do Gate C cria dispositivo, command queue, swapchain de `CoreWindow`,
fence, root signature e graphics PSO. Um shader de probe usa `SV_VertexID` para
desenhar um triângulo sem vertex buffer. Ele é compilado em runtime pelo DXC
como Shader Model 6/DXIL. `dxcompiler.dll` e o validador `dxil.dll` da versão do
Windows SDK usada no build são empacotados junto ao MSIX, evitando dependência
implícita de uma instalação de ferramentas no console.

Na Fase 3A, `platform/xbox_uwp/d3d12_device_context.*` possui o device, a fila
direta, o fence e o evento de espera. Ele cria pares de allocator/list para o
shell e para upload de ícones. `D3D12StatusRenderer` continua responsável por
swapchain, RTV/SRV e recursos da interface. Na 3B, o contexto submete listas e
devolve tickets monotônicos de fence. Cada um dos dois backbuffers possui um
allocator/list e o ticket de seu último uso; o shell espera esse ticket antes
de resetar o allocator. A apresentação não faz mais uma espera completa ao
final de cada frame. A troca do SRV compartilhado de ícone, a suspensão e o
encerramento drenam a fila; o upload de ícones permanece síncrono. Contadores
de frames, listas, reutilizações e esperas são persistidos com os probes.
O contexto não é ligado ao `GpuCommandSink` nem traduz comandos PM4.

Na 3C, `D3D12ResourceAllocator` cria buffers DEFAULT/UPLOAD/READBACK e
texturas 2D DEFAULT como recursos committed. `D3D12Resource` é um proprietário
movível e não copiável, que libera o recurso e devolve sua contabilidade ao
ser destruído/resetado. O tamanho cobrado vem de `GetResourceAllocationInfo`,
incluindo a granularidade de alocação. O estado de contabilidade mantém o
device vivo mesmo quando um recurso sobrevive ao objeto alocador.

O teto inicial de 64 MiB vale somente para buffers/texturas do host criados
por esse alocador. Ele rejeita uma criação acima do teto com `E_OUTOFMEMORY`
e mantém o proprietário de saída anterior quando uma criação falha. O
renderer usa esse caminho para textura e staging do ícone; a proteção por
fence da 3B continua sendo responsabilidade do chamador. Recursos liberados
depois de uma cópia concluída deixam a contagem imediatamente. O probe
`d3d12-resources` registra uso vivo/pico, heaps e falhas. Esses números não
equivalem ao uso total do app nem à residência física medida pelo driver.

O probe usa as interfaces estáveis `IDxcLibrary` e `IDxcCompiler`. O primeiro
teste com `IDxcCompiler3` retornou `E_NOINTERFACE` no Series S, embora a mesma
versão compilasse no SDK 26100. A escolha da interface antiga afeta somente a
API de invocação do compilador; a saída continua sendo Shader Model 6/DXIL.

Na 3D, `D3D12DescriptorArena` reserva slots fixos e valida capacidade,
índices e visibilidade GPU. O shell usa dois RTVs e um SRV; atualizar o SRV
continua exigindo a drenagem da fila. Não há reciclagem de slots em voo.
`D3D12PipelineCache` mantém root signatures serializadas e PSOs por conteúdo
dos shaders, layout, formato e blend. As chaves copiam os bytes relevantes,
sem depender de ponteiros, padding ou somente de hashes. Cada categoria tem
até 64 entradas por device; não há eviction, cache em disco nem concorrência.
O chamador deve serializar acesso e manter o cache vivo até concluir a GPU.

A API graphics expõe apenas o subconjunto inicial do host (triângulo,
um color target, sem vertex inputs/depth/MSAA); compute aceita bytecode e
layout. Isso não representa ainda todos os estados gráficos PS4. A interface
usa o cache para sua root/PSO, e na abertura executa um dispatch 2×2 com
UAV/readback que verifica os inteiros 100–103. O probe `d3d12-pipelines`
registra esse resultado e contadores de criação/reuso. Os recursos
temporários são liberados depois do ticket concluído. Esse teste não é
shader guest, boot de jogo nem integração com PM4.

## Fronteira futura do renderer

A 3E introduz `D3D12CommandEncoder` para uma command list direta em gravação.
O chamador declara os estados iniciais; o encoder emite transições apenas
quando necessário, aceita UAV barriers e valida estados, flags, heaps,
limites de cópia e footprints antes de emitir comandos. UPLOAD permanece
GENERIC_READ e READBACK permanece COPY_DEST. `State()` indica o estado em
gravação; `StateAfterExecution()` considera decay de buffers DEFAULT e
texturas simultaneous-access ao fim de ExecuteCommandLists. O chamador
deve respeitar essa fronteira, a ordem de submissão e os tickets de fence.

Os estados são uniformes por recurso; o encoder não pode ser misturado com
barriers manuais ou reutilizado após Reset da lista. Cópias completas de
textura 2D e resolve de cor aceitam apenas um mip/layer/plane. Resolve exige
formatos/dimensões iguais e MSAA de origem para single-sample de destino.
Clears cobrem RTV de cor e depth D32_FLOAT; o chamador fornece um descriptor
válido que referencie o recurso informado e o preserva até concluir a GPU.
Recursos também devem permanecer vivos até o fence; a posse temporária
do encoder não substitui esse contrato de submissão.

A swapchain, upload de ícones e readback do compute usam esse componente.
Na abertura, `RunD3D12TransferProbe` compara cópias com offsets, pixels de
upload/clear/resolve e depth por readback. O suporte MSAA é consultado; sua
ausência é registrada e não tratada como resolve aprovado. O probe
`d3d12-transfers` persiste resultado e contadores. Não há estado global,
filas async/copy, split/enhanced/aliasing barriers ou stencil clear neste
corte. Também não executa comandos PM4 ou shaders de jogos.

### Consumidor inicial de comandos no host D3D12

Na 3F, `D3D12VideoCoreBridge` implementa a interface existente
`VideoCore::GpuCommandSink`, compilada no MSIX. O adapter traduz fill/copy
de buffers registrados para gravações D3D12 e devolve tickets reais em
Flush. Finish/CpSync aguardam a GPU; OnSubmit conserva staging ainda em
gravação e só libera pendências já concluídas. Registro rejeita intervalos
sobrepostos, aliases do mesmo recurso, overflow e buffers de outro device
ou heap. Endereços registrados são identificadores, nunca ponteiros host
desreferenciados. Recursos DEFAULT iniciam em COMMON e sua reutilização
considera decay depois de ExecuteCommandLists.

O registro inicial tem até 16 buffers, transferências de até 16 MiB e
staging lógico de até 16 MiB por batch, sujeito ao orçamento de recursos da
3C. A política serial espera o batch anterior antes de reciclar seu
allocator. Fills aceitam palavras de 32 bits; cópia para o mesmo buffer é
rejeitada. Importação de frame cobre apenas BGRA8 linear cujo pitch/offset
correspondam ao footprint D3D12, não superfícies tiled/compressed de PS4.
Um frame retornado permanece sob posse do chamador até acabar o uso na GPU.

A fixture chama o contrato polimorficamente, confirma readback do buffer
e da textura e preserva uma textura verde/laranja para o shell apresentar
em Diagnostics usando seu PSO DXIL existente. São dois slots SRV: ícone e
frame. O frame/descriptor não é refeito durante navegação/retomada. O
probe `d3d12-videocore` distingue geração/readback do contador de frames
Diagnostics efetivamente apresentados; o teste visual no console é
necessário mesmo com readback aprovado.

Isso não conecta o adapter a `Liverpool::BindCommandSink` no UWP. O
decoder, MemoryManager/GpuMemoryTracker, caches de jogo e shaders guest
continuam fora do host. Draw/dispatch, GDS e ProcessDownloadImages
retornam E_NOTIMPL: não são no-ops que aparentem sucesso. Marcadores validam
escopos e emitem metadados textuais no debug output do host; não são
anotações de GPU/PIX (`gpu_marker_annotations=0`). Uma gravação
descartada não é submetida pelo destrutor. O renderer Vulkan desktop não
foi alterado.

Os contratos neutros deverão cobrir:

- device, queues, command contexts e fences;
- buffers, imagens, views e samplers;
- descriptors e constantes;
- barriers e hazards;
- draw, dispatch, copy, clear e resolve;
- pipelines e cache persistente;
- queries e apresentação.

Tipos `vk::*` ou `ID3D12*` não poderão atravessar essa fronteira.

### Primeiro corte: coerência de memória

`VideoCore::GpuMemoryTracker` é o primeiro contrato implementado dessa
fronteira. `Core::MemoryManager` e `PageManager` publicam eventos usando somente
endereços virtuais e tamanhos do guest. O backend Vulkan implementa o contrato
em `Vulkan::Rasterizer`; o futuro backend D3D12 implementará a mesma semântica
com seus próprios caches.

```mermaid
flowchart LR
    MM[Core::MemoryManager] --> GMT[GpuMemoryTracker]
    PM[PageManager / page faults] --> GMT
    GMT --> VKR[Vulkan::Rasterizer]
    GMT -. futuro .-> D12R[D3D12 memory tracker]
```

A interface não recebe recursos nativos e não cobre draw, dispatch ou
apresentação. Esses limites serão extraídos separadamente para evitar uma
abstração prematura que apenas replique Vulkan.

### Segundo corte: comandos PM4

`AmdGpu::Liverpool` decodifica os pacotes PM4 e envia operações ao
`VideoCore::GpuCommandSink`. A interface transporta valores do guest para draw,
dispatch, DMA/GDS, sincronização e marcadores; `Vulkan::Rasterizer` permanece
como implementação ativa. `Liverpool` não inclui tipos do renderer Vulkan.

Este corte não define ainda buffers/imagens nativos, descriptors, pipelines ou
swapchain. A configuração `IsVkHostMarkersEnabled` continua controlando os
marcadores de diagnóstico existentes até a migração das preferências do
renderer.

### Terceiro corte: estado de acesso a buffers

`VideoCore::BufferSyncState` armazena a última intenção de acesso ao buffer
(vértices, índices, indireto, shader, transferência ou uso geral) e emite uma
transição com estado anterior, próximo estado e faixa afetada. A lógica não
conhece `vk::*` nem `ID3D12*`.

O `VideoCore::Buffer` existente continua sendo um recurso Vulkan. Ele traduz
as transições para estágios e máscaras de acesso Vulkan ao construir
`vk::BufferMemoryBarrier2`. A transição inicial mantém a máscara ampla usada
antes da extração. O rastreador não resolve hazards de imagem, ownership de
queues ou residency; esses casos exigem contratos próprios.

### Quarto corte: descrição de buffers

`VideoCore::BufferDesc` descreve o recurso antes de sua criação: endereço do
guest, tamanho, preferência de memória e usos necessários. `BufferUsage` usa
flags sem dependência de Vulkan; inclui transferência, uniform/storage,
vértices, índices, indireto e endereço de dispositivo. Um endereço zero indica
buffer utilitário sem mapeamento direto para a memória do guest.

Na implementação atual, `VideoCore::Buffer` traduz esses usos para
`vk::BufferUsageFlags` e continua alocando via VMA. O futuro backend D3D12
deverá traduzir o mesmo descritor para seus recursos e estados, levando em
conta que algumas capacidades do Vulkan não correspondem a flags de criação
do D3D12.

### Quinto corte: descrição de image views

`VideoCore::ImageViewDesc` concentra os campos da view que vêm do guest:
dimensionalidade, subrecursos, swizzle de componentes, LOD mínimo, intenção de
escrita e formato original. `ImageViewInfo` acrescenta o formato Vulkan usado
hoje para a criação e comparação das views em cache. A conversão de
`AmdGpu::CompMapping` para `vk::ComponentMapping` ocorre na criação da view.

O mapeamento de formatos e a compatibilidade entre formatos ainda dependem do
Vulkan. A futura fronteira de formatos precisará representar essas regras sem
alterar a interpretação dos recursos do guest.

### Sexto corte: identidade do formato de imagem

`VideoCore::ImageFormatDesc` preserva o formato informado pelo guest como um
dos três casos: surface (data/number format e intenção de depth), depth/stencil
ou VideoOut. O descritor não contém formatos Vulkan nem D3D12. Para imagens
auxiliares sem formato identificado, usa-se o estado vazio.

`ImageInfo` mantém `guest_format` e o campo `pixel_format` legado. O segundo é
resolvido a partir do primeiro pela conversão Liverpool→Vulkan existente.
`IsCompatible`, criação/alocação e interpretação de formatos continuam
específicas do Vulkan. O descritor evita perder a identidade original
em conversões não injetivas (por exemplo, dois formatos VideoOut que resultam
no mesmo `vk::Format`), mas não promete uma equivalência direta com `DXGI_FORMAT`.

### Sétimo corte: formato das image views

`ImageViewDesc` também carrega `guest_format`; a view preserva o formato original
mesmo quando Vulkan exige um ajuste na representação nativa. Por exemplo, uma
view storage com formato sRGB do guest é convertida para UNORM apenas ao chamar
`LiverpoolToVK::ImageFormat`. A mesma função resolve o formato de `ImageInfo` e
das views, evitando regras duplicadas no cache de texturas.

`ImageViewInfo::operator==` mantém a chave de cache Vulkan anterior: geometria,
swizzle, intenção storage e formato Vulkan. Dois descritores do guest que
produzam a mesma view nativa podem reutilizá-la. Um cache D3D12 deverá definir
sua própria chave conforme as regras de compatibilidade e formatos DXGI.

### Oitavo corte: capacidades de uso das imagens

`VideoCore::ImageUsage` descreve as capacidades que o cache solicita para uma
imagem: cópia de/para, sampling, attachment color/depth-stencil e storage.
`CachedImageUsage` preserva a política existente: imagens coloridas recebem
storage para evitar recriação posterior, e imagens comprimidas mantêm essa
capacidade para permitir views não comprimidas. A função é independente de API
e coberta por teste unitário.

Na criação Vulkan, `ToVulkanUsage` traduz essas capacidades para
`vk::ImageUsageFlags`. A extensão de attachment feedback loop só é acrescentada
quando suportada pelo dispositivo. Recursos, views, layout e barriers permanecem
no backend Vulkan; `ImageUsage` não é uma enumeração de estados D3D12.

### Nono corte: transições de imagens

`VideoCore::ImageSyncState` rastreia um estado sem tipos Vulkan para cada
backing image. O estado contém layout semântico, classe de acesso e estágio de
execução. Uma transição total pode gerar uma única descrição; uma transição
parcial mantém estados por mip/layer e gera descrições para os subrecursos que
precisam de barrier. A lista curta usa armazenamento inline para evitar uma
alocação no caminho comum.

O adaptador em `Image::GetBarriers` converte layouts/acessos/estágios recebidos
dos chamadores Vulkan para o rastreador, depois produz os
`vk::ImageMemoryBarrier2`. `Image::CurrentLayout()` fornece o layout nativo
necessário a operações Vulkan existentes. A política de hazard foi preservada:
escritas de transferência, shader e memória forçam barrier mesmo quando o
estado seguinte é idêntico; alterar apenas o estágio não força barrier.

Isso não define ainda uma API neutra para os comandos de transição vindos do
rasterizer, nem traduz essas transições para D3D12. Essas interfaces pertencem
ao próximo corte de fronteira dos caches.

## Caminho inicial de shaders no UWP (4A)

`ComputeFixture` (SPIR-V autoral) → `TranslateCompute` (SPIRV-Cross no
console) → HLSL/main → DXC/cs_6_0 → DXIL → PSO compute → dispatch → readback.
O teste compara quatro valores fixos com os produzidos pelo shader HLSL
original e traduzido. Os dois PSOs compartilham a root de uma UAV.

A biblioteca é compilada de fontes fixadas por commit/SHA-256 para
AppContainer, não uma DLL desktop. O wrapper só aceita um compute e imagem
R32_UINT em set 0/binding 0 com tamanho de grupo literal. A entrada interna
não representa a saída do recompiler PS4; adaptar `EmitSPIRV`, reflection
geral, bindings guest, vertex/fragment e waves continua pendente. Ver
[ADR 0038](adr/0038-initial-spirv-hlsl-dxil.md).

A 4B compartilha o tipo `Shader::PushData` em um header de ABI sem Vulkan,
Boost ou logging. `resource.h` preserva a definição inline de AddOffset e
sua política ASSERT. O layout continua 120 bytes: quatro floats, 16 uints
e 40 bytes de offsets. Reflection aceita apenas os 11 membros/offsets
exatos emitidos por `DefinePushDataBlock`, mapeados para b0/space0 e 30
root constants. A root compute custa 31 DWORDs, incluindo a tabela UAV.
O módulo de teste com esse layout é autoral; o emissor completo ainda não
está ligado. Ver [ADR 0039](adr/0039-shader-push-data-abi.md).

## Fronteira do host

`Frontend::Window` é o contrato entre o ciclo de execução do emulador e a
janela fornecida pela plataforma. `Core::Emulator` recebe uma
`Frontend::WindowFactory`; ele não escolhe SDL, UWP ou outro toolkit.

No desktop, `main.cpp` injeta `WindowSDL`. O host UWP deverá injetar uma
implementação baseada em `CoreWindow`, com seu próprio loop de eventos e
superfície D3D12. A interface Vulkan também consome o contrato neutro e obtém o
handle nativo por `WindowSystemInfo`.

O handle opaco retornado por `GetFrontendHandle()` é uma ponte transitória para
os adaptadores SDL de ImGui e mouse. Código novo de core ou renderer não deve
depender desse handle.

## Biblioteca USB no host UWP

O host não converte permissão WinRT em acesso irrestrito por caminho. A pasta
selecionada é mantida como uma cadeia relativa ao dispositivo removível e
resolvida novamente por objetos `StorageFolder` a cada ativação.

```text
KnownFolders.RemovableDevices
  -> caminho relativo persistido
  -> enumeração StorageFolder limitada
  -> eboot.bin + sce_sys/param.sfo
  -> parser PSF limitado da bridge do core
  -> sce_sys/icon0.png opcional
  -> Windows.Graphics.Imaging (BGRA8, máximo 256x256)
  -> upload sob demanda da seleção para SRV D3D12
  -> modelo visual do shell D3D12
```

O parser recebe bytes lidos por `FileIO`, usa os layouts PSF do upstream e não
confia em offsets, contagens ou terminação NUL vindos do arquivo. Essa fronteira
mantém as APIs WinRT no host e os detalhes do formato Orbis na bridge do core.

O PNG nunca é interpretado pelo parser PSF. O host limita o arquivo codificado
a 8 MiB, rejeita dimensões de origem acima de 4096 e pede ao decoder WinRT uma
saída BGRA8 de no máximo 256×256. Os pixels permanecem em memória de CPU no
modelo da biblioteca; o renderer mantém apenas uma textura GPU, correspondente
ao jogo selecionado. A troca aguarda a fence existente antes de substituir o
SRV. Ausência, PNG inválido ou falha de upload não remove o jogo da lista.
