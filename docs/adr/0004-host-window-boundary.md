# ADR-0004: fronteira de janela injetada pelo host

- Status: Aceita
- Data: 2026-09-19

## Contexto

O `Core::Emulator` do baseline `v.0.18.0` construía `Frontend::WindowSDL`
diretamente. O tipo SDL também atravessava as interfaces do presenter, swapchain,
instância Vulkan, ImGui e bibliotecas HLE. Um executável UWP teria, portanto, de
instanciar SDL mesmo possuindo seu próprio `CoreWindow` e swapchain D3D12.

## Decisão

Introduzir `Frontend::Window`, um contrato independente da tecnologia do host,
e fornecer sua implementação por `Frontend::WindowFactory`. O executável desktop
injeta `WindowSDL`; o futuro executável UWP injetará uma implementação baseada em
`CoreWindow`.

O contrato expõe dimensões, ciclo de eventos, informação de superfície nativa e
um handle opaco opcional do frontend. O handle opaco mantém os adaptadores SDL
existentes de ImGui e mouse funcionando, mas não é consumido pelo núcleo do
emulador nem pela criação de superfície Vulkan.

## Consequências

- `Core::Emulator` deixa de depender de `WindowSDL`.
- O backend Vulkan passa a consumir `Frontend::Window`.
- SDL permanece somente como implementação do host desktop e em seus adaptadores
  de input/ImGui.
- O host UWP pode ser desenvolvido sem criar uma janela SDL.
- A remoção do handle opaco dependerá de adaptadores neutros de input e overlay,
  trabalho posterior que não bloqueia o backend D3D12.

## Alternativas rejeitadas

- Condicionar `emulator.cpp` com `#ifdef UWP`: aumentaria o acoplamento de
  plataforma no core.
- Fazer `WindowUWP` herdar de `WindowSDL`: manteria uma dependência artificial de
  SDL e dificultaria o ciclo de vida UWP.
- Aguardar o backend D3D12 para criar a abstração: misturaria a fronteira do host
  com a fronteira gráfica da Fase 2.
