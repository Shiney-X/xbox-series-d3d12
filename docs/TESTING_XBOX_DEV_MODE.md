# Teste da interface no Xbox Series S Dev Mode

Este roteiro valida o sandbox real do Xbox. Um resultado obtido no Windows
desktop não substitui esta execução.

## O que o pacote mede

O aplicativo executa os probes da Fase 0, valida uma bridge compilada contra o
core upstream e apresenta a interface inicial:

1. arquitetura x64 e políticas de mitigação do processo;
2. reserva, commit, proteção e realocação no mesmo endereço virtual;
3. reserva e divisão de placeholders, substituição por duas views fixas da
   mesma seção e coerência bidirecional entre os aliases;
4. transição de página RW para RX e execução de seis bytes de código x86-64;
5. criação de um dispositivo D3D12 de hardware e consulta de capabilities;
6. leitura do limite real do sandbox e alocação controlada de no máximo 5%
   desse limite, limitada a 256 MiB e a um quarto da memória ainda disponível;
7. criação de swapchain para `CoreWindow`, root signature, PSO, compilação
   HLSL Shader Model 6 para DXIL por DXC e desenho do shell por D3D12;
8. validação de tipos endian e do ABI de `PSFHeader`/`PSFRawEntry` vindos do
   core shadPS4 `v0.18.0` no mesmo executável UWP;
9. navegação entre Jogos, Configurações e Diagnósticos pelo controle;
10. gravação síncrona dos relatórios e de um journal do ciclo de vida no
   armazenamento local do pacote.

O indicador **CORE LINKED** confirma que a bridge foi inicializada. O rodapé
mostra **SYSTEM PROBES PASS** somente quando os probes, a apresentação e a
gravação passam. O resultado detalhado sempre deve ser coletado.

## Pré-requisitos

- Xbox Series S ativado e iniciado em Dev Mode;
- para coleta reproduzível pelo Device Portal, nenhum usuário conectado no
  console (consulte a observação abaixo);
- acesso remoto/Device Portal habilitado no Dev Home;
- PC e Xbox na mesma rede;
- navegador capaz de abrir o endereço HTTPS mostrado pelo Dev Home.

## Obter o pacote

1. Abra a execução mais recente do workflow **Xbox UWP shell** no GitHub.
2. Baixe o artefato `xbox-shell-uwp-sideload`.
3. Extraia o ZIP. Entre na pasta
   `AppPackages` e localize o `.msix`, o certificado `.cer` e o pacote em
   `Dependencies/x64`.

O certificado é efêmero e serve somente para sideload do build correspondente.
Não instale nem distribua um arquivo `.pfx`; o workflow não publica a chave
privada.

## Instalar pelo Device Portal

1. No PC, abra o endereço do Device Portal exibido no Dev Home e autentique.
2. Abra **Home** ou **Apps manager** e escolha a opção para adicionar/implantar
   um aplicativo.
3. Selecione o arquivo `.msix` como pacote principal.
4. Se o portal solicitar um certificado, selecione
   `xbox-series-d3d12-dev.cer`.
5. Adicione `Dependencies/x64/Microsoft.VCLibs.x64.14.00.appx` como
   dependência.
6. Instale o pacote e aguarde a confirmação.
7. No Dev Home, configure o aplicativo como **Game** quando essa opção estiver
   disponível; isso evita medir o perfil de recursos de um aplicativo comum.

Como o certificado de teste muda a cada build, desinstale a versão anterior se
o portal rejeitar a atualização por conflito de assinatura. Configure o novo
pacote como **Game antes da primeira abertura**.

## Executar e interpretar

1. Inicie **shadPS4 Xbox Shell** pelo Dev Home, sem debugger conectado.
2. Aguarde a tela estabilizar:
   - menu com três cartões e `CORE LINKED`: shell e bridge foram iniciados;
   - `SYSTEM PROBES PASS`: CPU/memória/D3D12, apresentação e persistência
     passaram;
   - `CORE FAILED` ou `SYSTEM PROBES FAIL`: colete os relatórios;
   - retorno imediato ao Dev Home: falha de ativação ou crash antes do render.
3. Use esquerda/direita no direcional para mover o foco entre os cartões.
4. Pressione **A** em cada cartão e confirme a abertura da página.
5. Pressione **B** e confirme o retorno ao início sem sair para o Dev Home.
6. Na tela inicial, pressione **B** e confirme o retorno normal ao Dev Home.
7. Anote a versão do sistema operacional exibida no Dev Home e se o app foi
   classificado como App ou Game.

## Testar suspensão e retomada

Faça esta sequência sem usuário conectado, para que os arquivos permaneçam
visíveis no Device Portal:

1. Inicie o aplicativo e confirme a interface.
2. Volte ao Dev Home, deixando o aplicativo em segundo plano.
3. Aguarde dez segundos.
4. Abra novamente **shadPS4 Xbox Shell**.
5. Confirme que a interface reaparece, aceita navegação e mantém os indicadores
   de sucesso.
6. Repita o ciclo uma segunda vez para verificar que o comportamento é estável.

