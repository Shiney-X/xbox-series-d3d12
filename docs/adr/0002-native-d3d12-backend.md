# ADR-0002: backend D3D12 nativo

- Status: aceito
- Data: 2026-09-19

## Contexto

O shadPS4 usa Vulkan e o Xbox Dev Mode não oferece Vulkan nativo. As alternativas
são um backend D3D12 próprio ou uma camada Vulkan-on-D3D12 como Mesa Dozen.
VKD3D-Proton traduz D3D12 para Vulkan e, portanto, tem a direção oposta.

## Decisão

Implementar um backend D3D12 nativo depois de extrair contratos gráficos neutros.
Dozen poderá ser usado em experimentos desktop e como referência diferencial,
mas não será dependência de produção do Xbox.

O bootstrap de shaders usará:

```text
IR do shadPS4 -> SPIR-V -> SPIRV-Cross -> HLSL -> DXC -> DXIL
```

## Consequências

- Maior custo inicial de implementação.
- Menor footprint e mais controle sobre sincronização e residency.
- O backend Vulkan deve permanecer operacional durante a refatoração.
- Um emissor HLSL direto poderá substituir a rota SPIR-V posteriormente.
