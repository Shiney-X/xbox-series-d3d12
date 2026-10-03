# Fase 4 — Validação no Xbox Series S

## 4A: SPIR-V → HLSL → DXIL compute

1. Aguardar `Xbox UWP shell / build-and-test` e `build-uwp-package` verdes.
2. Instalar o novo MSIX como **Game**, mantendo a biblioteca existente.
3. Abrir Diagnostics: conferir `SPIRV HLSL DXIL PASS` e o preview 3F com
   verde em cima/laranja embaixo. Esse preview continua sendo DMA sintético,
   não resultado visual do shader compute nem frame de jogo.
4. Abrir Games, alternar jogos/ícones e voltar; usar B sem sair ao Dev Home.
5. Voltar ao Dev Home e reabrir. Repetir três vezes, conferindo Diagnostics.
6. Enviar captura, `phase0-results.jsonl` e `phase0-lifecycle.jsonl`.

No resultado `d3d12-shaders`, esperar `passed=true`,
`translation_location=xbox_runtime`, `shader_format=DXIL`,
`reference_hlsl_passed=1` e `translated_readback_passed=1`. O compute escreve
100,101,102,103 em uma textura R32_UINT 2×2; leitura/comparação são automáticas.
Não é necessário fornecer shader, sysmodule ou iniciar jogo.

Os demais probes devem passar. Baseline de pipelines: duas roots, um graphics,
dois compute e sete hits. Transferências incluem um dispatch/readback extra,
portanto dois UAV barriers e uma cópia de textura adicional. O frame 3F
permanece vivo: um recurso DEFAULT/65536 bytes após Home, com UPLOAD/READBACK
zerados, sem falhas de alocação. Contagens de frames e navegação variam.

O journal deve demonstrar suspensão e, quando houver retomada no mesmo
processo, resume seguido de apresentação. Nova abertura com outro session
é teste de relaunch, não prova de resume.

## Teste portátil de desenvolvimento

```sh
git submodule update --init --recursive -- externals/sirit externals/ext-boost externals/fmt externals/magic_enum externals/half
cmake -S tests/probes/shaders -B out/shader-tools -DCMAKE_BUILD_TYPE=Debug
cmake --build out/shader-tools -j 4
ctest --test-dir out/shader-tools --output-on-failure
```

Isso testa o tradutor e os limites, sem emular Xbox. A CI Windows complementa
com WARP (fixtures antigas DXBC; emissor upstream DXC/DXIL); a validação
UWP/GPU do Xbox exige o console. A 4A não valida shaders
guest, compatibilidade de jogos ou o compilador de ISA PS4.

## 4B: ABI PushData / root constants

Instalar o MSIX novo como Game e repetir navegação/ícones/Dev Home/reabertura
da 4A. Em Diagnostics, conferir **PUSH DATA ABI PASS**, os PASS anteriores
e as faixas. Enviar captura, `phase0-results.jsonl` e `phase0-lifecycle.jsonl`.

O probe `d3d12-shader-push-data` deve passar: ABI 120 bytes, 30 root words,
b0/space0, 16 user registers e 40 offsets compactados. O shader lê os
quatro floats, todos os registradores e todos os bytes de offsets; os
resultados são 1066,1067,1068,1069 em 2×2. Verificação é automática; não
é preciso colocar novos arquivos de PS4 no USB.

Baseline da 4B: três roots, um graphics, três compute, nove hits e três
UAV barriers. O frame 3F continua retido; após Home, DEFAULT=65536,
UPLOAD/READBACK=0 e nenhuma falha/rejeição. Tickets devem concluir ao
suspender; distinguir resume na mesma sessão de relaunch em novo processo.

`upstream_emitter_linked=0` é esperado. O tipo PushData é realmente o do
shadPS4, mas o módulo SPIR-V deste teste ainda é autoral, não foi produzido
por `EmitSPIRV`. Não comprova execução de shaders guest/PM4.

