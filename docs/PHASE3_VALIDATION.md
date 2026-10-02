# Fase 3 — testes do host D3D12 no Xbox

## 3B: submissão e reutilização de frames

Instale o MSIX da build da 3B, depois dos checks Windows verdes. Use o perfil
**Game** e o mesmo dispositivo USB com jogos que já foi reconhecido na 3A.
Uma alteração de C++ exige um novo pacote; o merge não atualiza o console.

1. Abra o shell e entre em Games. Confira títulos e ícone selecionado.
2. Alterne entre os dois jogos pelo menos dez vezes, para exercitar a troca
   da textura/SRV. Confira que cada ícone acompanha sua seleção.
3. Volte com B e navegue por Home, Settings e Diagnostics umas dez vezes,
   para produzir mais apresentações do que o número de contextos de frame.
4. Volte ao Dev Home e reabra o app três vezes. Confira a interface e os
   ícones. Se houver retomada no mesmo processo, ela terá evento `resume`;
   eventos `launch` com sessões distintas indicam processos novos.
5. Volte ao Dev Home ao terminar, para suspender e persistir os contadores.
   Exporte `phase0-results.jsonl` e `phase0-lifecycle.jsonl` do LocalState.

O probe `d3d12-submission` deve passar, informar `frame_contexts=2`,
`submitted_frames` maior que dois e `allocator_reuses` maior que zero após
essa navegação. `submitted_lists` inclui frames e uploads de ícones e deve
ser pelo menos igual a `submitted_frames`. Após suspensão bem-sucedida,
`completed_ticket` deve alcançar `last_signaled_ticket`, e o probe de
suspensão informa `gpu_drained=1`. Os contadores pertencem ao processo atual;
uma nova abertura pode reiniciá-los.

`blocking_waits=0` é permitido: significa que os tickets verificados já
estavam concluídos. Não se exige uma contagem positiva nem um FPS específico.
Todos os outros probes devem continuar passando. Tela preta/vermelha,
travamento ou ícone que muda incorretamente constituem regressão.

## O que a CI cobre

O check `Xbox UWP shell / build-and-test` executa
`phase3.d3d12-submission` no Windows com D3D12/WARP. É um teste de cópia,
readback e reutilização de command allocators reais da API, sem janela.
`build-uwp-package` compila e empacota o host UWP. Os dois checks complementam
o teste no Series S; não executam o app no console nem iniciam jogos PS4.

## 3C: buffers, texturas e orçamento

Instale o novo MSIX como Game após os checks verdes. Não é preciso mudar os
dumps nem formatar o USB.

1. Entre em Games e alterne entre os dois títulos umas dez vezes. Confira
   que os ícones continuam correspondendo ao título selecionado.
2. Pressione B para voltar à Home do app, liberando o ícone selecionado.
3. Volte ao Dev Home, reabra, entre em Games e repita a troca de títulos.
4. Termine na Home do app (B), depois volte ao Dev Home para suspender e
   persistir os contadores. Exporte os dois arquivos `phase0-*.jsonl`.

O novo probe `d3d12-resources` deve passar e mostrar:

- `budget_source=host_cap` e `budget_bytes=67108864`;
- `created_resources` e `peak_bytes` positivos depois de apresentar ícones;
- `failed_allocations=0` e `last_error=0`;
- `live_resources=0` e `live_bytes=0` ao suspender depois de voltar à Home;
- soma de `default_bytes`, `upload_bytes` e `readback_bytes` igual a
  `live_bytes`, com pico menor ou igual ao teto.

Se suspender ainda em Games, um ícone pronto mantém uma textura DEFAULT
viva, e bytes vivos positivos são esperados. O staging de uploads concluídos
deve ter sido liberado (`upload_bytes=0`). Os contadores pertencem apenas ao
processo atual: uma nova abertura começa uma contagem nova. Não compare bytes
do alocador com o consumo total do app, pois a swapchain e outros recursos
não fazem parte desse orçamento.

A CI acrescenta `phase3.d3d12-resources`, com teste de limite/posse e readback
BGRA com row pitch diferente do tamanho lógico da linha. Falta de orçamento
é exercitada nesse teste de forma controlada, sem exigir um dump corrompido
ou alocação grande no console.

## 3D: descriptors, roots e pipelines

