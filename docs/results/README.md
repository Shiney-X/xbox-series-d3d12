# Resultados no hardware

## Xbox Series S — Game

- sistema operacional: `10.0.26100.9426`;
- classificação do pacote: **Game**;
- debugger: desconectado;
- usuário do console: desconectado durante execução e coleta;
- pacote: `0.1.0.1`;
- resultado: seis de seis probes aprovados;
- SHA-256 do JSONL: `4edc3ff0ac7f412b8df8c0c790e21d3bb28abfd2f780cc8566f59e1ac3782654`.

Arquivo bruto: [`xbox-series-s-game-10.0.26100.9426.jsonl`](xbox-series-s-game-10.0.26100.9426.jsonl).

## Xbox Series S — triângulo D3D12

- sistema operacional: `10.0.26100.9426`;
- classificação do pacote: **Game**;
- pacote: `0.1.0.2`;
- shader de probe: HLSL Shader Model 5 compilado em runtime por `D3DCompile`;
- chamada de desenho: `DrawInstanced` com três vértices;
- resultado: seis de seis probes aprovados;
- validação visual: triângulo colorido confirmado por captura do console;
- SHA-256 do JSONL: `07da540e6ef75480ba3361bc101ac423a4cf927b6d35b60db225f9b15519348d`.

Arquivo bruto:
[`xbox-series-s-game-triangle-10.0.26100.9426.jsonl`](xbox-series-s-game-triangle-10.0.26100.9426.jsonl).

## Xbox Series S — incompatibilidade `IDxcCompiler3`

O pacote `0.1.0.4` carregou o runtime DXC, mas retornou `E_NOINTERFACE`
(`0x80004002`) antes do primeiro `Present` ao solicitar `IDxcCompiler3`. Os
demais probes e a persistência continuaram aprovados. O host passou a usar
`IDxcLibrary` + `IDxcCompiler`, mantendo a saída Shader Model 6/DXIL.

- SHA-256 do JSONL: `5413d1ba439c22cd63b3abd0a9c791bf40597472675cb16c5e912f8b7a84b1b0`;
- arquivo bruto:
  [`xbox-series-s-game-dxc-interface-failure-10.0.26100.9426.jsonl`](xbox-series-s-game-dxc-interface-failure-10.0.26100.9426.jsonl).

## Xbox Series S — DXC/DXIL

O pacote `0.1.0.5` compilou em runtime os vertex e pixel shaders como Shader
Model 6/DXIL usando `IDxcCompiler`, criou o graphics PSO, desenhou o triângulo e
apresentou no perfil **Game**. A imagem foi confirmada visualmente no console.

- resultado: seis de seis probes aprovados;
- SHA-256 do JSONL: `2c5113f741f0a86ae37e174619b8628ec061884513170e01451d7250d86fe55f`;
- arquivo bruto:
  [`xbox-series-s-game-dxil-10.0.26100.9426.jsonl`](xbox-series-s-game-dxil-10.0.26100.9426.jsonl).

O valor de 128 MiB retornado como memória de vídeo dedicada é a visão do
DXGI dentro do sandbox UWP. Ele não representa a memória física total do
console nem deve ser usado como budget de residency.

Durante a coleta, o Device Portal não exibiu o `LocalState` do pacote quando
havia um usuário conectado. Sem usuário conectado, o mesmo pacote executado
como Game expôs normalmente `LocalState/phase0-results.jsonl`. Isso é tratado
como uma particularidade de enumeração do armazenamento por usuário no Device
Portal, não como falha do probe.

## Xbox Series S — placeholders e aliases de memória

O pacote `0.1.0.6`, executado em perfil **Game**, reservou e dividiu uma região
de placeholders em duas views fixas da mesma seção de 64 KiB. Escritas feitas
por qualquer uma das views ficaram imediatamente visíveis na outra. O
triângulo DXIL também foi confirmado visualmente, sem regressão na apresentação.

- sistema operacional: `10.0.26100.9426`;
- resultado: sete de sete etapas aprovadas;
- `fixed_views=1`, `forward_alias=1`, `reverse_alias=1`;
- SHA-256 do JSONL: `8b7211fa894a4580222b095c3e6f759d461bbd5e41bd5719c1072e3cd40c4dec`;
- arquivo bruto:
  [`xbox-series-s-game-aliases-10.0.26100.9426.jsonl`](xbox-series-s-game-aliases-10.0.26100.9426.jsonl).

## Xbox Series S — pressão de memória e ciclo de vida

O pacote `0.1.0.7`, executado em perfil **Game**, mediu um limite atual e
esperado de 5 GiB. O probe comprometeu e tocou 256 MiB, observou o crescimento
no contador do sistema e retornou ao uso inicial depois de liberar as regiões.

O journal confirmou `launch → suspend → resume` na mesma sessão e no mesmo
processo. A apresentação do triângulo depois da retomada também passou. A tela
ficou vermelha porque a consulta de `IDXGIDevice3` no dispositivo D3D12
`SraKmd_arden` retornou `E_NOINTERFACE` (`0x80004002`); esse resultado passa a
ser registrado como capability ausente, e não como falha do ciclo de vida.

- limite do sandbox: `5368709120` bytes (5 GiB);
- carga controlada: `268435456` bytes (256 MiB);
- uso antes/depois: `8110080` bytes;
- pico observado: `277069824` bytes;
- SHA-256 do relatório: `ec822ed27596c8e27ea8f304612a735b7aec19bf1ef096048ec017786b203c77`;
- SHA-256 do journal: `a3dcadede5eaf1abad020f0723d762258234694e86d6f1a8afb06c7901bd85ae`;
- arquivos brutos:
  [`xbox-series-s-game-memory-lifecycle-10.0.26100.9426.jsonl`](xbox-series-s-game-memory-lifecycle-10.0.26100.9426.jsonl) e
  [`xbox-series-s-game-lifecycle-journal-10.0.26100.9426.jsonl`](xbox-series-s-game-lifecycle-journal-10.0.26100.9426.jsonl).

## Xbox Series S — resultado final da Fase 0

O pacote `0.1.0.8` tratou a ausência de `IDXGIDevice3::Trim` como capability
opcional. Todos os onze resultados passaram, o triângulo permaneceu visível
depois da retomada e o journal confirmou `launch → suspend → resume` na mesma
sessão e no mesmo processo.

- resultado: onze de onze etapas aprovadas;
- validação visual antes e depois da retomada: aprovada;
- SHA-256 do relatório: `b8aaf5ee355d683337888a675f306c58ff46a2ab596a5ba6c915dc4559eb8045`;
- SHA-256 do journal: `ad6e0cf433f1ab297b6b7fc883c7409af9913eab4bfba3a29d1ab34dd68669cd`;
- arquivos brutos:
  [`xbox-series-s-game-phase0-final-10.0.26100.9426.jsonl`](xbox-series-s-game-phase0-final-10.0.26100.9426.jsonl) e
  [`xbox-series-s-game-phase0-final-lifecycle-10.0.26100.9426.jsonl`](xbox-series-s-game-phase0-final-lifecycle-10.0.26100.9426.jsonl).