Validada no Series S, sessão `134354603362413650-5884`, 18 probes positivos
e captura correta. Retomada confirmada pelo usuário; não inferida do journal
que contém lançamento em outra sessão.

## 4C: emissor SPIR-V real / IR do shadPS4

Instalar o MSIX novo como **Game**, após os checks UWP passarem. Abrir
Diagnostics: conferir **SHAD EMITTER PASS**, **PUSH DATA ABI PASS**, os
demais PASS e o preview verde/laranja. Os sete textos à esquerda agora
ficam dentro do painel, com espaçamento menor, sem sobrepor o preview.
Repetir Games/ícones/B/Dev Home/reabertura e retomada. Enviar captura,
`phase0-results.jsonl` e `phase0-lifecycle.jsonl`.

O novo `d3d12-shader-upstream` deve ter `passed=true`,
`upstream_emitter_linked=1`, `source=authored_shadps4_ir`, `guest_isa=0`,
`guest_runtime_linked=0`, `shader_format=DXIL` e `readback_passed=1`.
A IR autoral é emitida por `Shader::Backend::SPIRV::EmitSPIRV` no Xbox;
dispatch/readback real confere 100,101,102,103. Não precisa de novos arquivos
de PS4 nem boot de jogo.

Baseline esperada: 3 roots, 1 graphics, 4 compute, 11 hits e 4 UAV barriers.
DEFAULT retido=65536 bytes; UPLOAD/READBACK=0 após conclusão; nenhuma
falha de alocação/rejeição. Contagens de frames/tickets variam. A tela
permanece preview DMA sintético, não o primeiro frame de um jogo.

No desenvolvimento, o teste portátil emite/traduz a IR duas vezes e
verifica determinismo e mutação de constante. Para a IR upstream, WARP
compila HLSL com DXC para DXIL usando `main/cs_6_0/-Ges/-O3`, como no Xbox;
verifica valores GPU, reprova o oracle 100–103 para IR alterada e verifica
200–203 com o oracle correto. As fixtures 4A/4B continuam tendo o teste
legado DXBC; o emissor requer o caminho DXIL. AppContainer e GPU do Xbox
exigem o console para aceitação.

Validada 4C no Series S: sessão `134354649597873641-3192`, 20 probes
positivos, captura correta e retomada na mesma sessão demonstrada no journal.

## 4D: vértice, fragmento e bindings por estágio

Após `build-and-test` e `build-uwp-package` verdes, instalar o novo MSIX
como **Game**. Abrir Diagnostics: conferir **SHAD VS PS PASS**, os PASS
anteriores e o preview verde/laranja. Navegar Games/ícones/B e testar
Dev Home/retomada na mesma sessão. Enviar captura e os dois logs habituais.

`d3d12-shader-graphics` deve passar, com `stages=vertex,fragment`,
`source=authored_shadps4_ir`, `shader_format=DXIL`, `vs_cbv=b0_space1`,
`ps_cbv=b0_space2`, `root_words=60` e `readback_passed=1`.
O teste é automático: dois draws fullscreen 4×4, primeiro todos os pixels
RGBA=255,0,0,255 e depois 0,255,0,255. As constantes são trocadas entre VS
e PS usando a ABI real; o canal vermelho atravessa location 0/TEXCOORD0.
Um clear azul permite detectar ausência de desenho. Não precisa de novos
arquivos PS4 nem apertar botão para executar o probe.

Os draws são offscreen; o preview visível continua sendo o DMA 3F,
não a imagem desse teste e não um frame de jogo. Baseline: 4 roots,
2 graphics, 4 compute, pelo menos 13 hits; sem novos UAVs. Após os testes,
UPLOAD/READBACK=0 e nenhum allocation failure/rejeição. Os recursos do
probe são temporários; o frame DMA de 65536 bytes continua retido.

