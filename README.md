# Hue Browser

Navegador desktop em C com GTK 3 e WebKitGTK. A interface tem abas, barra de endereço e pesquisa, navegação, atalhos básicos e opção para não carregar imagens.

## Por que WebKitGTK

Qt WebEngine e CEF usam Chromium; este projeto usa WebKitGTK para evitar esse motor. Reutilizar um motor existente também é mais seguro e realista do que criar um motor web novo: um motor próprio teria de implementar HTML, CSS, JavaScript, TLS, codecs e APIs web, além de acompanhar correções de segurança e compatibilidade.

WebKitGTK mantém regras de compatibilidade de user-agent para sites conhecidos que bloqueiam ou degradam o navegador com o identificador padrão. Isso permite enviar um identificador Chrome nos casos em que o próprio WebKit sabe que é necessário, sem anunciar Chrome indiscriminadamente para todos os sites. Um user-agent Chrome global não adiciona APIs Blink/Chromium e pode piorar a compatibilidade quando um site escolhe recursos com base nesse identificador.

## Memória

2 GB é o alvo mínimo para abrir o navegador e usar páginas leves, preferencialmente com poucas abas. Não é uma garantia de que qualquer site moderno, vídeo ou quantidade de abas funcione sem pressão de memória: páginas web variam muito e podem consumir mais RAM do que o próprio navegador. A opção **Carregar imagens** pode reduzir tráfego e memória de páginas com muitas imagens. Em máquinas de 2 GB, use poucas abas e mantenha swap habilitada.

## Perfil e logins

O Hue Browser guarda os dados dos sites e os cookies persistentes no perfil
`~/.local/share/hue-browser`. Assim, logins que os próprios sites permitem
manter entre reinicializações continuam disponíveis ao fechar e abrir o
navegador. Sites que usam cookies estritamente temporários ou invalidam sessões
no servidor ainda podem pedir login novamente.

## Dependências e compilação

No Debian 13:

```sh
sudo apt install build-essential cmake pkg-config libgtk-3-dev libwebkit2gtk-4.1-dev
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j2
./build/hue-browser
```

O WebKitGTK deve vir dos repositórios de segurança atualizados da distribuição. O motor é uma dependência grande do sistema; o executável e a interface do navegador são pequenos.

## Instalação automática

No checkout local, rode:

```sh
sh install.sh
```

O script instala as dependências de compilação pelo `apt`, compila uma versão Release e instala o executável em `~/.local/bin/hue-browser`. Também cria `~/.local/share/applications/hue-browser.desktop` para o Hue Browser aparecer no menu de aplicativos. Se as dependências ainda não estiverem instaladas, ele usa `sudo` e poderá pedir sua senha.

Depois que este projeto estiver publicado no GitHub, o comando remoto será:

```sh
curl -fsSL 'https://raw.githubusercontent.com/wanbnn/huebrowser/main/install.sh' \
  | sh -s -- 'https://github.com/wanbnn/huebrowser/archive/refs/heads/main.tar.gz'
```

O instalador usa o código-fonte da branch `main` e instala o binário em `~/.local/bin/hue-browser`.

## Smoke test e medição de memória

Com o navegador compilado e `xdotool` instalado, rode o cenário automatizado em um desktop ou em X virtual:

```sh
sudo apt install xvfb xdotool
xvfb-run -a python3 tools/memory_benchmark.py --output real-site-results.json
```

O script serve páginas locais determinísticas e verifica requisições da página simples, execução de JavaScript em um DOM com 5.000 nós e carregamento de 36 imagens. Mede a soma de RSS e PSS dos processos descendentes do navegador em cinco cenários, incluindo três abas abertas. PSS é a métrica principal por dividir memória compartilhada entre processos; RSS é incluída para comparação. A memória medida é do processo, não a memória livre total do sistema. O arquivo JSON registra a versão do kernel e a RAM visível ao ambiente de execução.

Para comparar máquinas, use a mesma versão do WebKitGTK, kernel, resolução, duração (`--settle-seconds`) e cenários. Xvfb mede sem compositor desktop e seus próprios recursos não entram na soma dos processos do navegador. O script falha se as rotas de qualidade não forem carregadas, JavaScript não fizer o callback esperado, as imagens não forem requisitadas ou o navegador encerrar.

O benchmark também abre páginas públicas reais do Hacker News, Python.org, MDN Web APIs e GitHub/WebKit. Para cada site, ele aguarda o título da página aparecer na barra de título da janela, mede o navegador por 12 segundos (`--site-seconds`) e grava o título observado. Os resultados reais dependem da rede, dos redirecionamentos, dos anúncios/recursos servidos e das mudanças dos sites; compare-os separadamente dos cenários locais. O teste não entra em contas nem envia formulários.

## User-agent e compatibilidade

O projeto não força um user-agent Chrome para toda navegação. O WebKitGTK aplica identificadores de compatibilidade por site para alguns serviços que rejeitam WebKitGTK. Isso não garante acesso a todo site que bloqueie motores diferentes do Chromium; alguns sites verificam recursos JavaScript e APIs, não apenas o cabeçalho ou `navigator.userAgent`.
