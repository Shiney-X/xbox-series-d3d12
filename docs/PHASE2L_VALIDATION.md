# Fase 2L — validação da fronteira gráfica

Esta etapa verifica que o desacoplamento da Fase 2 não quebrou o renderer
Vulkan existente. **Não testa D3D12 nem boot de jogos no Xbox**: o MSIX atual
continua sendo o shell/probe UWP. A evidência visual deve ser produzida no
shadPS4 SDL desktop, preferencialmente com um título já conhecido por funcionar
(por exemplo, DELTARUNE `CUSA15250`). Use somente uma cópia própria do jogo.

## O que a CI testa

- `GpuContractTrace.ReplaysNeutralTransitionsAgainstGoldenTrace` reproduz uma
  sequência **sintética e versionada** de transições de buffer/imagem e compara
  as transições emitidas com um golden textual. Ela detecta alterações de
  ordem, escopo e redundância de barriers no contrato neutro.
- `shadps4_compare_frames_test` verifica o leitor PNG, os limiares de diferença
  e o código de saída do comparador de frames.
- Os builds SDL Linux/Windows e os testes C++ continuam sendo exigidos. A
  falha `macos-sdl` por falta de `libclc` no Mesa já existia antes da 2L e deve
  ser acompanhada separadamente, sem ser tratada como aprovação visual.

O replay não captura pacotes PM4 de um jogo nem executa Vulkan/D3D12. É um
teste determinístico da política de sincronização. Ele **não substitui** a
comparação de screenshots reais.

## Captura visual manual

1. Guarde o hash/artefato de uma versão Vulkan anterior ao fechamento da 2K
   (por exemplo, a build da PR #29) como **referência**. Guarde o hash/artefato
   da build desta PR como **candidata**. Não misture versões de jogos, saves,
   firmware, sysmodules, driver, resolução ou opções gráficas.
2. Abra o mesmo título e a mesma cena estática nas duas builds. O atalho
   padrão `F12` pede uma captura **game-only** quando o RenderDoc não está
   carregado; `Alt+F12` inclui overlays e não deve ser usado aqui. As capturas
   são salvas na pasta de screenshots do usuário do shadPS4. Capture algumas
   amostras e escolha duas do mesmo estado visual, sem animação transitória.
3. Rode, a partir da raiz do repositório:

   ```bash
   python3 scripts/compare_frames.py /caminho/referencia.png /caminho/candidata.png \
     --report /caminho/phase2l-report.json \
     --diff /caminho/phase2l-diff.png
   ```

   O padrão é comparação RGB exata. O código de saída é `0` para aprovado,
   `1` para diferença acima do limite e `2` para PNG inválido, dimensões
   diferentes ou outro erro de entrada. O JSON traz dimensões, pixels
   alterados, proporção de alteração, erro médio e erro máximo por canal.
   O PNG de diferença mostra o valor absoluto do erro RGB.
4. Se a cena contiver variabilidade inevitável, explique-a e registre limites
   **antes** de aprovar a comparação. Exemplo, apenas quando justificado:

   ```bash
   python3 scripts/compare_frames.py referencia.png candidata.png \
     --pixel-threshold 2 --max-changed-ratio 0.001 --max-mean-error 0.1 \
     --report phase2l-report.json --diff phase2l-diff.png
   ```

   Não aumente os limites só para fazer um frame divergente passar. O leitor
   aceita PNG RGB8/RGBA8 não entrelaçado; compara RGB e ignora alfa. Isso
   corresponde às capturas usuais do presenter, mas outros PNGs serão
   rejeitados com mensagem explícita.

## Evidência mínima para fechar 2L

- Hash das duas builds, título/versão e configuração gráfica/driver.
- Cena usada e duas capturas **game-only** na mesma resolução.
- `phase2l-report.json` e, se houver diferença, `phase2l-diff.png` com uma
  avaliação humana da mudança.
- CI de testes de contrato e builds relevantes concluída. Um teste sintético
  aprovado não autoriza marcar o renderer Vulkan como visualmente validado.

Não publique dumps de jogos, sysmodules nem capturas que você não tenha direito
de compartilhar. Os relatórios pequenos podem ser anexados à PR; as imagens
podem ser enviadas diretamente para revisão ou descritas por seus hashes.
