# Matriz de plataforma

| Capacidade | Windows desktop x64 | Xbox UWP Dev Mode | Xbox GDK/GameCore |
|---|---:|---:|---:|
| Execução x86-64 nativa | Sim | A validar | Sim, acesso condicionado |
| `codeGeneration` | Não requerida | Requerida | Modelo diferente |
| D3D12 | Sim | Sim | Sim, API específica |
| Vulkan nativo | Dependente do driver | Não | Não é o baseline |
| Filesystem irrestrito | Sim | Não | APIs GDK |
| Qt desktop | Sim | Não planejado | Não planejado |
| Limite de memória do alvo | Sistema | Medir; documentação pública indica até 5 GB para jogos | Contratual/NDA |
| Uso neste repositório | Desenvolvimento e CI | Alvo público | Overlay futuro e autorizado |

Nenhum resultado desktop conta como prova de suporte no Xbox. Cada capability
crítica deve ser medida no console, sem debugger, e registrada com a versão do
sistema operacional.
