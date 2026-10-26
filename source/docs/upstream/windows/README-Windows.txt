Upstream reference: The Order: 1886 for PC (theorder1886-port) - version 0.7.2
=========================================================

No game files are included. You need your own copy of The Order: 1886 as a folder that contains
eboot.bin, sce_module and sce_sys. Tested: CUSA00035, version 01.02.

This is a work in progress. Checked so far: the logos, the title screen, the prologue and the first
gameplay after it (the cover tutorial, from a save), at the game's own 30 FPS without crashes on an
NVIDIA GeForce RTX 5060 laptop. Not checked yet: most of the campaign, sound, AMD and Intel
graphics cards. Known issues: the first session after an update builds the shader cache again
(objects can appear a moment late, and there are short pauses); later sessions pause briefly when
a scene loads; a rare black triangle can flash for one frame.

UPDATING FROM PREVIOUS VERSIONS (VERY IMPORTANT)
-----------------------------------------------
When upgrading from older releases (v0.6.x, v0.7.0, v0.7.1) or another fork to avoid
conflicts, stale shader caches, black screens, or settings loops, delete these files:

1. Launcher settings in AppData:
   %APPDATA%\theorder1886-launcher\settings.json
   (or C:\Users\<YourUsername>\AppData\Roaming\theorder1886-launcher\settings.json)
   If you used the other fork, also delete:
   C:\Users\<YourUsername>\AppData\Roaming\order1886-launcher\settings.json

2. Game configuration INI inside the extracted port folder:
   Delete 'theorder.ini' (or 'order1886.ini')

3. Shader and pipeline cache folder inside the port folder:
   Delete the 'cache' folder inside 'user':
   <PortFolder>\user\cache
   (Your save files in <PortFolder>\user\savedata are safe and will NOT be touched).

Requirements
- Windows 10 (1903 or newer) or Windows 11, 64-bit.
- A 64-bit processor with AVX. Older processors stop at start with a "STOP: this CPU lacks" line
  in the log.
- A graphics card with Vulkan 1.3 and an up-to-date driver.
  Nothing else: Python and the libraries are inside this folder.

Starting
1. Extract this folder anywhere, for example C:\Games\theorder1886-windows (not inside the zip).
2. Open "The Order 1886.exe". If Windows shows "Windows protected your PC", click More info,
   then Run anyway: the program is not digitally signed.
3. On the Game page, choose the game folder (the one with eboot.bin).
4. Back on Home, click PLAY. The first start takes longer while the game image is prepared and
   the shaders are built; later starts reuse the shader cache.

Frame rate: the game runs at its original 30 FPS. "60 (experimental)" under Engine frame rate
applies a 60 FPS patch; the speed of the game stays correct, and the prologue averages about 57
FPS (heavy scenes dip to the 40s).

Aspect ratio: the game's picture is 2.40:1 with black bars. Graphics > Aspect ratio (or the
in-game menu, Image tab) picks 16:9, 16:10 or 21:9 to fill a screen of that shape: the view keeps
its width and shows more above and below. Applied when the game starts again.

While playing
- Insert on the keyboard, or R3 + L2 on the controller, opens the port's menu (display, frame
  limit, performance overlay).
- Keyboard: WASD move, arrows camera, Space or Enter = Cross, Left Shift or Esc = Circle,
  E = Square, Q = Triangle, 1/3 = L1/R1, R/F = L2/R2, Z/C = L3/R3.

Files
- Saves, the shader cache and logs stay in this folder (user\). Reinstalling over it keeps them.
  The pipeline cache keeps at most 4 GB (older entries are removed at start).

---------------------------------------------------------------------------------------------
Português (Brasil)

Nenhum arquivo do jogo está incluído: use o dump da sua própria cópia de The Order: 1886 (pasta
com eboot.bin, sce_module e sce_sys). Testado: CUSA00035, versão 01.02.

Em desenvolvimento. Verificado: logos, tela de título e a cinemática do prólogo, nos 30 FPS
originais e sem crashes numa NVIDIA GeForce RTX 5060 (notebook). Ainda não verificado: gameplay
depois do prólogo, som, salvar, placas AMD e Intel. Problemas conhecidos: a primeira sessão monta o
cache de shaders (objetos podem aparecer com um instante de atraso e há pausas curtas); nas
seguintes há uma pausa breve quando uma cena carrega; raramente um triângulo preto pisca por um
quadro.

Como jogar: extraia a pasta, abra "The Order 1886.exe" (se o Windows avisar, clique em "Mais
informações" e "Executar assim mesmo"), escolha a pasta do jogo na página Jogo e clique em JOGAR.
A primeira execução demora mais (preparação da imagem do jogo e dos shaders).

No jogo: Insert (ou R3 + L2 no controle) abre o menu do port (tela, limite de quadros e overlay de
desempenho).
