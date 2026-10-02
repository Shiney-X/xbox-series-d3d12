# ADR 0035 — Descriptors e cache de pipelines do host D3D12

- Status: aceito; validado no Series S
- Data: 2026-10-02

## Viabilidade e dependências

Device, submissão por tickets e recursos committed já foram validados no
Xbox. A root signature, PSO e heaps da interface ainda eram privados do
renderer. Este bloco os organiza sem depender de Vulkan, GDK ou novas
bibliotecas. Usa D3D12 e o DXC já empacotado; os testes Windows usam WARP.

## Decisão

- Introduzir arena de descriptors não copiável, slots fixos e validação de
  capacidade/visibilidade. Os heaps do shell reservam dois RTVs e um SRV.
- Não reciclar descriptors automaticamente. O chamador drena a fila antes
  de sobrescrever um slot que possa estar em uso pela GPU.
- Serializar root signatures v1 e reutilizar por igualdade dos bytes.
- Criar PSOs graphics/compute em um cache não copiável, por device, com até
  64 entradas por categoria e rejeição explícita quando cheio. Hits continuam
  disponíveis no limite; não há eviction de objetos ainda em uso.
- Usar igualdade exata de chaves proprietárias: layout serializado,
  bytecodes e estados expostos. Não comparar estruturas nativas com padding,
  ponteiros ou hashes sem confirmação de igualdade.
- Expor inicialmente graphics com triângulos, um color target e blend
  opcional premultiplicado; sem vertex inputs, depth, MSAA ou outros estágios.
  A API não aceita silenciosamente estados guest não implementados.
- Manter cache apenas em memória, acesso serializado e vida útil protegida
  pelos tickets. Disco/pipeline libraries e políticas de eviction ficam para
  quando houver estados/shaders guest e uma política de compatibilidade.

## Implementação e validação

O shell usa os componentes reais, sem mock. Na abertura, a mesma interface
DXC estável compila um shader compute sintético SM6. Uma textura UAV R32_UINT
2×2 recebe os valores 100–103; a verificação CPU usa readback após fence.
Pedidos repetidos de root/graphics/compute verificam a identidade do cache.
O relatório registra `d3d12-pipelines`; recursos temporários entram no
orçamento da 3C e são liberados ao concluir o teste.

O teste `phase3.d3d12-pipelines` usa shaders SM5/DXBC compilados por
D3DCompile e D3D12/WARP, verifica limites/chaves e compara pixels de um draw
offscreen e valores do dispatch. A compilação MSIX cobre a integração DXC,
mas execução DXIL e apresentação no Series S precisam do teste manual em
[PHASE3_VALIDATION.md](../PHASE3_VALIDATION.md).

## Limites

Em 2026-10-02, a captura da sessão `134354436005173693-1712` aprovou
compute/readback, criação/reuso dos caches e todos os 14 probes. Foram
registradas duas roots, um PSO graphics, um compute, quatro hits e nenhum
recurso vivo/falha após voltar à Home e suspender. O fence concluiu o ticket
18. O journal também registrou duas retomadas com apresentação na sessão
anterior `134354435676283705-3048` e uma nova abertura.

Os caches não incluem todos os estados PS4, nem carregam shaders guest.
Ampliar os estados suportados exige ampliar a chave no mesmo commit e
adicionar testes para distinguir os novos estados. Capacidade de entradas
não é budget em bytes de GPU; PSOs, roots e descriptors permanecem fora do
teto de buffers/texturas da 3C. Este bloco não executa jogos ou PM4.
