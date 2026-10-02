# ADR 0032 — Contexto de dispositivo D3D12 do host UWP

- Status: aceito; abertura, ícones, suspensão e nova abertura validados no Series S
- Data: 2026-10-01

## Contexto

O shell UWP já cria um device D3D12, fila direta, command lists e fence, mas
esses recursos estavam todos sob posse de `D3D12StatusRenderer`. A Fase 3
precisa de um ponto de partida que possa ser usado além do desenho da UI, sem
confundir o shell existente com o backend gráfico do emulador.

## Decisão

Extrair a posse de device, fila direta, fence e evento para
`D3D12DeviceContext`. O contexto cria pares de command allocator/list e
fornece a espera da fila e a operação de trim. O shell continua possuindo
swapchain, targets, descriptors, PSO e lógica de desenho. A ordem de vida dos
objetos garante que o contexto sobreviva aos recursos do renderer.

Não conectar ainda o contexto ao rasterizer do shadPS4. Não criar uma camada
Vulkan-to-D3D12 nem mudar o backend Vulkan desktop neste corte.

## Validação e consequências

A CI Windows deve compilar o MSIX UWP. No Xbox em perfil Game, testar abertura
da home, navegação até Jogos e exibição de `icon0.png`, retorno ao Dev Home e
reabertura do app, além de suspensão/retomada. Guardar o journal de ciclo de
vida e informar se houve tela preta, falha ou ícone ausente. Alteração do
binário UWP exige compilar e instalar um novo MSIX; o merge isolado não muda
o app já instalado no console.

O fence ainda sincroniza o shell de forma bloqueante. Um backend de jogo
precisará de múltiplos frames em voo, política de residency, barriers,
descriptors e pipelines próprios. Este ADR não afirma boot de jogo no Xbox.

Em 2026-10-02, todos os probes passaram no console. O journal registrou três
processos com suspensão; dois jogos e dois ícones foram reconhecidos, com o
ícone selecionado apresentado. Não houve evento `resume` no mesmo processo
nessa amostra. O [ADR 0033](0033-d3d12-submission-tickets.md) substitui a espera
completa por apresentação por uma política de tickets por frame.
