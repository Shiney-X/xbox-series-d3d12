# ADR-0001: UWP como baseline público

- Status: aceito
- Data: 2026-09-19

## Contexto

Xbox Dev Mode em console retail permite desenvolvimento UWP. Acesso a recursos
GDK específicos do console exige onboarding, acordos e materiais que podem estar
sob NDA.

## Decisão

O alvo público deste repositório é UWP x64 em Xbox Series S Dev Mode. GDK será
tratado como plataforma distinta e opcional.

## Consequências

- Todas as dependências precisam ser compatíveis com AppContainer.
- O pacote declarará somente capabilities necessárias.
- Nenhum SDK, binário, header ou documento protegido por NDA será versionado.
- Limitações de memória e lifecycle UWP fazem parte da arquitetura.
