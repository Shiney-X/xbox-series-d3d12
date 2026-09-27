# ADR-0010: fronteira de comandos entre PM4 e renderer

## Estado

Aceito como corte da Fase 2B.

## Contexto

`AmdGpu::Liverpool` interpreta os pacotes PM4 do PS4, mas seu campo de
submissão e seu cabeçalho dependiam diretamente de `Vulkan::Rasterizer`.
Isso impede conectar um renderer D3D12 ao mesmo processador de comandos.

## Decisão

Introduzir `VideoCore::GpuCommandSink` para receber os comandos já decodificados
por Liverpool. Seus argumentos são endereços, tamanhos e valores do guest,
sem handles de Vulkan ou D3D12. O contrato cobre draw direto/indireto,
dispatch direto/indireto, DMA/GDS, processamento de downloads, sincronização,
submissão e marcadores de diagnóstico.

`Vulkan::Rasterizer` implementa a interface atual, preservando o comportamento
de execução. `Liverpool` armazena apenas um ponteiro para o contrato e continua
funcionando sem renderer quando o modo NullGPU estiver ativo.

## Consequências

- O processador PM4 deixa de incluir o cabeçalho do rasterizer Vulkan.
- O contrato expressa somente operações que Liverpool já enviava; não cria
  cópias de recursos ou estados nativos de uma API gráfica.
- Caches, pipelines e recursos ainda precisam de cortes posteriores antes de
  um backend D3D12 conseguir executar jogos.
- O teste de regressão principal deste corte é compilar Liverpool e o
  rasterizer Vulkan, seguido dos testes C++ existentes nas plataformas do CI.
