# ADR 0042 — Fechamento inicial de shaders e avaliação de HLSL direto

- Status: aceito; fundação 4A–4D validada, consolidação 4E implementada
- Data: 2026-10-03

## Viabilidade e dependências

O Xbox executou SPIR-V → HLSL → DXIL com compute e VS/PS gerados pela IR
e emissor real. Os contratos atuais são pequenos e autorais. A dependência
SPIRV-Cross está fixada; DXC usa o SDK. Não há dados de performance de jogos
ou shaders GCN traduzidos no console para justificar substituir o caminho.

## Decisão

Fechar a fundação inicial 4A–4E, não o suporte geral a shaders. Dividir os
itens amplos do roadmap em partes verificadas e extensões guest pendentes.
Manter corpus golden Vulkan/D3D12 como pendência real, não renomear nossos
oracles sintéticos como golden cross-backend. Nenhum runtime/PM4/boot de
jogo é certificado por esses probes.

Avaliar e **adiar** um emissor HLSL direto:

- Poderia eliminar emissão/reparsing de SPIR-V e dar controle mais direto
  sobre bindings HLSL. Não há medição que quantifique esse ganho aqui.
- Exigiria outro backend de instruções IR, tipos, controle de fluxo,
  builtins, memória e recursos, mais manutenção com mudanças upstream.
- Reutilizar EmitSPIRV e SPIRV-Cross preserva mais código existente e já
  possui uma linha de teste real no console; limites atuais de reflection
  não serão resolvidos automaticamente por trocar o emissor.
- Reavaliar com corpus guest, timings de emissão/Cross/DXC separados e
  um bloqueio semântico concreto. Não prometer que o caminho atual é mais
  rápido; a decisão inicial prioriza integração verificável/manutenção.

## Implementação e evidência

Adicionar validador Python stdlib para JSONL real e testes negativos em CI:
campos de ABI/bindings/oracles, status estritos, duplicatas, dados truncados
e ordenação de lifecycle. Não cria mock GPU, não executa jogo e não escreve
os logs. Declara explicitamente guest/golden não certificados. Preservar
arquivos pessoais fora do Git; documentar apenas sessões e resultados mínimos.

4D: resultados `134354665468983635-5052`, 20 positivos, 32 pixels corretos,
bindings VS/PS separados e zero falhas de alocação. Resume registrado em
`134354665134013638-6320`, sem apresentação posterior no journal; outro
launch corresponde à sessão de resultados. Não ampliar a prova de lifecycle.
O pacote da PR 41 permanece válido: não há alteração no executável da 4E.

## Próximo gate

Antes de boot guest no UWP, auditar e integrar loader/CPU/HLE e dependências
de plataforma, memória, ABI/TLS/exceções e syscalls. Executar primeiro um
teste guest mínimo controlado, não prometer boot de jogo pelo PASS dos
shaders. Recursos/shaders GCN e PM4 reais serão gates próprios do caminho
ao primeiro frame, mantendo as extensões de shaders rastreadas no roadmap.
