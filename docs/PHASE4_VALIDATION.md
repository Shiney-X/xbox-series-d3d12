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
dois compute e seis hits. Transferências incluem um dispatch/readback extra,
portanto dois UAV barriers e uma cópia de textura adicional. O frame 3F
permanece vivo: um recurso DEFAULT/65536 bytes após Home, com UPLOAD/READBACK
zerados, sem falhas de alocação. Contagens de frames e navegação variam.

O journal deve demonstrar suspensão e, quando houver retomada no mesmo
processo, resume seguido de apresentação. Nova abertura com outro session
é teste de relaunch, não prova de resume.

## Teste portátil de desenvolvimento

```sh
cmake -S tests/probes/shaders -B out/shader-tools -DCMAKE_BUILD_TYPE=Debug
cmake --build out/shader-tools -j 4
ctest --test-dir out/shader-tools --output-on-failure
```

Isso testa o tradutor e os limites, sem emular Xbox. A CI Windows complementa
com WARP/DXBC; a validação DXIL/UWP exige o console. A 4A não valida shaders
guest, compatibilidade de jogos ou o compilador de ISA PS4.
