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
- SHA-256 do JSONL: `07da540e6ef75480ba3361bc101ac423a4cf927b6d35b60db225f9b15519348d`.

Arquivo bruto:
[`xbox-series-s-game-triangle-10.0.26100.9426.jsonl`](xbox-series-s-game-triangle-10.0.26100.9426.jsonl).

O valor de 128 MiB retornado como memória de vídeo dedicada é a visão do
DXGI dentro do sandbox UWP. Ele não representa a memória física total do
console nem deve ser usado como budget de residency.

Durante a coleta, o Device Portal não exibiu o `LocalState` do pacote quando
havia um usuário conectado. Sem usuário conectado, o mesmo pacote executado
como Game expôs normalmente `LocalState/phase0-results.jsonl`. Isso é tratado
como uma particularidade de enumeração do armazenamento por usuário no Device
Portal, não como falha do probe.
