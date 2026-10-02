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
