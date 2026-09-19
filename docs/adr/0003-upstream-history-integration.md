# ADR-0003: integrar o upstream preservando o histórico Git

- Status: aceito
- Data: 2026-09-19

## Contexto

O repositório nasceu com probes específicos para validar UWP, memória e D3D12
no Xbox Series S. A Fase 1 precisa incorporar o código real do shadPS4 sem
transformá-lo em uma cópia opaca e sem dificultar a importação periódica de
correções do projeto original.

## Decisão

O shadPS4 será integrado na raiz por um merge Git com históricos inicialmente
não relacionados. O primeiro baseline é o release estável `v.0.18.0`. O merge
mantém os commits e a autoria upstream alcançáveis na mesma DAG do repositório.

Regras da integração:

1. `upstream` aponta somente para `https://github.com/shadps4-emu/shadPS4.git`.
2. Releases estáveis são usados como pontos de integração reproduzíveis.
3. Atualizações futuras entram por merge, nunca por cópia de arquivos ou
   squash do histórico upstream.
4. O layout, o CMake e os submódulos upstream permanecem canônicos.
5. O suporte Xbox entra como extensões condicionais e permanece isolado em
   `platform/xbox_uwp`, `src/phase0` e documentação própria enquanto o núcleo
   ainda não tiver contratos neutros de plataforma e renderer.
6. Alterações genéricas devem continuar compilando no frontend desktop antes
   de receberem caminhos específicos para UWP.

## Consequências

- `git log` e `git blame` preservam a proveniência do shadPS4.
- O repositório passa a carregar os submódulos e requisitos de build upstream.
- Merges de releases futuras podem gerar conflitos, mas o Git reconhece a
  ancestralidade comum e reduz reaplicações manuais.
- O primeiro merge exige resolver colisões nos arquivos de raiz criados durante
  a Fase 0.
- A branch de integração pode ser comparada diretamente com tags upstream.

## Alternativas rejeitadas

### Submódulo

Preserva o histórico, mas separa as modificações do port em outro repositório
e dificulta commits atômicos que alterem o core e a plataforma Xbox.

### Git subtree em um subdiretório

Evita colisões na raiz, mas muda todos os caminhos do projeto e acrescenta
atrito a builds, patches e comparações com o upstream.

### Snapshot ou cópia vendorizada

Perde autoria e ancestralidade, torna atualizações manuais e não atende ao
requisito de manutenção de longo prazo.

## Procedimento de atualização

```bash
git fetch upstream --tags
git switch -c codex/integrate-shadps4-vX.Y.Z
git merge --no-ff v.X.Y.Z
```

Todo novo baseline deve atualizar `docs/UPSTREAM.md`, passar pelo build desktop
e manter o pacote de probes do Xbox compilável.
