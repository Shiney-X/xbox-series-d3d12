<!-- SPDX-FileCopyrightText: 2026 Shiney-X and xbox-series-d3d12 contributors -->
<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Fase 5 — Execução guest e caminho ao primeiro frame

## 5A: auditoria e preflight de executáveis (não é boot)

Instalar o MSIX desta etapa como **Game**, após os checks UWP verdes.
Manter Deltarune e/ou Sonic Mania na biblioteca USB existente. Em Games,
usar X RESCAN ou selecionar a pasta e aguardar a varredura.

Abaixo dos metadados do jogo, esperar um dos estados:

- `ELF HEADER OK NOT BOOTED`: cabeçalho ELF x86-64 reconhecido.
- `SELF HEADER OK NOT BOOTED`: cabeçalho SELF/ELF embutido reconhecido.
- `EBOOT HEADER UNSUPPORTED`: prefixo truncado/formato não suportado;
  verificar o motivo no relatório, sem concluir que o jogo é incompatível.
- `EBOOT READ FAILED`: falha de acesso ao arquivo; reportar HRESULT.

Enviar captura e `phase5-preflight.jsonl`, além de `phase0-lifecycle.jsonl`
e `phase0-results.jsonl` para regressão. O novo relatório é escrito em
LocalState a cada scan completo, com uma linha por jogo e um resumo de scan.
O resumo positivo significa varredura concluída, não todos os headers válidos.
Os logs antigos não são alterados pelo novo relatório.

Confere no máximo 16 KiB de cada `eboot.bin`, sem carregar o arquivo inteiro.
Relata formato, tipo, identidade Orbis, entry, quantidade de program headers
e flags de segmentos SELF. `header_valid=1` **não** prova integridade dos
segmentos, relocations, importações, descriptografia ou prontidão do loader.
`orbis_identity` só indica OSABI/ABI/tipo do header esperado pelo upstream.
`encrypted_segments=1` não faz descriptografia nem implica suporte.
Falha de inspeção não remove o jogo da biblioteca nem impede ícones/navegação.

Todos os registros mantêm `guest_executed=0`. Não há botão de boot nesta
build. O header inspector usa o layout documentado no loader upstream,
mas **não é o loader upstream ligado ao UWP**.

## Teste portátil do parser

```sh
cmake -S tests/guest_preflight -B out/guest-preflight -DCMAKE_BUILD_TYPE=Debug
cmake --build out/guest-preflight -j 2
ctest --test-dir out/guest-preflight --output-on-failure
```

Fixtures de cabeçalho ELF/SELF são autorais, não programas executáveis.
Testam leituras desalinhadas, limites, truncamentos e offsets extremos;
nunca são executadas como código. CI Windows também roda o teste.

## Gates seguintes (estimativa, não garantia de primeiro frame)

| Bloco | Entrega/gate | Teste no console |
| --- | --- | --- |
| 5A | Auditoria e identificação read-only ELF/SELF | Inspecionar eboot real |
| 5B | Loader mínimo controlado, ranges/PT_LOAD/permissões e limites | Carregar fixture guest sem executá-la |
| 5C | ABI SysV, entrada/retorno, stack e diagnóstico de exceções | Executar código guest autoral com resultado conhecido |
| 5D | HLE/threads/TLS/filesystem e entrada de controle guest mínima | Fixture com chamada HLE e leitura de controle |
| 5E | Tentativa de boot Deltarune, imports e bloqueios documentados | Executar até o primeiro bloqueio identificável |
| 5F | PM4, shaders/recursos reais exigidos pela primeira tela | Tentativa de renderização real |
| 5G | Primeiro frame reproduzível e regressões | Captura de imagem produzida pelo jogo |

Dependências podem exigir juntar blocos ou adicionar correções; só concluir
gates com evidência. Antecipar tentativas de Deltarune se a execução ficar
pronta antes; não chamar inspeção/carregamento de boot. HLE mínimo pode
não bastar para esse jogo. Teste autoral isola falhas; Deltarune mede o avanço
real. Não redistribuir binários comerciais/sysmodules nem incluir dumps no Git.

Controlar a UI UWP já funciona; gamepad guest é outro caminho. A Fase 5
inclui seu suporte básico, conforme necessidade para avançar telas do jogo.
As extensões de shaders/resources da Fase 4 continuam gates explícitos.
