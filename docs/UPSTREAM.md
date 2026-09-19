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
