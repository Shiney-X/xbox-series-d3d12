# ADR 0040 — Emissor SPIR-V upstream no UWP, a partir de IR autoral

- Status: implementado e validado no Series S
- Data: 2026-10-02

## Viabilidade e dependências

O backend upstream é compilável separadamente do renderer Vulkan. Usa
Sirit/SPIRV-Headers, Boost headers, fmt, magic_enum, half e a IR real.
Versões vêm dos gitlinks já fixados no repositório; SPIRV-Cross mantém seu
archive/hash. Não carregar DLL desktop nem substituir o emissor por mock.
Uma biblioteca estática CMake WindowsStore é compilada para AppContainer
e linkada ao MSIX. Licenças das dependências seguem no pacote.

## Decisão e implementação

- Introduzir `EmissionOptions`: política de DMA e fetch shader previamente
  parseado são fornecidos pelo host, não consultados no singleton desktop.
  Vulkan preserva sua consulta a settings e usa o mesmo parser real antes
  da emissão; o UWP compute fornece opções vazias, sem DMA/fetch.
- Extrair constantes de página para `BufferAddressLayout`, compartilhadas
  com BufferCache. Valores 14/16384 não mudam e nenhuma API Vulkan entra
  no compilador isolado.
- Logging standalone é uma configuração privada da biblioteca: mensagens
  vão ao debugger no Windows e stderr no desenvolvimento. Assertions e
  operações inalcançáveis lançam erro no probe, sem sucesso artificial ou
  fallback. Não altera o logger/assert handler do executável desktop.
- Compilar todos os TUs do backend SPIR-V e implementações reais de IR e
  Sirit. Métodos deducing-this de duas funções de acesso passam a overloads
  const/não-const equivalentes para GCC 13. Acrescentar includes explícitos.
- Corrigir quatro spaceship operators defaultados com retorno `bool`,
  inválido para `<=>`, para retorno `auto`; igualdade membro-a-membro é
  preservada. Remover constexpr de GetSharp que chama leitura não-constexpr.
- MSVC exige UTF-8 para fmt e não pode compilar a convenção SysV do
  walker SRT. No compilador standalone o walker é apenas um handle de
  código opaco; tentar executá-lo falha explicitamente. Não trocar a ABI
  guest por ABI Windows nem alegar que SRT/JIT está portado. Desktop
  mantém seu tipo SysV sem esse define privado. O probe libera use lists
  e destrói instruções antes de liberar o armazenamento do ObjectPool.
- Construir IR compute autoral com LocalInvocationId, multiplicação,
  adição, bitcast e ImageWrite. Descriptor PS4 autoral representa uma
  imagem R32_UINT 2×2; grupo 2×2×1. O shader escreve 100+x+2y.
- Invocar `EmitSPIRV` real, reflection/mapeamento 4B, SPIRV-Cross, DXC
  e GPU/readback real. `d3d12-shader-upstream` identifica exatamente essa
  origem. Não falsificar `guest_isa`/`guest_runtime_linked`.
- Reorganizar Diagnostics em sete linhas menores dentro do painel;
  manter preview e navegação existentes.

## Verificação e limites

Teste portátil: emissão determinística, ABI real/LocalSize e constante
alterada muda HLSL. Windows WARP/DXC/DXIL: readback correto; constante 200 rejeita
oracle original e passa oracle 200–203, sem filtrar erros da debug layer.
CI precisa compilar o renderer Vulkan após a nova passagem de opções.
Aceitação Series S segue PHASE4_VALIDATION; não marcada antes do teste.

A CI revelou que FXC/DXBC compila o bit carrier `asuint(float4(asfloat(...)))`
do emissor em `store_uav_typed ... l(0,0,0,0)`: readback 0/0/0/0, não
100–103. O teste da IR upstream passa a usar DXC/DXIL com os mesmos entry,
target, flags e interfaces do Xbox, sem mudar o oracle ou editar o HLSL.
DXBC permanece apenas nos probes anteriores; sua aprovação não deve ser
usada como substituto da compatibilidade DXIL. DLLs DXC/DXIL vêm do SDK e
são copiadas junto ao teste Windows.

Não há GCN extraído, frontend/otimização/resource tracking de shaders guest,
shader de jogo, Liverpool/PM4, boot ou core completo neste bloco. O uso de
descriptor autoral não prova acesso à memória guest. A imagem na tela é
o preview sintético 3F, não resultado visual deste compute. Próximos blocos
planejados: 4D bindings/vertex/fragment iniciais e 4E regressão/fechamento.

Aceitação 4C: sessão `134354649597873641-3192`, 20 probes positivos;
readback 100–103, captura SHAD EMITTER PASS e suspend/resume registrado
na mesma sessão. Não amplia a conclusão a shaders de jogos.
