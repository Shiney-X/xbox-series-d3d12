# ADR 0037 — Consumidor inicial VideoCore/D3D12 e preview no UWP

- Status: aceito; validação no Series S pendente
- Data: 2026-10-02

## Viabilidade e dependências

O upstream já oferece `VideoCore::GpuCommandSink`; Liverpool envia a essa
interface operações decodificadas de PM4 e Vulkan possui seu consumidor.
Device, recursos, descriptors, pipelines e transferências do host D3D12
foram validados no Xbox. É possível compilar um consumidor parcial dessa
interface sem importar Vulkan, Liverpool, caches ou o runtime inteiro.
O contrato usa apenas tipos comuns; este bloco não altera a ABI da interface
nem o consumidor Vulkan. Não exige biblioteca nova ou GDK.

## Decisão e implementação

- Criar `D3D12VideoCoreBridge : VideoCore::GpuCommandSink` no host, com
  fill/copy real de buffers registrados e sincronização por tickets da 3B.
- Tratar endereços como identificadores de intervalos GPU, nunca como
  ponteiros CPU. Rejeitar overflow, sobreposição, aliases, heap/device
  incompatível, acesso não registrado e cópia do buffer nele mesmo.
- Registro de até 16 buffers DEFAULT em estado inicial COMMON. Operações
  limitadas a 16 MiB; staging lógico por batch também limitado a 16 MiB,
  sujeito à cobrança committed de 64 MiB. Registro não substitui MMU ou
  coerência com CPU guest, e recursos não podem ser mutados externamente.
- Gravar em batch; Flush devolve o ticket real, repetição vazia devolve o
  último ticket sem criar trabalho. Finish/CpSync bloqueiam até conclusão.
  Antes de reciclar allocator, aguardar o batch anterior. OnSubmit não
  libera staging em gravação; destruição não executa gravação descartada.
- Fills de palavra de 32 bits usam UPLOAD e CopyBufferRegion. Expor download
  bloqueante de buffers e importação de frame BGRA8 linear com footprint
  explícito. Não inferir tiling/formatos/estados PS4 não implementados.
- Draw/dispatch guest, GDS e ProcessDownloadImages retornam E_NOTIMPL.
  Não conectar o consumidor a Liverpool até esses caminhos e o runtime
  necessário terem implementação própria.

## Validação e apresentação

A fixture chama a interface polimorficamente: duas fills, uma copy,
marcadores e submissão/sincronização. Readback confirma os pixels no buffer.
Importação para textura BGRA8 64×32 e novo readback confirmam duas faixas,
verde e laranja. A textura é apresentada em Diagnostics pelo shader/PSO
DXIL do shell, com SRV dedicado e rótulo de frame sintético. Conta-se a
apresentação apenas depois de Present bem-sucedido.

O resultado `d3d12-videocore` registra a origem sintética, dados verificados,
tickets, contadores, tamanho da textura retida e apresentações em Diagnostics.
O preview mantém um recurso DEFAULT vivo até encerrar o renderer; staging
e readbacks devem ser liberados antes disso. Troca de ícone só muda seu
próprio slot SRV, após a drenagem da fila.

Windows/WARP verifica o mesmo probe, pedidos inválidos/unsupported,
semântica de tickets, retenção e descarte, e amostragem da textura em draw
offscreen com comparação de pixels. O teste Xbox complementa isso com
preview UWP/DXIL, ícones e suspensão/retomada conforme
[PHASE3_VALIDATION.md](../PHASE3_VALIDATION.md).

## Limites e conclusão da fundação

Esse é o último bloco planejado 3A–3F da fundação experimental do host, não
a conclusão de um renderer PS4 funcional. O MSIX ainda não compila/binda
Liverpool, MemoryManager/GpuMemoryTracker e caches guest, nem traduz shaders
GCN ou inicializa jogos. A fixture não é um trace PM4 capturado e o preview
não é o primeiro frame de um título. Fases 4/5 precisam ligar shaders,
recursos/estados guest e runtime antes de prometer boot ou compatibilidade.
