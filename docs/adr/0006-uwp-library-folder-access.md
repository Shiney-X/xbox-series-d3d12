# ADR-0006: acesso persistente à pasta da biblioteca pelo UWP

- **Status:** aceito
- **Data:** 2026-09-19

## Contexto

O host Xbox precisa acessar dumps fornecidos pelo usuário fora do diretório
privado do pacote. Caminhos arbitrários e capacidades amplas de filesystem não
são uma base portável ou confiável para o sandbox UWP do Xbox Dev Mode.

## Decisão

A tela Games abre `Windows.Storage.Pickers.FolderPicker` por ação explícita do
usuário. A pasta retornada é registrada em
`StorageApplicationPermissions::FutureAccessList` com um token estável. Nas
inicializações seguintes, o host solicita a pasta por esse token em vez de
persistir ou reabrir diretamente um caminho absoluto.

Seleção e restauração são assíncronas e mantêm o objeto `IFrameworkView`
vivo durante a operação. Cancelamento é um estado válido; exceções e tokens
inválidos aparecem como falha controlada na interface e em
`phase1-library.jsonl`.

## Consequências

- O sistema continua responsável pelo consentimento e pelo escopo do acesso.
- O token pode depender do usuário ativo no Xbox; isso deve ser medido no
  hardware antes da enumeração dos jogos.
- Este incremento não interpreta `param.sfo` nem carrega executáveis.
- O próximo incremento percorrerá a pasta concedida procurando
  `sce_sys/param.sfo`, sem ampliar as permissões.
