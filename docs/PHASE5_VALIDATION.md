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

## 5B: plano PT_LOAD e staging de dados (não é boot)

Instalar o novo MSIX como Game, depois dos três checks do workflow
`Xbox UWP shell` verdes: `portable-quality`, `build-and-test` e
`build-uwp-package`. Não é necessário recompilar manualmente no console.

1. Abrir o app: a fixture ELF autoral é carregada automaticamente em um buffer
   de dados de 32 KiB e liberada após a verificação. Não é código do PS4.
2. Em Games, usar X RESCAN com Sonic Mania e Deltarune na pasta.
3. Voltar ao Dev Home, reabrir e conferir navegação/ícones/Diagnostics.
4. Enviar `phase5-loader.jsonl`, `phase5-segments.jsonl`,
   `phase5-preflight.jsonl`, `phase0-results.jsonl` e `phase0-lifecycle.jsonl`.

No `phase5-loader.jsonl`, esperar `guest-loader-fixture` com `passed=true`,
`staged_bytes=32768`, `copy_bss_verified=1`, `host_permissions_applied=0`
e `guest_executed=0`. A verificação compara todos os bytes do buffer,
incluindo payloads, BSS e intervalos zerados. Uma falha aparece também nos
system probes. Não foi adicionada outra linha à tela Diagnostics.

`phase5-segments.jsonl` inspeciona a tabela de program headers dentro do mesmo
prefixo de 16 KiB. Relata ranges e flags PT_LOAD, entry file-backed executável,
overflow, alinhamento, sobreposição e orçamento de imagem de 16 MiB.
Esses limites são política restrita do protótipo, não limites do hardware
nem uma medida da memória necessária para rodar esses jogos.

Um `plan_valid=1` em SELF **não valida os blocos do container**: `p_offset`
é lógico ao ELF, não um offset diretamente copiável de `eboot.bin`.
`self_adapter_ready=0` e `payload_loaded=0` continuam explícitos.
Uma rejeição do plano não remove o título e não prova incompatibilidade.
O resumo `guest-segment-scan` só confirma que o scan terminou.

`StageRawGuest` só aceita payload ELF cru, recalcula o plano antes de alocar,
copia bytes e zera BSS. Valida flags guest e rejeita W+X, mas **não aplica
permissões de página, endereço virtual fixo ou relocations**. O buffer host
não é um address space guest e seu endereço não é o entry. Aplicação real
de permissões, guard pages, stack e boundary de execução permanece gate da 5C.
Não há carregamento/boot dos jogos, áudio nem entrada guest nesta build.

O teste portátil agora inclui `phase5.guest-loader`. Exercita truncamentos,
ranges extremos, W+X, alinhamento, entry inválido, BSS, limite de imagem,
rejeição de staging SELF e corrupção detectada pelo oracle de bytes.

## CI proporcional ao escopo

Mudanças exclusivamente nos caminhos Xbox já filtrados, incluindo
`tests/guest_preflight/**`, não disparam builds SDL desktop. Alterações em
código compartilhado continuam disparando-os. Branches de trabalho disparam
o workflow desktop via PR, não também via push; `main` continua coberta por
push e execução manual permanece disponível. O workflow Xbox mantém testes
Windows, pacote UWP e qualidade/licenças/testes portáteis com sanitizers.
Alterações no próprio workflow desktop ainda disparam os builds nesta PR.
Não altera regras de proteção de branch ou transforma checks antigos em passes.

## 5C: execução da fixture ELF autoral, não boot de jogo

Esperar os checks Windows/UWP/qualidade verdes, instalar o novo MSIX como
Game e abrir. O probe roda automaticamente em uma thread de teste dedicada.
Não selecionar jogo para executar: Games continua só inspecionando SELF.

Enviar `phase5-execution.jsonl`, `phase0-results.jsonl` e
`phase0-lifecycle.jsonl`. Conferir UI e Diagnostics, sair para Dev Home e
reabrir, depois fazer X RESCAN para regressão da biblioteca.

Esperado no registro `guest-execution-fixture`:

- `passed=true`, `win32_error=0`, `guest_executed=1` (somente a fixture).
- `abi=sysv_integer_leaf`, `arguments=19,23`, `return_value=42`.
- `alignment_red_zone_verified=1`, `host_permissions_verified=1`.
- `stack=windows_worker_thread`, `stack_verified=1`.
- `fault_recovered=1`, `fault_code=3221225477` (access violation esperada).
- `fault_filter_verified=1`: sete casos fora do escopo não são absorvidos.
- `fault_boundary=win64_leaf_seh`, `sysv_fault_unwind_supported=0`.
- `return_after_fault=42`, `allocations_released=1`.
- `game_executed=0`, `game_frame=0`, `loader_linked=0`.

A fixture é ELF cru autoral com instruções x86-64 de um leaf SysV. Usa o
staging 5B, depois copia a imagem para alocação virtual própria, sem fixar
seus endereços guest. Páginas de código e bridge passam RW→RX; dados RW;
gaps NOACCESS e extremos reservados/não comprometidos. Nunca usa RWX.
O thunk preserva RSI/RDI e XMM6..15, adapta dois argumentos inteiros e
verifica alinhamento de entrada e uso síncrono de 128 bytes de red zone.

A stack é a de uma thread Windows com reserva solicitada de 256 KiB e
limites verificados, não uma stack de processo Orbis montada pelo linker.
Não há troca manual de RSP da UI nem alteração de TEB. A proteção/expansão
da stack é gerenciada pelo Windows; o probe não simula stack overflow.

A segunda entrada autoral é uma **leaf Win64 separada**, chamada diretamente
por helper nativo com `__try/__except`. Lê um endereço reservado pertencente
ao probe. O filtro SEH trata exclusivamente esse RIP/read/endereço enquanto
armado no escopo da chamada. A leaf não altera RSP/não voláteis; não precisa
de tabela de unwind própria. Não passa pela bridge SysV. O SDK AppContainer
rejeitou Add/RemoveVectoredExceptionHandler no primeiro build; o caminho VEH
foi removido, sem redeclarar/importar APIs desktop à força.

Outras exceções continuam sua busca normal. **Recuperação de faults através
da bridge SysV não está implementada**, e não há handler geral de jogos.
O segundo retorno 42 comprova chamada SysV após recuperação da leaf Win64,
não recuperação de fault ocorrido no corpo SysV.

Não aceita paths, entrys ou instruções externas. A execução tem código fixo
sem loops e espera a thread terminar; não é um executor com timeout/cancelamento
para conteúdo não confiável. Mitigações proibitivas ou CFG ativo fazem o gate
falhar, sem desligar políticas. API não suportada também deve falhar o gate.

Ainda faltam Orbis main/startup, relocations/imports, adapter SELF, TLS/HLE,
chamadas guest→host e tratamento geral de faults, além de comandos gráficos
de jogo. Esse sucesso não certifica o ABI completo do PS4 ou um JIT do shadPS4.
O teste Linux do mesmo thunk valida bytes/aritmética no host, não substitui
o teste nativo Windows e AppContainer no Xbox.

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
