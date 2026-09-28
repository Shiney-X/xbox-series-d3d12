# Regressão desktop Vulkan — Deltarune

- Data do relato: 2026-09-28
- Ambiente: Linux, AppImage SDL do fork shadPS4 Shiney-X v0.18.0
- Jogo: DELTARUNE Chapter 1&2, CUSA15250
- Resultado informado pelo usuário: jogo iniciou, renderizou a tela de seleção
  de mão e continuou funcionando durante o teste manual.
- Evidência: captura de tela enviada na conversa; não há log arquivado neste
  repositório nem medição de desempenho reproduzível.
- Escopo: baseline visual da Fase 2I no backend Vulkan do PC. Não valida o
  primeiro corte 2J, o backend D3D12 ou a execução do jogo no Xbox.

Para validar o 2J, repetir o mesmo percurso com o binário produzido a partir
da PR correspondente e guardar o log do emulador, além de registrar eventuais
diferenças visuais ou travamentos.
