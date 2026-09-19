# ADR-0006: acesso USB à biblioteca sem `FolderPicker`

- **Status:** aceito
- **Data:** 2026-09-19

## Contexto

O host Xbox precisa acessar dumps fornecidos pelo usuário fora do diretório
privado do pacote. O protótipo `0.3.0.0` abriu o `FolderPicker`, mas o seletor
permaneceu carregando sem enumerar qualquer origem. O comportamento foi o mesmo
sem usuário e com um usuário conectado, mesmo com um pendrive reconhecido pelo
Xbox como dispositivo de mídia.

`broadFileSystemAccess` não é suportado no Xbox. Manter o seletor bloqueado ou
tentar caminhos arbitrários não oferece uma base utilizável para o port.

## Decisão

O host acessa `KnownFolders::RemovableDevices` com a capability
`removableStorage`. O manifesto declara inicialmente os tipos necessários para
descoberta e carregamento controlado de dumps PS4. A tela Games detecta o
primeiro dispositivo removível de forma assíncrona e mostra o resultado sem
abrir UI externa.

Este primeiro incremento seleciona a raiz do primeiro dispositivo. Depois da
validação no Series S, o próprio shell implementará navegação por diretórios,
sem expor o filesystem geral do sistema.

## Consequências

- A biblioteca passa a depender de armazenamento USB para conteýo externo.
- O acesso continua limitado pelas capabilities e associações de tipos UWP.
- O aplicativo não persiste caminhos absolutos nem usa capabilities restritas.
- Arquivos sem extensão e tipos ainda não declarados precisarão de uma etapa
  de importação para `LocalState` ou de outra estratégia compatível antes do
  primeiro boot completo.
