# xbox-series-d3d12

Port experimental do [shadPS4](https://github.com/shadps4-emu/shadPS4) para Xbox
Series S em Dev Mode, usando UWP x64 e um futuro backend Direct3D 12 nativo.

> [!WARNING]
> A Fase 0 de viabilidade foi concluída no Xbox Series S. O projeto ainda não
> executa jogos; o próximo trabalho é integrar o upstream shadPS4.

## Estado atual

- [x] Direção arquitetural documentada.
- [x] Backend D3D12 nativo escolhido como caminho principal.
- [x] Biblioteca compartilhada de probes para capabilities, memória virtual,
  código executável e dispositivo D3D12.
- [x] Host UWP x64 com pacote de sideload para Xbox Dev Mode.
- [x] Apresentação D3D12 mínima com indicação visual de sucesso ou falha.
- [x] Execução, DXIL e triângulo validados no Series S em perfil Game.
- [x] Placeholders e aliases de memória validados no Series S.
- [x] Pressão de memória e ciclo suspender/retomar validados no Series S.
- [ ] Importação do upstream shadPS4.
- [ ] Backend D3D12 integrado ao video core.

## Por que começar por probes?

Em hosts x86-64, o shadPS4 executa o código x86-64 do guest nativamente e usa
mapeamentos virtuais, proteção de páginas e trampolines gerados em runtime. O
sandbox UWP precisa permitir esse modelo antes que o trabalho no renderer tenha
valor. A capability `codeGeneration` será declarada no pacote de teste.

## Compilar os probes no Windows

Requisitos:

- Windows 10/11 x64;
- Visual Studio 2022 com o workload C++;
- CMake 3.25 ou superior.

```powershell
cmake --preset windows-msvc
cmake --build --preset windows-msvc-debug
ctest --preset windows-msvc-debug
```

Execução manual:

```powershell
out\build\windows-msvc\Debug\xbox_phase0_probe.exe --all
```

Os resultados desktop são emitidos como JSON Lines. O host UWP grava o mesmo
formato de maneira síncrona em `LocalState/phase0-results.jsonl`, com fallback
para `LocalCache/phase0-results.jsonl`, e inclui o probe `report-storage`. No
console, uma tela verde significa que CPU/memória/D3D12, apresentação e
persistência passaram; uma tela vermelha significa falha em pelo menos uma
dessas etapas.

## Testar no Xbox Series S

O workflow **Windows probes** produz o artefato temporário
`xbox-phase0-uwp-sideload`, com o pacote x64 assinado e o certificado público.
O roteiro completo de instalação, execução e coleta está em
[Teste no Xbox Dev Mode](docs/TESTING_XBOX_DEV_MODE.md).

## Documentação

- [Arquitetura](docs/ARCHITECTURE.md)
- [Roadmap](docs/ROADMAP.md)
- [Matriz de plataforma](docs/PLATFORM_MATRIX.md)
- [Teste no Xbox Dev Mode](docs/TESTING_XBOX_DEV_MODE.md)
- [Baseline upstream](docs/UPSTREAM.md)
- [Referências técnicas](docs/REFERENCES.md)
- [ADR-0001: UWP como baseline público](docs/adr/0001-uwp-public-baseline.md)
- [ADR-0002: backend D3D12 nativo](docs/adr/0002-native-d3d12-backend.md)
- [ADR-0003: integração do upstream com histórico](docs/adr/0003-upstream-history-integration.md)

## Escopo legal

Este projeto não fornece jogos, firmware, módulos, chaves ou conteúdo
proprietário da Sony ou da Microsoft. SDKs e artefatos protegidos por NDA não
devem ser versionados.
