# ADR 0041 — VS/PS upstream e constantes separadas por estágio

- Status: implementado e validado no Series S (escopo gráfico inicial)
- Data: 2026-10-02

## Viabilidade e dependências

Reutilizar a biblioteca AppContainer real IR/EmitSPIRV/Sirit da 4C,
SPIRV-Cross fixado e DXC do SDK. Não introduzir Vulkan no UWP, não depender
de assets PS4 e não substituir o emissor upstream por HLSL manual.

## Decisão e implementação

Construir IR autoral de VS fullscreen via VertexId e Position0, exportando
um Param0 float4; o FS lê Param0 e exporta RenderTarget0. Cada estágio lê
S0 por GetUserData, usando o layout real Shader::PushData. EmitSPIRV produz
os dois módulos no console, traduzidos e compilados como vs_6_0/ps_6_0.

Reflection aceita somente location 0 float4 e a ABI PushData exata.
Builtins ativos: VertexIndex e Position no VS; nenhum adicional no FS.
Rejeitar entry point/estágio diferente, recursos ou layouts não suportados.
Isso é um contrato fechado para fixtures internas, não um validador completo
de SPIR-V arbitrário. Não aceitar módulos de jogos nesse caminho ainda.

Mapear VS a b0/space1 e PS a b0/space2, ambos de 30 DWORDs, com visibilidade
de root por estágio: 60 DWORDs, abaixo do limite 64. Evita colisão entre
PushData independentes. Não é layout escalável para todos os recursos de
jogos: adicionar descritores exigirá rever orçamento e CBVs, não expandir
essa root indefinidamente. Varying location 0 vira TEXCOORD0, RT vira SV_Target0.

O probe compartilhado WARP/UWP cria RT RGBA8 4×4 e readback, desenha duas
vezes com o mesmo PSO, trocando constantes: VS=1/PS=0 gera vermelho;
VS=0/PS=1 gera verde. Confere todos os pixels sem tolerância; clear azul
impede sucesso sem rasterização. Root/PSO repetidos devem acertar o cache.
Os recursos são liberados após fence/readback. Não mudar o oracle para
acomodar erros de tradução. Nenhuma mudança no renderer Vulkan desktop.

## Verificação e limites

Teste portátil: determinismo, mapping/ABI e rejeições. Windows WARP/DXC:
draw/readback e oracle negativo (PS azul). UWP: novo probe no JSON e
SHAD VS PS PASS em Diagnostics, mantendo navegação e retomada.
Aceitação no console só será marcada após evidências.

Não suporta guest GCN/frontend/resource tracking, vertex fetch, texturas,
samplers, SSBOs, MRT, tessellation ou geometria. A imagem visível continua
DMA sintético. A 4E fecha regressões do escopo inicial, sem declarar backend
completo ou primeira imagem de um jogo.

Aceitação 4D: 20 probes positivos em `134354665468983635-5052`, incluindo
32 pixels corretos, bindings por estágio e captura SHAD VS PS PASS.
UPLOAD/READBACK zerados, zero falhas de alocação. O journal tem resume na
sessão anterior `134354665134013638-6320`, sem apresentação posterior
registrada; a sessão de resultados é relaunch. A 4E preserva essa ressalva.
