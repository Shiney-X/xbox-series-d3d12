# ADR 0034 — Recursos committed com posse e orçamento no host D3D12

- Status: aceito; validado no Series S
- Data: 2026-10-02

## Contexto e viabilidade

O shell cria sua textura de ícone e staging diretamente no renderer. O
backend precisa de um ponto comum para criar buffers/texturas, atribuir
posse, controlar alocações e diagnosticar falta de memória. O host já dispõe
de D3D12 e tickets de fence; este corte não exige nova biblioteca nem GDK.

O limite de processo observado no Xbox não é um budget de residência de GPU.
Para os poucos recursos do shell, committed resources e um teto explícito
são suficientes para testar criação e vida útil antes de introduzir heaps
compartilhados, pools ou eviction.

## Decisão

- Criar `D3D12ResourceAllocator` para buffers DEFAULT/UPLOAD/READBACK e
  texturas 2D DEFAULT. O caminho retorna HRESULT e preserva a saída na falha.
- Usar `D3D12Resource` como proprietário movível, não copiável. O chamador
  deve manter o proprietário até o fence de seu último uso terminar.
- Cobrar o tamanho informado por `GetResourceAllocationInfo`. O teto do
  host é 64 MiB, separado da RAM do app e do budget do driver. Uma criação
  que ultrapasse o teto retorna `E_OUTOFMEMORY` sem criar o recurso.
- Contabilizar recursos vivos, bytes por heap, pico, criações e falhas. A
  destruição devolve a contabilidade, e a movimentação transfere a posse
  sem contar o mesmo recurso duas vezes. Substituir uma saída viva exige
  espaço para a nova criação antes de liberar a antiga.
- Integrar textura e staging dos ícones ao alocador. Swapchain, descriptors,
  PSOs e memória de CPU permanecem fora dessa contagem.

## Validação

O teste Windows `phase3.d3d12-resources` usa D3D12/WARP. Ele verifica moves,
reset, sobrevivência do recurso ao objeto alocador, rejeição por tamanho/teto
e preservação da saída em uma falha. Uma textura BGRA 63×17 percorre
UPLOAD → DEFAULT → READBACK com padding de row pitch, e os pixels retornados
são comparados. Depois do fence e da liberação, os bytes vivos devem ser zero.

No Xbox, a troca de ícones e retorno à Home exercitam a mesma implementação.
O relatório `d3d12-resources` deve mostrar pico positivo, nenhuma falha e
zero recursos vivos após sair da biblioteca. O procedimento completo está
em [PHASE3_VALIDATION.md](../PHASE3_VALIDATION.md).

Em 2026-10-02, a sessão `134354421747283697-2788` passou todos os probes
no Series S. A primeira captura mostrou uma textura DEFAULT de 262144 bytes
e nenhum staging vivo, após 34 criações. A segunda, depois de voltar à Home,
confirmou `live_resources=0`, `live_bytes=0` e todos os heaps contabilizados
zerados, preservando o mesmo número de criações. O pico foi 524288 bytes,
sem falhas de alocação. A GPU concluiu o ticket 108; suspensão e retomada
com reapresentação no mesmo processo foram aprovadas.

## Consequências e limites

O alocador restringe bytes de alocação conhecidos pelo host; não consulta nem
promete residência física. A política inicial é `implicit_no_eviction`,
sem chamadas explícitas a MakeResident/Evict, heaps placed ou subalocação.
A memória dos jogos e o comportamento sob pressão global precisam de
política própria na integração com o VideoCore. Esse corte não executa PM4
ou shaders de jogos e não muda o backend Vulkan desktop.

Referência: [tamanho e alinhamento de alocação por adaptador](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12device-getresourceallocationinfo%28uint_uint_constd3d12_resource_desc%29).
