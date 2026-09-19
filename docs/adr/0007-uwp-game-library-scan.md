# ADR-0007: descoberta de jogos e metadados PSF no host UWP

- **Status:** aceito
- **Data:** 2026-09-19

## Contexto

O navegador USB da Fase 1C permite escolher uma pasta real no Xbox, mas uma
lista de jogos exige distinguir diretórios comuns de dumps PS4 e interpretar
metadados sem depender do frontend desktop. O sandbox UWP também impede tratar
um caminho absoluto como permissão persistente.

## Decisão

O host persiste somente os componentes relativos da pasta selecionada dentro
do primeiro dispositivo retornado por `KnownFolders.RemovableDevices`. Na
ativação seguinte, cada componente é resolvido novamente por `StorageFolder`.
Uma falha de resolução invalida a seleção e retorna com segurança à raiz.

O scanner é assíncrono, somente leitura e limitado a 128 diretórios, 32 jogos
e três níveis. Um diretório candidato deve conter `eboot.bin` e
`sce_sys/param.sfo`. A bridge UWP interpreta `TITLE`, `TITLE_ID` e `APP_VER`
usando os layouts `PSFHeader` e `PSFRawEntry` do core upstream, com validação
de todos os offsets e comprimentos antes de ler o buffer.

O resultado é gravado em `LocalState/phase1-library-scan.jsonl`. Nenhum arquivo
do jogo é copiado, alterado ou enviado para o repositório.

## Consequências

- A descoberta funciona sem `std::filesystem` sobre caminhos não autorizados.
- Um USB removido ou reorganizado não deixa uma permissão inválida permanente.
- A profundidade e os limites evitam travar a thread de interface com uma
  árvore arbitrariamente grande.
- O primeiro incremento mostra texto. `icon0.png` será decodificado e enviado
  ao D3D12 somente depois da validação do scanner no Series S.
