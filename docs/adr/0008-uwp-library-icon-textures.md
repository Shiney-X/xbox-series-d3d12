# ADR-0008: texturas de ícone da biblioteca UWP

## Estado

Aceito e validado no Xbox Series S em 27 de setembro de 2026.

## Contexto

A Fase 1D comprovou no console que o host consegue restaurar uma pasta USB,
encontrar um dump próprio e ler `TITLE`, `TITLE_ID` e `APP_VER`. A interface
ainda era somente textual. `sce_sys/icon0.png` vem de armazenamento removível e
deve ser tratado como entrada não confiável sem aumentar desnecessariamente a
residência de GPU.

## Decisão

O host UWP usa `Windows.Graphics.Imaging.BitmapDecoder`, solicita BGRA8
premultiplicado e reduz o maior eixo para no máximo 256 pixels. Antes da
decodificação, limita o arquivo a 8 MiB e cada dimensão de origem a 4096.

O modelo conserva pixels limitados para que a navegação não reabra o USB. O
renderer cria um descriptor heap SRV de uma entrada e mantém somente a textura
do jogo selecionado. A transferência usa upload buffer com row pitch obtido por
`GetCopyableFootprints`, command list dedicada e transição para
`PIXEL_SHADER_RESOURCE`.

Um ícone ausente, inválido ou que falhe no upload produz `NO ICON`; o jogo e
seus metadados continuam disponíveis. O host grava contagens e estado por jogo
em `LocalState/phase1-library-icons.jsonl`, sem persistir pixels nem conteúdo do
jogo.

## Consequências

- O custo de CPU por varredura cresce, mas fica limitado a 32 saídas pequenas.
- A residência incremental de GPU é uma textura mais um upload temporário.
- O decoder e o acesso a `StorageFile` permanecem fora do core do emulador.
- O caminho foi validado com `icon0.png` real: decoder, upload, SRV e
  apresentação retornaram sucesso, inclusive após suspensão e retomada.
