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
com WARP/DXBC; a validação DXIL/UWP exige o console. A 4A não valida shaders
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
