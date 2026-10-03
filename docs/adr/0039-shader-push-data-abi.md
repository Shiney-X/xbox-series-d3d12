# ADR 0039 — ABI PushData do shadPS4 em root constants D3D12

- Status: aceito; validado no Series S
- Data: 2026-10-02

## Viabilidade e dependências

Inspeção do emissor mostrou que `EmitContext::DefineInterfaces` sempre
chama `DefinePushDataBlock`. A 4A rejeitava qualquer push constant, portanto
a ligação direta com `EmitSPIRV` ainda falharia mesmo em shaders simples.
Também persistem dependências de logging, settings e cache Vulkan no emissor;
este bloco não finge que essas dependências foram resolvidas.

O bloco AuxData usa offsets 0,4,8,12,16,32,48,64,80,96,112, com quatro
floats, seis uint4 e um uint2. Os três últimos membros são a representação
compactada de 40 offsets em bytes, não 40 uints. O tipo C++ PushData ocupa
120 bytes. Uma tabela UAV + 30 constantes custa 31 DWORDs da root, abaixo
do limite D3D12; não é necessário um buffer CBV/upload permanente aqui.

## Implementação

- Extrair o tipo real PushData e constantes de contagem para header de ABI.
  Não copiar outro struct no host nem mudar nomes, ordem, tamanho ou defaults.
  AddOffset continua inline em resource.h, com o mesmo ASSERT/código.
  static_assert fixa tamanho, alinhamento, offsets e trivial copyability.
- Reflection da tradução aceita zero ou um bloco. Quando presente, exigir
  11 membros, tipos/escalas vetoriais exatos, offsets explícitos, sem arrays
  ou matrizes e tamanho 120. Rejeitar layout incompatível antes do HLSL.
- SPIRV-Cross remapeia push constants para b0/space0; root constants
  fornecem 30 palavras little-endian com bit_cast, sem reinterpretar bytes
  compactados como valores separados de 32 bits.
- Fixture autoral com o layout real lê todos os campos: floats 1–4,
  user registers 1–16 e offsets 1–40. Soma independente 10+136+820,
  acrescida de 100+x+2y: readback esperado 1066–1069.
- DXC/DXIL/dispatch/readback no Xbox, sem fallback. Relatório informa
  `source=authored_spirv_push_data_fixture;upstream_emitter_linked=0`.
  Preview DMA da 3F é preservado e não representa a saída desse compute.

## Verificação e limites

Validação Series S: sessão `134354603362413650-5884`, todos os 18 probes
positivos, readback 1066–1069, captura com PASS e preview correto. Retomada
confirmada manualmente pelo usuário; o journal enviado documenta relaunch
e suspensão, não resume na mesma sessão.

Teste portátil valida ABI, packing, reflection, tradução determinística e
rejeição de offsets/tipos incompatíveis. WARP/DXBC verifica todos os campos,
atualização dos valores, último user register/byte, rejeição de root de 29
palavras e oracle que reprova resultados errados. GPU/allocator são reais.
CI upstream precisa compilar a extração do header para assegurar regressão
do caminho Vulkan; mudanças de comportamento do renderer não são pretendidas.

O Xbox executa o caminho DXIL com a mesma ABI. Ainda não compila `EmitSPIRV`,
não usa IR de um jogo e não integra guest buffers/SSBO, waves, DMA físico ou
PM4. A reflection é um gate estrito deste layout, não suporte geral a cbuffer.
Próximo gate continua sendo a ligação do emissor/IR com perfil restrito e
isolamento de suas dependências de renderer/runtime.

Referências de implementação: `src/shader_recompiler/resource.h`,
`backend/spirv/spirv_emit_context.cpp::DefinePushDataBlock` e
[API HLSL fixada](https://github.com/KhronosGroup/SPIRV-Cross/blob/aa217aeb6c9f0ace7a0ab233b28807edf45eb165/spirv_hlsl.hpp).