Este bloco modifica C++ e exige instalar um novo MSIX da PR da 3D, após
`build-and-test` e `build-uwp-package` verdes. Continue usando Game.

1. Abra o app. O autoteste de compute roda uma vez por processo na abertura;
   o resultado é registrado automaticamente, sem ação no controle.
2. Entre em Games, confira os dois títulos/ícones e alterne a seleção dez
   vezes. Volte com B para a Home e navegue por Settings/Diagnostics.
3. Vá ao Dev Home e reabra três vezes. Confira interface e ícones em cada
   retorno. Termine na Home do app e vá ao Dev Home para persistir os logs.
4. Exporte `phase0-results.jsonl` e `phase0-lifecycle.jsonl`.

O novo resultado `d3d12-pipelines` deve passar, com `compute_passed=1`,
`root_creations=2`, `graphics_creations=1`, `compute_creations=1`,
`cache_hits=4`, `rtv_slots=2` e `srv_slots=1`. Os hits incluem a verificação
deliberada de pedidos repetidos durante a abertura, não uma medida de
performance de jogos. O dispatch escreve 100–103 em uma textura R32_UINT
2×2; a CPU verifica esses valores após fence e readback.

O probe de recursos passa a incluir duas criações temporárias desse teste,
liberadas ao concluir a GPU. Depois de voltar à Home, `live_resources=0`,
`live_bytes=0` e `failed_allocations=0` continuam sendo esperados. Contadores
não devem crescer por novas criações de PSO durante a navegação/retomada no
mesmo processo. Uma nova sessão reinicia contadores e repete o autoteste.

Na CI Windows, `phase3.d3d12-pipelines` verifica limites de descriptors,
capacidade das roots, rejeição de layout de outro cache, reuso por conteúdo
mesmo em endereços diferentes e distinção de shader/formato/blend. Também
renderiza pixels vermelhos em target offscreen e compara o readback, além
de executar o mesmo autoteste de compute. Habilita a debug layer quando
disponível e rejeita mensagens ERROR/CORRUPTION. As fixtures desktop usam
SM5/DXBC via D3DCompile; o MSIX usa SM6/DXIL via DXC. Só o teste no Series S
valida o segundo caminho em hardware e a apresentação UWP.

## 3E: estados, cópias, clears e resolve

Instale o novo MSIX da 3E como Game após os dois checks Windows verdes.
Nenhum arquivo de jogo novo é necessário: os autotestes são internos ao
host e usam a GPU real do console na abertura de cada processo.

1. Abra o app e confira Home, Games e os ícones dos dois títulos.
2. Alterne os títulos dez vezes. Volte com B e navegue por Settings e
   Diagnostics, conferindo que a apresentação continua normal.
3. Vá ao Dev Home e reabra três vezes. Termine na Home do app e volte ao
   Dev Home para suspender/persistir o relatório.
4. Envie `phase0-results.jsonl` e `phase0-lifecycle.jsonl`.

O novo `d3d12-transfers` deve passar, com `probe_passed=1`,
`resolve_supported=1`, `resolve_sample_count=2` ou `4`, `buffer_copies=2`,
`depth_clears=1`, `resolves=1`, `uav_barriers=1` e `rejected_requests=0`.
`texture_copies` deve ser pelo menos 6: cinco do autoteste de transferências
e uma do compute. Uploads de ícones aumentam esse número. `color_clears`
deve ser o número de frames apresentados mais dois (clears do autoteste).
Transições e no-ops são contados separadamente; no-ops não emitem barriers.

O autoteste confirma por readback: os inteiros de duas cópias de buffer com
offsets, pixels azuis de upload, vermelhos de clear e verdes de resolve, além
de depth 0.5. A textura é 7×3, com offset 512 e row pitch de footprint, não
uma cópia linear que ignore padding. Resolve consulta primeiro o suporte
do formato e MSAA; se ausente, registra `resolve_supported=0` e o probe
geral não passa. A interface pode continuar funcionando para permitir
coleta dos logs; isso não autoriza marcar resolve como validado.

