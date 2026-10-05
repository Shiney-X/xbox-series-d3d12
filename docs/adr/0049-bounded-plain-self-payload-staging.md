# ADR 0049 — Staging limitado de payloads ELF/SELF por StorageFile

Status: implementado; validação USB no Xbox pendente.

## Contexto e viabilidade

A PR 48 passou no Series S: duas workers (IDs 3284/644), valores TLS 100/101,
rejeições e cleanup positivos; 25 probes com retomada na sessão
`134357009774483959-876`, sem alertas. Threads/contexto host não implementam
TLS/pthreads Orbis. A próxima dependência da integração é obter os bytes dos
segmentos reais, não apenas os cabeçalhos inspecionados na biblioteca.

O loader desktop `Core::Loader::Elf::LoadSegment` resolve SELF usando
IsBlocked (`0x800`) e GetId (bits 20–31), relacionando o bloco ao **índice
original do program header**, não à posição filtrada na lista PT_LOAD.
Usar diretamente p_offset como offset do SELF é incorreto.

## Decisão

`StageGuestPayload` recebe snapshot imutável limitado a 32 MiB e revalida
internamente cabeçalhos/plano. PT_LOAD recebe program_index explícito.
ELF raw mantém os offsets normais. SELF suporta somente blocos diretos
não criptografados e não comprimidos, um bloco por program header carregado.
Não fornece decriptação, chaves ou extração de conteúdo protegido.

Antes de alocar a imagem, validar:

- Cabeçalhos/plano e orçamento de imagem de 16 MiB, flags/ranges/alinhamento
  e ausência de overlaps PT_LOAD conforme ADR 0044.
- Fim dos metadados SELF (header_size + meta_size) contém a tabela ELF e
  permanece dentro do snapshot; payload não invade os metadados.
- Ranges de todos os blocos dentro do arquivo, sem overlaps/overflow.
- IDs blocked válidos, sem duplicatas; cada PT_LOAD file-backed tem bloco.
- Tamanho direto do bloco igual ao p_filesz; file_size igual ao memory_size
  do descritor SELF. p_memsz ELF pode ser maior: a diferença é BSS.
- PT_LOAD somente BSS não precisa de bloco de arquivo.

Auxiliares não blocked são validados em range, mas não viram PT_LOAD. Flags
signed não certificam assinatura: não há verificação criptográfica nesse gate.
O tamanho físico do StorageFile é o limite efetivo; o campo SELF file_size
não é tomado como prova de integridade nem exige igualdade, pois snapshots
podem conter dados finais adicionais. Perfis diferentes retornam erro explícito.

## Integração UWP e lifetime

A varredura usa o StorageFile já obtido pela pasta USB autorizada. Não usa
stdio/caminho irrestrito para contornar permissões. Limites: 32 MiB por
snapshot, 64 MiB solicitados e oito arquivos por varredura. Filesize é
conferido antes/depois da leitura e short read falha; não é lock contra
alterações de mesmo tamanho no dispositivo. O parser trabalha no snapshot.

Após staging em vector de dados, verificar BSS zero e produzir contagens e
FNV-1a64 diagnóstico. Não é hash criptográfico, comparação Vulkan/D3D12 ou
verificação de autenticidade. Não registrar bytes comerciais no repositório.
Não mapear em endereço guest, aplicar RX ou chamar entry. A imagem é
descartada após o relatório, uma por vez: não vira cache permanente de jogos.
Leitura pode usar buffer temporário além do snapshot e imagem; o limite
por arquivo não deve ser confundido com consumo total instantâneo.

`phase5-payload.jsonl` distingue data_staged/payload_loaded de guest_executed.
Falha/limite não oculta título ou ícone e não trava a biblioteca. Summary
passed exige pelo menos um resultado e todos positivos; biblioteca vazia não
é validação. UI pode dizer SELF DATA OK NOT BOOTED; nenhuma linha nova em
Diagnostics. Relatórios preflight/segments permanecem metadata-only.

## Verificação

Fixture SELF autoral reordena os blocos e usa offsets lógicos ELF que não
apontam aos bytes físicos, com oracle independente de dados/BSS. Casos de
truncamento, budgets, IDs duplicados/inválidos, bloco ausente, overlaps,
metadados, tamanhos, encryption/compression, BSS-only e PH não LOAD intermediário.
Probe de startup UWP também testa a fixture SELF em dados.

Diagnóstico local read-only no eboot Deltarune fornecido pelo usuário:
arquivo 6.709.849 bytes, imagem 12.222.464, cópia 6.030.144, BSS 3.214.776;
dois PT_LOAD e BSS positivo. Nenhum código do jogo foi executado.
Isso não substitui o teste USB no Series S.

## Limites e próximos requisitos

Não integra Core::Loader::Elf ou Core::Linker ao UWP, não carrega sysmodules,
não aplica relocations/imports nem inicializa Orbis. Não implementa filesystem
HLE guest (open/read/stat), TLS Orbis, scePad ou unwind guest geral.
É uma dependência concreta de IO/loader da 5D, não conclusão da 5D nem boot 5E.

Referências locais: `src/core/loader/elf.h` e `elf.cpp` (upstream v0.18.0),
ADRs 0043/0044/0048. O teste opcional `xbox_guest_loader_test <eboot>` apenas
imprime diagnóstico; não salva/dumpa/redistribui ou executa o arquivo.
