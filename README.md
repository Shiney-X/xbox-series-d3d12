# xbox-series-d3d12

Port experimental do [shadPS4](https://github.com/shadps4-emu/shadPS4) para Xbox
Series S em Dev Mode, usando UWP x64 e um futuro backend Direct3D 12 nativo.

> [!WARNING]
> A Fase 0 de viabilidade foi concluída no Xbox Series S. O código upstream está
> integrado, mas este port ainda não executa jogos no Xbox.

## Estado atual

- [x] Direção arquitetural e backend D3D12 nativo documentados.
- [x] Memória virtual, código executável, aliases e limite de 5 GiB validados.
- [x] D3D12, DXC/DXIL, apresentação e suspensão/retomada validados no Series S.
- [x] Histórico do shadPS4 `v.0.18.0` integrado na raiz.
- [ ] Build desktop upstream validado sem regressões.
- [ ] Frontend desktop isolado do futuro host UWP.
- [ ] Backend D3D12 integrado ao video core.

## Baseline upstream

O primeiro baseline do core é o release `v.0.18.0`, commit
`e3ce810f3a653f43ac64ebab63023de281a4103a`. A integração foi feita por merge
com histórico completo; não é um snapshot nem um submódulo. Consulte
[Baseline upstream](docs/UPSTREAM.md) e
[ADR-0003](docs/adr/0003-upstream-history-integration.md).

O código original, seus submódulos, licenças e instruções de compilação estão
preservados. As instruções desktop oficiais ficam em `documents/`.

## Compilar os probes da Fase 0 no Windows

Requisitos:

- Windows 10/11 x64;
- Visual Studio 2022 com o workload C++;
- CMake 3.30 ou superior.

```powershell
cmake --preset windows-msvc
cmake --build --preset windows-msvc-debug
ctest --preset windows-msvc-debug
```

Execução manual:

```powershell
out\build\windows-msvc\Debug\xbox_phase0_probe.exe --all
```

O preset `windows-msvc` ativa `XBOX_D3D12_PHASE0_ONLY`; portanto, essa validação
não configura nem compila as dependências completas do emulador.

## Testar no Xbox Series S

O workflow **Windows probes** produz o artefato temporário
`xbox-phase0-uwp-sideload`, com o pacote x64 assinado e o certificado público.
O roteiro completo está em
[Teste no Xbox Dev Mode](docs/TESTING_XBOX_DEV_MODE.md).

## Documentação do port

- [Arquitetura](docs/ARCHITECTURE.md)
- [Roadmap](docs/ROADMAP.md)
- [Matriz de plataforma](docs/PLATFORM_MATRIX.md)
- [Teste no Xbox Dev Mode](docs/TESTING_XBOX_DEV_MODE.md)
- [Baseline upstream](docs/UPSTREAM.md)
- [Referências técnicas](docs/REFERENCES.md)
- [ADR-0001: UWP como baseline público](docs/adr/0001-uwp-public-baseline.md)
- [ADR-0002: backend D3D12 nativo](docs/adr/0002-native-d3d12-backend.md)
- [ADR-0003: integração do upstream com histórico](docs/adr/0003-upstream-history-integration.md)

## Escopo legal e licença

O projeto não fornece jogos, firmware, módulos, chaves ou conteúdo proprietário
da Sony ou da Microsoft. SDKs e artefatos protegidos por NDA não devem ser
versionados.

O core shadPS4 e o código deste port são distribuídos sob GPL-2.0-or-later.
Consulte [LICENSE](LICENSE) e [LICENSES](LICENSES/).