O dispositivo D3D12 `SraKmd_arden` medido não expôs `IDXGIDevice3`. O probe
registra `dxgi_trim_supported=0` sem reprovar a suspensão; a retomada e a nova
apresentação continuam obrigatórias.

Se o Xbox encerrar o processo em vez de retomá-lo, o journal mostrará uma nova
sessão `launch` depois de `suspend`, sem o evento `resume` correspondente. Esse
resultado deve ser preservado; não é equivalente a uma retomada aprovada.

## Testar o navegador USB da biblioteca

O pacote `0.3.1.0` acessa o dispositivo removível diretamente e renderiza o
navegador de pastas dentro do shell D3D12. Ele ainda não procura nem executa
jogos.

1. Formate um pendrive como armazenamento de mídia reconhecido pelo Xbox.
2. Conecte o pendrive antes de iniciar o aplicativo.
3. Crie antes uma estrutura simples, por exemplo `F:\Games\SonicMania`.
4. Abra **Games** e confirme que nenhum seletor externo é aberto.
5. Confirme `USB FOLDER BROWSER`, o breadcrumb e a lista de subpastas.
6. Use o direcional para selecionar `Games` e pressione **A**.
7. Confirme que o breadcrumb e a lista mudam para o novo diretório.
8. Pressione **B** e confirme o retorno para a raiz do dispositivo.
9. Entre novamente em `Games` e pressione **X** para selecionar a pasta.
10. Confirme `LIBRARY FOLDER SELECTED` na tela.
11. Pressione **B** dentro de uma subpasta para subir; na raiz, **B** retorna
    para a tela inicial sem fechar o aplicativo.

## Coletar o relatório

1. No Device Portal, abra **File explorer**.
2. Selecione o armazenamento local do pacote
   `ShineyX.xbox-series-d3d12_*`.
3. Entre em `LocalState` e baixe `phase0-results.jsonl`.
4. Baixe também `phase0-lifecycle.jsonl`.
5. Baixe `phase1-core.jsonl`.
6. Baixe `phase1-library.jsonl`.
7. Se `LocalState` estiver vazio, procure o relatório da Fase 0 em `LocalCache`.
   Essa é a rota de contingência usada quando o perfil Game recusa a primeira
   gravação.
8. Não edite os arquivos. Anexe-os a uma issue junto com:
   - modelo do console;
   - versão do sistema operacional;
   - data/hora do teste;
   - classificação App/Game;
   - cor exibida ou descrição do crash.

### Armazenamento por usuário

No Xbox Series S testado, o Device Portal não enumerou o `LocalState` do
processo quando havia um usuário conectado, embora o probe de escrita tivesse
sucesso. Ao desconectar o usuário, executar novamente e atualizar o File
Explorer, o arquivo apareceu normalmente. Essa condição afeta a coleta pelo
portal, não a execução dos probes.

Cada linha é um objeto JSON independente. Um exemplo de sucesso do probe de
execução é:

```json
{"probe":"executable-memory","passed":true,"win32_error":0,"details":"return_value=42;exception_caught=0"}
```

O novo probe de aliases deve produzir uma linha equivalente a:

```json
{"probe":"memory-aliases","passed":true,"win32_error":0,"details":"view_size=65536;reservation_size=131072;fixed_views=1;forward_alias=1;reverse_alias=1"}
```

A bridge do core deve produzir em `phase1-core.jsonl`:

```json
{"component":"shadps4-core-uwp","passed":true,"details":"upstream=v0.18.0;initialized=1;psf_abi=1;endian=1"}
```

Depois de pressionar **X**, `phase1-library.jsonl` deve conter `passed:true`,
estado `folder_selected`, `source=KnownFolders.RemovableDevices`, a profundidade
e o breadcrumb. O relatório guarda os nomes para diagnóstico, mas não persiste
um caminho absoluto.

O probe de pressão registra o limite atual, o limite esperado pelo Xbox, uso
antes/depois, pico observado e a quantidade efetivamente alocada. O journal usa
um identificador por processo:

```json
{"session":"<ticks>-<pid>","event":"launch","details":"process started"}
{"session":"<ticks>-<pid>","event":"suspend","details":"suspending event observed;report flushed"}
{"session":"<ticks>-<pid>","event":"resume","details":"resuming event observed"}
```

## Build local opcional

Em um PC Windows com Visual Studio 2022, workload C++ para UWP e Windows SDK
10.0.26100.0, execute:

```powershell
.\scripts\windows\Build-UwpPackage.ps1
```

O script cria um certificado de sideload temporário no repositório de
certificados do usuário, compila o pacote, exporta apenas o `.cer` público e
remove o certificado com chave privada após o build. A saída fica em
`out\package\xbox-uwp`.

O pacote mantém `10.0.19041.0` como versão mínima do alvo; o SDK 26100 é usado
apenas no build para obter os headers C++/WinRT compatíveis com C++20.

## Critério para avançar

Avançar para a enumeração de jogos somente depois de confirmar no Series S:
listagem, rolagem, entrada e retorno entre diretórios, seleção com **X** e
`state:"folder_selected"` em `phase1-library.jsonl`.
