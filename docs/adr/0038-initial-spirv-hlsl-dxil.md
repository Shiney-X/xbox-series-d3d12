# ADR 0038 — Primeiro caminho SPIR-V/HLSL/DXIL no UWP

- Status: aceito; validação no Series S pendente
- Data: 2026-10-02

## Viabilidade e dependências

O recompiler do shadPS4 possui `Shader::Backend::SPIRV::EmitSPIRV`.
Reusar esse formato evita criar imediatamente um emissor HLSL inteiro.
Mas seus requisitos Vulkan, descriptors, wave ops e layouts precisam de
adaptação explícita; SPIRV-Cross não fornece automaticamente um renderer PS4.

Fixar SPIRV-Cross em `aa217aeb6c9f0ace7a0ab233b28807edf45eb165`, arquivo com
SHA-256 `b99db6ee6182dbeb102257e755bf6d4978c23dcfd3329033304779279bc27212`.
FetchContent baixa fontes verificadas; o MSIX compila core/GLSL/HLSL para
AppContainer. Não utiliza DLL desktop, Vulkan, Mesa ou GDK. Notices MIT são
distribuídos no pacote. O build exige rede no primeiro download.

## Implementação

- Wrapper C++20 compartilhado entre testes e UWP. Traduz no Xbox em runtime.
- Contrato inicial compute, um entry point, LocalSize literal e uma imagem
  2D R32_UINT não array/MSAA em set 0/binding 0, mapeada para u0/space0.
- Rejeitar outros layouts de recursos, bindings e grupos fora dos limites
  cs_6_0. Envelope até 65536 words/IDs; conferir magic/version e limites
  de instruções antes do parser. Isso não substitui um validador semântico
  SPIR-V: neste bloco só se consome a fixture interna confiável, não arquivos
  de shader externos. Validação semântica de corpus guest fica pendente.
- Fixture SPIR-V autoral 1.0 produz `100+x+2*y` em 2×2. Não veio da ISA PS4.
- Compilar HLSL emitido via IDxcCompiler, main/cs_6_0, DXIL; dispatch e
  readback pelo mesmo encoder/allocator/cache da Fase 3. Comparar com a
  expectativa CPU fixa, não com sucesso de compilação apenas.
- Manter o shader HLSL de referência e seu teste. Duas variantes de compute
  têm PSOs por conteúdo; mesma root pode ser reutilizada. Nenhum fallback
  mascara falha de tradução/compilação/readback.

## Verificação e limites

Teste portátil: tradução determinística, reflection do grupo e rejeições
de truncamento/header/instrução/binding/grupo. Windows/WARP compila o HLSL
emitido com D3DCompile/cs_5_1 (DXBC) e verifica os quatro valores na GPU,
além do shader original e debug layer. **Não chamar esse teste de DXIL**:
DXC/cs_6_0 e AppContainer são verificados pelo MSIX/console.

O Xbox registra `d3d12-shaders` com origem, revisão, local de tradução,
binding, DXIL e readbacks. Diagnostics mantém o preview 3F e adiciona
SPIRV HLSL DXIL PASS. Não há shader guest, shader runtime/cache persistente,
reflection geral, graphics guest, waves/subgroups ou integração com Liverpool.
Próximo gate: estudar o SPIR-V realmente emitido pelo shadPS4 e adaptar seu
perfil e os bindings antes de conectar o recompiler.

Referências: [SPIRV-Cross](https://github.com/KhronosGroup/SPIRV-Cross),
[API HLSL da revisão fixada](https://github.com/KhronosGroup/SPIRV-Cross/blob/aa217aeb6c9f0ace7a0ab233b28807edf45eb165/spirv_hlsl.hpp).
