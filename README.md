# xbox-series-d3d12

Port experimental do [shadPS4](https://github.com/shadps4-emu/shadPS4) para Xbox
Series S em Dev Mode, usando UWP x64 e um futuro backend Direct3D 12 nativo.

> [!WARNING]
> A Fase 0 de viabilidade foi concluída no Xbox Series S. O código upstream está
> integrado e a primeira interface UWP nativa está em validação, mas este port
> ainda não executa jogos no Xbox.

## Estado atual

- [x] Direção arquitetural e backend D3D12 nativo documentados.
- [x] Memória virtual, código executável, aliases e limite de 5 GiB validados.
- [x] D3D12, DXC/DXIL, apresentação e suspensão/retomada validados no Series S.
- [x] Histórico do shadPS4 `v.0.18.0` integrado na raiz.
- [x] Build desktop Windows upstream validado sem regressões.
- [x] Frontend desktop isolado do futuro host UWP por contrato de janela injetável.
- [x] Shell inicial UWP/D3D12 e bridge verificável com o core upstream.
- [x] Navegação da interface validada no Xbox Series S em perfil Game.
- [x] Validar acesso direto ao armazenamento USB no Xbox.
- [x] Validar o navegador de pastas USB nativo no Xbox.
- [x] Validar descoberta de jogos e leitura de `param.sfo` no Xbox.
- [x] Validar decodificação e apresentação de `icon0.png` no Xbox.
- [x] Iniciar a Fase 2 com coerência de memória GPU desacoplada do Vulkan.
- [ ] Backend D3D12 integrado ao video core.

A fundação D3D12 do host (3A–3F) foi validada no Series S. A 3F adiciona
um consumidor experimental da interface `VideoCore::GpuCommandSink` para
DMA de buffers e um preview sintético em Diagnostics. Isso **não** significa
que o Liverpool/PM4, shaders guest ou execução de jogos estejam ligados ao
UWP; o item de backend completo acima permanece pendente. Procedimento:
[validação da Fase 3](docs/PHASE3_VALIDATION.md).

A 4A inicia SPIR-V → HLSL → DXIL no próprio UWP com SPIRV-Cross,
dispatch compute e readback de uma fixture autoral; validação no console
pendente. Ainda não traduz shaders de jogos nem compila o core completo.
Procedimento: [validação da Fase 4](docs/PHASE4_VALIDATION.md).

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

## Testar a interface no Xbox Series S

O workflow **Xbox UWP shell** produz o artefato temporário
`xbox-shell-uwp-sideload`, com o pacote x64 assinado e o certificado público.
Essa interface é renderizada diretamente por D3D12 no host UWP do console;
não é um mock desktop. Na tela Jogos, o direcional percorre as pastas do
USB, **A** abre a pasta, **B** sobe um nível e **X** seleciona e examina a
pasta atual em busca de jogos PS4.
O pacote `0.5.0.0` também decodifica `sce_sys/icon0.png`, limita a imagem a
256×256 e envia somente o ícone selecionado para uma textura D3D12. Jogos sem
ícone continuam aparecendo com um fallback textual.
O roteiro completo está em
[Teste no Xbox Dev Mode](docs/TESTING_XBOX_DEV_MODE.md).

## Documentação do port

- [Arquitetura](docs/ARCHITECTURE.md)
- [Roadmap](docs/ROADMAP.md)
- [Validação da Fase 2L](docs/PHASE2L_VALIDATION.md)
- [Validação da Fase 3 no Xbox](docs/PHASE3_VALIDATION.md)
- [Matriz de plataforma](docs/PLATFORM_MATRIX.md)
- [Teste no Xbox Dev Mode](docs/TESTING_XBOX_DEV_MODE.md)
- [Baseline upstream](docs/UPSTREAM.md)
- [Referências técnicas](docs/REFERENCES.md)
- [ADR-0001: UWP como baseline público](docs/adr/0001-uwp-public-baseline.md)
- [ADR-0002: backend D3D12 nativo](docs/adr/0002-native-d3d12-backend.md)
- [ADR-0003: integração do upstream com histórico](docs/adr/0003-upstream-history-integration.md)
- [ADR-0004: fronteira de janela injetada pelo host](docs/adr/0004-host-window-boundary.md)
- [ADR-0005: bridge do core e shell UWP](docs/adr/0005-uwp-core-bridge-and-shell.md)
- [ADR-0006: acesso USB à biblioteca](docs/adr/0006-uwp-library-folder-access.md)
- [ADR-0007: descoberta de jogos e metadados PSF](docs/adr/0007-uwp-game-library-scan.md)
- [ADR-0008: texturas de ícone da biblioteca UWP](docs/adr/0008-uwp-library-icon-textures.md)
- [ADR-0009: coerência de memória GPU neutra](docs/adr/0009-backend-neutral-gpu-memory-tracking.md)

## Escopo legal e licença

O projeto não fornece jogos, firmware, módulos, chaves ou conteúdo proprietário
da Sony ou da Microsoft. SDKs e artefatos protegidos por NDA não devem ser
versionados.

O core shadPS4 e o código deste port são distribuídos sob GPL-2.0-or-later.
Consulte [LICENSE](LICENSE) e [LICENSES](LICENSES/).
