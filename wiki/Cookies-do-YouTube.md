# Cookies do YouTube

## Quando eu preciso disso?

Só quando as músicas falham com:

```
Sign in to confirm you're not a bot
```

O YouTube costuma pedir isso para IPs de VPS e datacenter. Com os cookies, o yt-dlp usa o **seu login** do YouTube e o bloqueio some. Se as músicas já tocam, você não precisa de cookies.

> **Use uma conta secundária.** O arquivo de cookies dá acesso à conta, como uma senha. Não compartilhe e não envie esse arquivo para o GitHub. Uma conta usada por servidor pode ser limitada pelo YouTube, então não use a sua conta principal.

## Passo 1 - Exportar o cookies.txt

1. Instale uma extensão que exporta no formato **cookies.txt (Netscape)**:
   - Chrome / Edge / Opera: **Get cookies.txt LOCALLY**
   - Firefox: **cookies.txt**
2. Nas configurações da extensão, permita que ela rode em **janela anônima/privada**.
3. Abra uma **janela anônima**, entre na sua conta do Google e abra **youtube.com**.
4. Ainda na aba do YouTube, clique na extensão e exporte os cookies (**Export** / **Current Site**). Salve como `cookies.txt`.
5. **Feche a janela anônima** logo em seguida, sem sair da conta (não clique em "Sair").

Por que janela anônima? Enquanto o YouTube está aberto no navegador, ele troca os cookies de tempos em tempos e os exportados param de valer. Fechando a janela anônima, ninguém mais usa aqueles cookies, e eles duram bem mais.

**Confira o arquivo** abrindo no Bloco de Notas. Ele deve começar assim:

```
# Netscape HTTP Cookie File
.youtube.com	TRUE	/	TRUE	17...	LOGIN_INFO	AFmmF2sw...
.youtube.com	TRUE	/	TRUE	17...	__Secure-3PSID	g.a000...
```

Se o arquivo começar com `[` ou `{` (JSON), a extensão exportou no formato errado: escolha **Netscape** / **cookies.txt** nela.

## Passo 2 - Colocar o arquivo no servidor

Envie o `cookies.txt` para a pasta do servidor (pelo FTP ou pelo gerenciador de arquivos da hospedagem). Sugestão:

```
servidor/
  omp-server.exe   (ou samp-server.exe / omp-server / samp03svr)
  server.cfg
  scriptfiles/
    audiocast/
      cookies.txt      <- aqui
```

## Passo 3 - Configurar

O caminho é **relativo à pasta do servidor** (a pasta onde está o `omp-server`/`samp-server`). Use **uma** das formas:

**server.cfg**

```
audiocast_cookies scriptfiles/audiocast/cookies.txt
```

- Não coloque comentário (`#`) na mesma linha: ele vira parte do caminho.
- Caminho completo também funciona: `audiocast_cookies C:\servidor\scriptfiles\audiocast\cookies.txt` (aspas são opcionais, mesmo com espaços).

**config.json (open.mp)**

```json
"audiocast": {
    "cookies": "scriptfiles/audiocast/cookies.txt"
}
```

No JSON use `/` no caminho (ou `\\` dobrado: `"C:\\servidor\\cookies.txt"`).

**Variável de ambiente:** `AUDIOCAST_COOKIES=scriptfiles/audiocast/cookies.txt`.

## Passo 4 - Reiniciar e conferir o log

A opção é lida quando o servidor inicia, então **reinicie o servidor**. No log aparece uma destas mensagens:

| Mensagem no log | O que significa |
|---|---|
| `using the YouTube sign-in from C:\...\cookies.txt` | Pronto, está funcionando. |
| `... was not found, so songs load without cookies. A relative path starts at the server folder (...)` | O caminho está errado. A mensagem mostra o caminho completo que o plugin procurou e a pasta do servidor: compare com onde o arquivo realmente está. |
| `... is not a cookies.txt file (Netscape format)` | O arquivo não é um cookies.txt. Exporte de novo no formato Netscape (Passo 1). |
| `... has YouTube cookies but no sign-in` / `has no youtube.com cookies` | Os cookies foram exportados sem estar logado, ou de outro site. Entre na conta, abra youtube.com e exporte desta aba. |

Com `was not found` ou `is not a cookies.txt file` o plugin ignora o arquivo e as músicas são baixadas sem cookies até você corrigir; nada quebra, só o bloqueio do YouTube continua.

## Testar sem abrir o jogo

A ferramenta que vem no zip carrega uma música exatamente como o plugin, usando os cookies:

```
tools/audio-cast-check --cookies scriptfiles/audiocast/cookies.txt "https://youtu.be/dQw4w9WgXcQ" auto scriptfiles/audiocast
```

Rode na pasta do servidor. Se terminar com `fully loaded`, está tudo certo.

## Depois de configurado

- **O yt-dlp atualiza o arquivo** a cada música (ele grava os cookies renovados de volta). Deixe o arquivo com permissão de escrita e não estranhe se o conteúdo mudar.
- **Cookies vencem.** Se o `Sign in to confirm you're not a bot` voltar depois de semanas, repita o Passo 1 e substitua o arquivo.
- Ainda bloqueado mesmo com cookies válidos? Alguns IPs são bloqueados com mais força; espere algumas horas ou use outra conta.
