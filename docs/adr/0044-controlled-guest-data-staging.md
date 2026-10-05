<!-- SPDX-FileCopyrightText: 2026 Shiney-X and xbox-series-d3d12 contributors -->
<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# ADR 0044 — Plano PT_LOAD e staging controlado antes da execução

Status: implementação inicial; validação em hardware pendente.

## Contexto

5A reconheceu os cabeçalhos SELF de Sonic Mania e Deltarune no Xbox.
Isso não valida PT_LOAD, payloads, imports ou execução. O loader desktop
depende de address space e runtime ainda não ligados ao AppContainer.

## Decisão

Separar parsing, plano e staging. Usar leituras little-endian com bounds,
prefixo máximo de 16 KiB, até 128 program headers e imagem máxima de 16 MiB.
Rejeitar overflow, filesz maior que memsz, alinhamento inválido, ranges
sobrepostos e W+X. Exigir entry em bytes de arquivo de um segmento X.
São gates conservadores da fixture, não promessa de compatibilidade.

`StageRawGuest` recalcula o plano a partir dos bytes recebidos para evitar
planos forjados ou inconsistentes. Aloca buffer de dados limitado, copia
PT_LOAD e zera BSS/gaps. Uma fixture de dois segmentos, com marcadores em
vez de instruções, usa exatamente essa implementação no teste portátil e
no UWP. O oracle confere todos os bytes e as flags esperadas.

Não copiar SELF como ELF cru: o upstream `Elf::LoadSegment` resolve blocos
SELF por ID e offset de payload. A 5B inspeciona seus metadados, mas recusa
staging SELF mesmo com plano positivo. Não descomprime ou descriptografa.

## Limites e consequências

- Validar flags guest não significa aplicar permissões host. Nada é executado.
- O buffer não reserva os endereços guest; base e entry são metadados.
- Não há relocations, TLS, imports, SysV, loader upstream ou boot de jogo.
- Segmentos com intervalos de bytes separados podem compartilhar páginas;
  conciliação de permissões de página permanece obrigatória antes da execução.
- Plano SELF positivo não comprova acesso aos seus bytes de segmento.
- Memória de jogo acima do orçamento é reportada, não alocada automaticamente.
- Inspeção comercial read-only acompanha teste autoral; nenhum dump é versionado.

5C deverá escolher alocação/proteção compatível com AppContainer, guard pages,
stack e boundary de entrada/retorno antes de executar uma fixture de código.
Não chamar esse staging de prova de JIT ou de boot no Xbox.

## Verificação

CTest portátil e Windows: payloads/BSS, truncamentos, overflow, alinhamento,
W+X, sobreposição, entry, orçamento, SELF rejeitado e corrupção do oracle.
Sanitizers ASan/UBSan no CI Linux, pacote nativo UWP no Windows; validar
`phase5-loader.jsonl` e `phase5-segments.jsonl` em hardware separadamente.
