# Baseline do shadPS4

## Revisão inicial da Fase 1

| Campo | Valor |
|---|---|
| Repositório | `https://github.com/shadps4-emu/shadPS4.git` |
| Release | `v.0.18.0` (`UltraPersona`) |
| Publicação | 2026-08-18 |
| Objeto da tag anotada | `ea730712f38ff86d18e4925c36470956c4d1f5f4` |
| Commit apontado pela tag | `e3ce810f3a653f43ac64ebab63023de281a4103a` |
| Licença do core | GPL-2.0-or-later |

O baseline usa um release estável em vez da ponta de desenvolvimento para que
o primeiro build e o desacoplamento de plataforma tenham um alvo reproduzível.
A revisão de `upstream/main` observada ao iniciar a integração foi
`42c555b7ab5d0678f531a7e4d505560ccc0f8add`, mas ela não faz parte do baseline.

## Verificação

```bash
git fetch upstream --tags
git rev-parse v.0.18.0
git rev-parse v.0.18.0^{commit}
```

Os valores devem corresponder, respectivamente, ao objeto da tag e ao commit
registrados na tabela acima.

## Integração e validação

O merge inicial foi registrado em `642fdddfc98d24f40023dcfdcbb29d85f618d218`.
Ele possui como pais o histórico do port (`e27df123`) e o commit do release
upstream (`e3ce810f`), permitindo auditar futuras atualizações com operações
Git normais.

A execução de CI do [PR #2](https://github.com/Shiney-X/xbox-series-d3d12/pull/2)
validou no Windows:

- compilação e execução dos testes C++ do shadPS4;
- compilação completa do frontend SDL e geração do artefato desktop;
- compilação e execução dos probes da Fase 0;
- geração do pacote UWP x64 assinado para Xbox Dev Mode;
- conformidade clang-format 19 e REUSE 3.3.

Evidências: [Build and Release](https://github.com/Shiney-X/xbox-series-d3d12/actions/runs/35467148590)
e [Windows probes](https://github.com/Shiney-X/xbox-series-d3d12/actions/runs/35467148585).
O job desktop macOS da mesma execução falhou e não integra o critério de
saída Windows/Xbox desta fase; os testes C++ macOS passaram.
