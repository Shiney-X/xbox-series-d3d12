# Matriz de plataforma

| Capacidade | Windows desktop x64 | Xbox UWP Dev Mode | Xbox GDK/GameCore |
|---|---:|---:|---:|
| Execução x86-64 nativa | Sim | Validada no Series S | Sim, acesso condicionado |
| `codeGeneration` | Não requerida | Validada com página RW→RX | Modelo diferente |
| Placeholders e aliases de memória | Disponíveis no Windows 10+ | Probe implementado; validação pendente | Modelo específico da plataforma |
| Dispositivo D3D12 | CI validado | Validado no `SraKmd_arden` | Sim, API específica |
| Swapchain D3D12 | Não necessária no runner | `CoreWindow` + `Present` validados | API específica |
| Graphics PSO + draw | Não necessário no runner | Triângulo validado no Series S | API específica |
| DXC + Shader Model 6/DXIL | Disponível no SDK | Validado via `IDxcCompiler` | Toolchain específica |
| Vulkan nativo | Dependente do driver | Não | Não é o baseline |
| Filesystem irrestrito | Sim | Não | APIs GDK |
| Qt desktop | Sim | Não planejado | Não planejado |
| Limite de memória do alvo | Sistema | Medir; documentação pública indica até 5 GB para jogos | Contratual/NDA |
| Uso neste repositório | Desenvolvimento e CI | Alvo público | Overlay futuro e autorizado |

Nenhum resultado desktop conta como prova de suporte no Xbox. Cada capability
crítica deve ser medida no console, sem debugger, e registrada com a versão do
sistema operacional.
