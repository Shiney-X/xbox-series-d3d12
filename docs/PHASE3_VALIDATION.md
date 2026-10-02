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
