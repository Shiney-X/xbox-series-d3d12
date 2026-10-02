# ADR 0036 — Estados explícitos e transferências no host D3D12

- Status: aceito; validação no Series S pendente
- Data: 2026-10-02

## Viabilidade e dependências

As fases 3A–3D validaram device, tickets, recursos, descriptors e PSOs no
Series S. O renderer ainda gravava barriers e cópias diretamente, tornando
difícil verificar estados/heaps/footprints antes da integração VideoCore.
Este bloco usa apenas D3D12 e os componentes existentes; não exige Vulkan,
nova biblioteca ou GDK.

## Decisão e implementação

- Introduzir encoder não copiável de gravação em fila DIRECT, com estados
  iniciais explícitos e uniformes por recurso. Não reutilizar após Reset,
  misturar com barriers manuais nem presumir estados de outras filas.
- Emitir transition/UAV barriers; contar transições repetidas sem emiti-las.
  Rejeitar combinações read/write inválidas e estados incompatíveis com
  flags, dimensões e heaps. UPLOAD/READBACK conservam seus estados fixos.
- Diferenciar estado em gravação de estado após ExecuteCommandLists:
  buffers DEFAULT e texturas simultaneous-access decaem para COMMON. Não
  transportar o estado registrado cegamente para outra submissão.
- Validar offsets/tamanhos sem overflow e rejeitar cópia do buffer nele
  mesmo. Cópias de textura são completas, mip/layer/plane únicos, com
  footprint validado por GetCopyableFootprints e limite do buffer.
- Clear de cor usa RTV; clear de depth cobre D32_FLOAT, sem stencil. O
  chamador garante associação correta do descriptor ao recurso e sua
  vida útil até o fence. Resolve de cor exige dimensões/formatos idênticos,
  suporte consultado e origem MSAA/destino single-sample.
- Integrar a apresentação/clear da swapchain, upload de ícones e readback
  do compute. O encoder mantém referências durante gravação, mas o chamador
  mantém proprietários e descriptors até o ticket terminar.

## Validação real e procedimento

Na abertura do MSIX, o host executa duas cópias de buffer com offsets,
upload/readback de textura 7×3 com footprint deslocado, clear de cor/depth
e resolve MSAA de cor quando suportado. Comparação CPU verifica dados,
pixels e depth depois do fence; não é um stub. Recursos temporários entram
no orçamento da 3C e são liberados ao concluir. `d3d12-transfers` registra
resultados/contadores; suporte ausente a resolve não aprova esse gate.

O teste Windows/WARP cobre o mesmo autoteste, pedidos inválidos que não
emitem transições e duas submissões separadas de cópia de buffer com decay.
A debug layer é habilitada quando disponível e mensagens ERROR/CORRUPTION
fazem o teste falhar. A validação de apresentação/ícones e retomada no
Series S está em [PHASE3_VALIDATION.md](../PHASE3_VALIDATION.md).

## Limites

Não há rastreamento global/subresource divergente, split/enhanced barriers,
aliasing de placed/reserved resources, copy queue, stencil clear, cópia
parcial de imagem ou resolve de depth. Uma gravação descartada não publica
estados para o host; o contrato de ordem/tickets continua na 3B. Os próximos
consumidores precisam ampliar esses contratos antes de executar operações
guest que não caibam neste subconjunto. Este bloco não inicia jogos PS4.

Referência primária: [estados, barriers e decay no D3D12](https://learn.microsoft.com/en-us/windows/win32/direct3d12/using-resource-barriers-to-synchronize-resource-states-in-direct3d-12).
