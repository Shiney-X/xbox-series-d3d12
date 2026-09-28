# ADR 0015 — Formato original das image views

- Status: aceito
- Data: 2026-09-27

## Contexto

O ADR 0014 preservou o formato do guest em `ImageInfo`, mas `ImageViewInfo`
ainda o convertia diretamente para `vk::Format`. A regra storage sRGB→UNORM
também ficava embutida no construtor da view, misturando identidade do recurso
com a representação exigida pelo Vulkan.

## Decisão

Acrescentar `guest_format` a `ImageViewDesc` e concentrar a conversão em
`LiverpoolToVK::ImageFormat`. A função recebe a intenção de view storage e
aplica a conversão sRGB→UNORM somente nesse caso. Imagens e views continuam
armazenando o `vk::Format` nativo para o backend Vulkan atual.

Manter a igualdade usada pelo cache Vulkan baseada nos campos anteriores da
view e no formato Vulkan resolvido. Descritores do guest diferentes que
produzam uma view Vulkan idêntica não precisam criar handles duplicados.

## Consequências

- O formato original fica disponível para um futuro adaptador D3D12 sem
  inferência reversa de formatos Vulkan.
- O cache D3D12 precisará de chave e regras próprias; a igualdade Vulkan atual
  não serve como contrato universal de compatibilidade de views.
- Esta mudança não cria recursos D3D12, não altera o boot do emulador no Xbox
  e não exige novo teste no console por si só.
