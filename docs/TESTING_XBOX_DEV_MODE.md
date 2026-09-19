# Teste no Xbox Series S Dev Mode

Este roteiro valida o sandbox real do Xbox. Um resultado obtido no Windows
desktop não substitui esta execução.

## O que o pacote mede

O aplicativo executa quatro probes e uma apresentação:

1. arquitetura x64 e políticas de mitigação do processo;
2. reserva, commit, proteção e realocação no mesmo endereço virtual;
3. transição de página RW para RX e execução de seis bytes de código x86-64;
4. criação de um dispositivo D3D12 de hardware e consulta de capabilities;
5. criação de swapchain para `CoreWindow`, clear e `Present`.

A tela fica verde quando os quatro probes passam. Vermelho indica que pelo
menos um falhou. O resultado detalhado sempre deve ser coletado.

## Pré-requisitos

- Xbox Series S ativado e iniciado em Dev Mode;
- um usuário conectado no console;
- acesso remoto/Device Portal habilitado no Dev Home;
- PC e Xbox na mesma rede;
- navegador capaz de abrir o endereço HTTPS mostrado pelo Dev Home.

## Obter o pacote

1. Abra a execução mais recente do workflow **Windows probes** no GitHub.
2. Baixe o artefato `xbox-phase0-uwp-sideload`.
3. Extraia o ZIP. Entre na pasta
   `AppPackages/xbox_phase0_uwp_0.1.0.0_x64_Test` e localize o `.msix`, o
   certificado `.cer` e o pacote em `Dependencies/x64`.

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

## Executar e interpretar

1. Inicie **xbox-series-d3d12 probes** pelo Dev Home, sem debugger conectado.
2. Aguarde a tela estabilizar:
   - verde: CPU/memória/D3D12 passaram;
   - vermelho: ao menos um probe falhou;
   - retorno imediato ao Dev Home: falha de ativação ou crash antes do render.
3. Anote a versão do sistema operacional exibida no Dev Home e se o app foi
   classificado como App ou Game.

## Coletar o relatório

1. No Device Portal, abra **File explorer**.
2. Selecione o armazenamento local do pacote
   `ShineyX.xbox-series-d3d12_*`.
3. Entre em `LocalState` e baixe `phase0-results.jsonl`.
4. Não edite o arquivo. Anexe-o a uma issue junto com:
   - modelo do console;
   - versão do sistema operacional;
   - data/hora do teste;
   - classificação App/Game;
   - cor exibida ou descrição do crash.

Cada linha é um objeto JSON independente. Um exemplo de sucesso do probe de
execução é:

```json
{"probe":"executable-memory","passed":true,"win32_error":0,"details":"return_value=42;exception_caught=0"}
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

Não iniciar a integração do upstream shadPS4 antes de obter o arquivo bruto do
Series S. Falha em `executable-memory` ou `virtual-memory` exige uma decisão de
arquitetura; falha apenas em `d3d12-device`/`uwp-presentation` direciona o
trabalho ao host gráfico.