O teste portátil verifica emissão determinística, ABI/remap e rejeição de
estágio/location/layout incompatíveis. Windows WARP usa DXC/DXIL e compara
os 32 pixels; um PS azul deliberadamente incorreto deve falhar no oracle.
Isso não substitui a aceitação no Xbox. Não há texturas, samplers, SSBOs,
vertex buffers guest, GCN de jogo, PM4 ou core completo nesta 4D.

Validada 4D: 20 probes positivos em `134354665468983635-5052`, 32 pixels
corretos, 4 roots/2 graphics/4 compute/14 hits, UPLOAD/READBACK zerados
e captura SHAD VS PS PASS. O journal registra resume anterior em
`134354665134013638-6320`, sem apresentação posterior registrada. A sessão
dos resultados é outra abertura; não comprova retomada por si só.

## 4E: regressão e fechamento do contrato inicial

Esta etapa não altera o executável UWP: o pacote da PR 41 continua sendo
o alvo de teste. Não é necessário reinstalar um MSIX só para validar logs
ou atualizar documentação. A CI recompila para verificar a integração.

Gate inicial:

- Testes portáteis de emissão/reflection/determinismo e rejeição de layouts.
- Windows WARP/DXC/DXIL: compute, ABI completa e últimos campos, draws VS/PS,
  mudanças de constantes, PSO cache e oracles negativos.
- Logs/captura do Xbox: PASS nos probes, pixels declarados corretos,
  nenhum allocation failure e nenhum UPLOAD/READBACK retido após os probes.
- Ciclo de vida: registrar sessão, distinguir relaunch de resume e não
  inferir apresentação a partir de um resume isolado.
- Pendências guest e golden cross-backend explicitamente preservadas.

### Conferir os logs exportados

Requer Python 3, somente biblioteca padrão; não modifica arquivos ou console.

```sh
python3 scripts/validate_phase4.py \
  --results "/home/henrique/Área de trabalho/phase0-results.jsonl" \
  --lifecycle "/home/henrique/Área de trabalho/phase0-lifecycle.jsonl" \
  --require-resume
python3 -m unittest discover -s tests/phase4 -v
```

Retorna JSON e exit code 0 para evidências consistentes; exit code 1 para
erro, probe ausente/duplicado/falhando, ABI/oracle/binding incorreto ou
journal incompatível. `--require-resume` exige suspend/resume ordenado em
alguma sessão conhecida do journal, não necessariamente a de resultados.
O relatório separa `results_session`, `resume_sessions` e
`presentation_after_resume_sessions`. Warning sobre apresentação é uma
limitação de evidência, não falha do shader. Os detalhes são declarações
do probe; o script não repete o readback nem valida autenticidade do arquivo.

Para acrescentar evidência de apresentação após resume, usar o mesmo MSIX:
abrir Diagnostics, voltar ao Dev Home, reabrir e navegar após a retomada.
Exportar novamente o journal e conferir as sessões. Se surgir um novo
processo, é relaunch; não registrar como resume.

### Corpus de regressão disponível

| Caso | Oracle independente | Onde é verificado |
| --- | --- | --- |
| Compute original e SPIR-V autoral | 100–103 | WARP e Xbox |
| PushData completo / campos finais | 1066–1069 / 117–120 | WARP; completo no Xbox |
| Compute IR upstream / constante alterada | 100–103 / 200–203 | WARP; original no Xbox |
| VS/PS IR upstream, constantes por estágio | 16 pixels vermelhos, depois 16 verdes | WARP e Xbox |
| Compute ou PS deliberadamente incorretos | Rejeição do oracle correto | WARP |
| Layout/ABI/estágio/location incompatíveis | Rejeição antes da GPU | Teste portátil |

Isso é corpus **sintético com oracles**, não corpus golden Vulkan/D3D12.
Comparação cross-backend e shaders GCN reais continuam pendentes. Não
redistribuir jogos/sysmodules para preencher o corpus. A fundação pode
avançar à integração guest; suporte a um jogo só será declarado após gates
de loader/CPU/HLE, PM4, shaders e recursos reais.
