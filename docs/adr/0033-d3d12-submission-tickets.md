# ADR 0033 — Tickets de submissão e contextos de frame D3D12

- Status: proposto; aguardando teste da 3B no Series S
- Data: 2026-10-02

## Contexto

O shell tinha um allocator/list e esperava a fila inteira após cada Present.
Para suportar trabalho em voo, o allocator não pode ser resetado enquanto
seus comandos estão sendo executados. O ícone também usa um SRV compartilhado;
reescrever esse descriptor ou destruir a textura enquanto um frame o utiliza
introduziria uma falha de vida útil.

## Decisão

`D3D12DeviceContext::Submit` executa uma lista fechada na fila direta e devolve
o ticket do fence sinalizado logo depois. `Wait(ticket)` verifica conclusão
antes de bloquear e rejeita tickets que ainda não foram sinalizados. Uma
falha ao sinalizar bloqueia submissões posteriores, evitando reutilizar um
allocator com um ticket antigo. O valor reservado `UINT64_MAX` é tratado como
remoção de dispositivo, não como conclusão de todas as submissões.

O shell possui dois pares de allocator/list, um por backbuffer, e espera
somente o ticket do par a reutilizar. Ao trocar o ícone compartilhado, drena
a fila antes de alterar o SRV ou liberar a textura anterior. A cópia do ícone
continua esperando seu próprio ticket antes de liberar o upload. Suspensão
e encerramento drenam a fila. As operações continuam serializadas na thread
da UI; sincronização entre várias filas/threads pertence a trabalho futuro.

## Validação

O teste `phase3.d3d12-submission` usa o backend D3D12 WARP no Windows: grava
oito cópias, alterna e reutiliza dois pares de allocator/list, verifica tickets
monotônicos e compara os dados de readback após drenar a fila. Ele também
verifica que uma espera de ticket não sinalizado é rejeitada. O teste tem
timeout de CI; não mede desempenho do Series S nem valida a swapchain.

O procedimento de Xbox está em
[PHASE3_VALIDATION.md](../PHASE3_VALIDATION.md). O relatório inclui contadores
de submissão, reutilização e esperas. A contagem de esperas bloqueantes pode
ser zero se a GPU já tiver terminado; isso não é falha.

## Consequências

O host deixa de impor uma espera completa ao final de cada apresentação.
Isso permite trabalho de até dois contextos de frame, sujeito ao Present e
ao andamento da GPU. A UI continua dirigida por eventos, sem promessa de ganho
mensurável de FPS. Recovery de device removal e integração com comandos PM4
ainda estão pendentes. Uma falha de GPU não autoriza reciclar recursos como
se a execução tivesse terminado.

Referências: [regras de reset de command lists e allocators](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12graphicscommandlist-reset),
[valor do fence em remoção de dispositivo](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12fence-getcompletedvalue).
