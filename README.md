# xbox-series-d3d12

Port experimental do [shadPS4](https://github.com/shadps4-emu/shadPS4) para Xbox
Series S em Dev Mode, usando UWP x64 e um futuro backend Direct3D 12 nativo.

> [!WARNING]
> O projeto está na fase de validação de viabilidade. Ele ainda não executa jogos
> no Xbox.

## Estado atual

- [x] Direção arquitetural documentada.
- [x] Backend D3D12 nativo escolhido como caminho principal.
- [x] Probes Win32 para capabilities, memória virtual e código executável.
- [ ] Host UWP empacotado e implantável no Xbox.
- [ ] Triângulo D3D12 no Series S.
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

Os resultados são emitidos como JSON Lines para facilitar coleta no Device
Portal e comparação entre Windows desktop e Xbox.

## Documentação

- [Arquitetura](docs/ARCHITECTURE.md)
- [Roadmap](docs/ROADMAP.md)
- [Matriz de plataforma](docs/PLATFORM_MATRIX.md)
- [Referências técnicas](docs/REFERENCES.md)
- [ADR-0001: UWP como baseline público](docs/adr/0001-uwp-public-baseline.md)
- [ADR-0002: backend D3D12 nativo](docs/adr/0002-native-d3d12-backend.md)

## Escopo legal

Este projeto não fornece jogos, firmware, módulos, chaves ou conteúdo
proprietário da Sony ou da Microsoft. SDKs e artefatos protegidos por NDA não
devem ser versionados.
