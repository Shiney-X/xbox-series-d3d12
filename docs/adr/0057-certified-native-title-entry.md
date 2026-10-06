# ADR 0057 — Entrada nativa do título até o primeiro import

Status: implementação experimental; validação física pendente.

## Problema e decisão

O host passou nos gates de preparação, mas `Linker::Execute`, CPU patches,
MMU e tratamento geral de faults ainda não estão portados. Mais fixtures de
serviços não demonstram que o entry do título pode ser alcançado. O objetivo
é obter evidência de **execução nativa de bytes do eboot real**, sem introduzir
um interpretador, mascarar imports ou liberar execução geral prematuramente.

Adicionar uma tentativa fechada e certificada de entrada até o primeiro
import. O primeiro perfil aceita um prólogo CRT de nove instruções sem loops,
seguido de CALL direto a uma PLT `jmp [rip+disp32]`. A única leitura de dados
do prólogo é argc no EntryParams validado. CALL/PLT precisam corresponder
exatamente a um JUMP_SLOT de função indefinida, com namespace e versões
validados pelo resolver. Outros perfis **não são executados**.

O plano não usa Title ID para permitir execução. Uma atualização do jogo que
alterar o prólogo pode ser recusada. Essa restrição é intencional: reconhecimento
de um pequeno caminho não equivale a um decoder/recompiler geral.

## Integração e posse

Reusar os tipos de startup compartilhados com shadPS4, o staging SELF/ELF e
as relativas com bias da alocação real. Compilar também o **AeroLib original**
no pacote UWP para nomear NIDs; não compilar os stubs upstream que retornam zero.
Isso integra um componente adicional real, não `Linker::Execute` completo.

A bridge Win64→SysV salva oito GPRs não voláteis e XMM6–15. Entra por JMP com
RDI=params, RSI=exit diagnóstico, dois qwords EntryParams no topo e RSP%16=8,
conforme o contrato de `RunMainEntry` upstream. Usa stack da worker Windows;
a stack possuída preparada serve como backing dos parâmetros. Não reivindicar
execução na stack Orbis completa, pthread/TLS binding ou construção do processo.

O único import alcançável é vinculado a um **stop diagnóstico não retornável**:
captura seis argumentos SysV, argc/argv/exit, RSP e o return PC do CALL real.
Restaura a stack e todos os registros salvos da bridge, retornando ao host sem
executar o restante do CRT. Não chama libc, não inventa uma implementação e
não devolve sucesso ao guest. Captura/argumentos são dados; não dereferenciar
ponteiros guest arbitrários para escrever o relatório.

Antes de RX, comparar todas as páginas acessíveis com a imagem relocada do
snapshot imutável e conferir permissões. Só as páginas do entry e da PLT,
e a bridge possuída, tornam-se RX. Entry/PLT que cruzam página são recusados.
Nunca RWX. Um único slot é alterado após validação do resolver e restaurado
ao fim; nenhum código do jogo é reescrito. Todas as demais páginas X ficam RO.

Worker dedicada executa o caminho finito e é joined antes de liberar backing.
Não há loops ou chamadas host nesse caminho; não usar `TerminateThread`.
Depois de join: fechar handle, restaurar slot/permissões e liberar a bridge.
Falha anormal de join encerra o host em vez de devolver backing ainda em uso.
Setup failure faz rollback antes de reportar. B/suspensão não podem liberar
backing durante execução: o trecho nativo e join não possuem coroutine await.

## Limite de segurança e compatibilidade

Este perfil não executa instruções TLS, inicializadores de dependências,
libc, chamadas arbitrárias ou código após o primeiro import. **Não implementa
tratamento geral de exceções ou unwind de guest não-leaf.** O caminho é
certificado antes da execução para não precisar recuperar uma fault guest.
Não adicionar permissões desktop, carregar APIs proibidas via GetProcAddress
ou suprimir falhas com uma tabela de unwind fictícia para todo o eboot.

O gate automático raw/SELF precisa passar antes de a UI permitir entrada de
título. Mesmo aprovado, não certifica execução geral: o gate anterior para
runtime/faults/TLS/MMU continua aberto. Uma falha inesperada nesse código
experimental pode encerrar o processo; não há promessa de recuperação geral.

## Evidência e próximo bloqueio

Inspeção read-only do dump local de Deltarune CUSA15250: o perfil é reconhecido
e a primeira chamada aponta ao NID `bzQExy189ZI`, `_init_env` da biblioteca
`libc`, módulo `libc` versão 1.1. Isso é **inspeção**, não resultado físico de
execução. Não adicionar o jogo ou libc.prx ao repositório. Continuar requer
avaliar a inicialização libc/LLE ou HLE correto, além do boundary geral; não
remover o stop e usar os stubs de sucesso do desktop como prova de boot.

`phase5-boot.jsonl` separa execução do prefixo, import alcançado, retorno ao
host, permissões e cleanup de `game_boot_completed=0` e `game_frame=0`.
`passed=true` significa diagnóstico fechado completo, **não jogo inicializado**.
Título tem game_executed=1 somente quando seus bytes foram executados;
fixture autoral tem game_executed=0. Esse campo deve sempre ser lido com scope.

Referências primárias: `src/core/linker.cpp`, `runtime_layout.h`,
`aerolib/aerolib.cpp`, `module.cpp` e os planos/resolver em `core/uwp`.
