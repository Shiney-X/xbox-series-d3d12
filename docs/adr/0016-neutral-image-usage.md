# ADR 0016 — Capacidades de uso neutras para imagens em cache

- Status: aceito
- Data: 2026-09-27

## Contexto

O cache de texturas escolhia as capacidades de imagens diretamente com
`vk::ImageUsageFlags`. Isso misturava a política do emulador com flags e recursos
opcionais do dispositivo Vulkan, dificultando a criação de um backend D3D12.

## Decisão

Introduzir `VideoCore::ImageUsage` para transferência, sampling, attachment e
storage, e `CachedImageUsage` para preservar a política de alocação atual de
imagens coloridas, depth e comprimidas. `ImageInfo::Usage()` fornece essas
capacidades sem tipos de API. A criação Vulkan converte o resultado para suas
flags nativas e só acrescenta attachment feedback loop quando suportado.

## Consequências

- O comportamento Vulkan de uso da imagem permanece equivalente, inclusive o
  pedido antecipado de storage para color/compressed e sua interação com
  extended usage.
- Um backend D3D12 deverá interpretar as capacidades segundo seus próprios
  recursos, descriptors e estados; a correspondência não é necessariamente 1:1.
- Transições, layouts, barriers e handles de imagens continuam específicos do
  Vulkan. Este recorte não habilita renderização D3D12 nem boot no Xbox.