O pico de recursos poderá aumentar bastante por causa da granularidade de
alocação MSAA. Depois de voltar à Home, bytes/recursos vivos continuam zero,
sem falhas e abaixo do teto de 64 MiB. Na abertura há 13 recursos temporários
quando resolve é suportado (11 de transferências e dois de compute), mais
os uploads de ícones. Não são alocações de jogo nem medida de VRAM física.
Os contadores de PSO/root da 3D permanecem iguais; suspensão deve drenar a
GPU e retomada deve apresentar a interface sem alterar estados indevidamente.

A CI acrescenta `phase3.d3d12-transfers` com os mesmos testes de readback
em WARP, rejeições sem alteração de estado, heaps imutáveis, footprint/bounds
inválidos e buffer copiado entre duas submissões (decay para COMMON). A
debug layer, quando disponível, deve ficar sem ERROR/CORRUPTION. Os testes
da 3D continuam executando draw e compute. A CI não substitui o Series S.

## 3F: interface VideoCore e preview UWP

Instale o novo MSIX como Game após os checks Windows verdes. Não é preciso
adicionar jogos, sysmodules ou outro conteúdo ao USB.

1. Abra **Diagnostics**. Deve aparecer `VIDEOCORE DMA PASS` e uma imagem
   com a metade superior verde e a inferior laranja, identificada como
   `SYNTHETIC FRAME NOT A GAME`. Envie uma captura dessa tela.
2. Volte com B, entre em Games e alterne os ícones/títulos dez vezes.
   Volte a Diagnostics e confira novamente as duas faixas.
3. Enquanto estiver em Diagnostics, vá ao Dev Home e reabra. Confira que
   o preview volta corretamente. Repita três vezes; os journals distinguem
   retomada no mesmo processo de uma abertura em processo novo.
4. Termine na Home do app e vá ao Dev Home para persistir. Envie
   `phase0-results.jsonl` e `phase0-lifecycle.jsonl` junto da captura.

`d3d12-videocore` deve passar, informar `interface=VideoCore.GpuCommandSink`,
`source=synthetic_dma_fixture`, `buffer_verified=1`, `texture_verified=1`,
`fills=2`, `copies=1`, `flushes=3`, `synchronizations=4`, `downloads=1`,
`linear_frames=1`, `host_submit_events=2`, `markers=2`, zero rejeições e
zero comandos não suportados na fixture. Marcadores são metadados do host
(`marker_delivery=host_debug_metadata`, `gpu_marker_annotations=0`), não
anotações PIX da GPU. `dma_ticket` deve ser positivo,
`frame_ticket` maior, e o fence concluído deve alcançar ambos. Depois de
visitar Diagnostics, `diagnostic_frames_presented` deve ser positivo.

O autoteste usa o contrato real de forma polimórfica, mas NÃO decodifica
PM4 nem faz boot. `liverpool_bound=0`, `guest_draw_supported=0` e
`guest_dispatch_supported=0` são limites esperados, não falhas escondidas.
Pedidos não suportados são rejeitados na implementação e testados na CI.

**Mudança intencional de baseline de memória nesta build:** o frame de
preview fica vivo para ser reapresentado, inclusive na Home. Após sair de
Games, espere `live_resources=1` e `live_bytes=default_bytes` igual a
`frame_allocation_bytes` informado por `d3d12-videocore`. UPLOAD/READBACK
devem ser zero; não espere o zero absoluto das builds 3C–3E. Enquanto um
ícone estiver ativo em Games, haverá uma segunda textura DEFAULT. Fechar o
renderer libera o frame também. Contadores e baseline devem permanecer
estáveis durante navegação e retomada no mesmo processo.

O heap do shell passa a ter `srv_slots=2`; roots/PSOs e hits permanecem
2/1/1/4. Os autotestes somados geram `buffer_copies=6` e pelo menos oito
`texture_copies`, mais uploads de ícones. Clears/resolve/UAV da 3E continuam
aprovados. Os contadores não representam execução de jogo ou FPS guest.

`phase3.d3d12-videocore` no Windows/WARP valida dados/frame por readback,
limites do registro, intervalos parciais, retenção de staging até o fence,
descarte sem submissão, rejeição de comandos não suportados e amostragem
da textura em um draw offscreen com comparação de pixels. A debug layer
fica sem ERROR/CORRUPTION quando disponível. Só o Series S valida o preview
DXIL no CoreWindow, a navegação e a retomada nesta build.
