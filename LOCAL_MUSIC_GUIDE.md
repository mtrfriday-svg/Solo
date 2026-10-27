# Music on the robot: XiaoZhi "local music library" (server side)

I removed my own internet-link player. Music now uses the **official XiaoZhi way**: the **XiaoZhi server's `play_music` plugin**,
which plays music files stored in a folder **on the server computer** ("local library"). The robot firmware has no music player of its own:
the server sends the music to the robot through the same audio channel as its voice, and the robot's speaker plays it.

## What you need
* A **self-hosted XiaoZhi server** (project `xiaozhi-esp32-server`) running on a computer that stays on and is on the **same Wi-Fi** (a PC, mini PC,
  Raspberry Pi, or a rented small server). The robot talks to it instead of the free xiaozhi.me cloud.
* A **music folder** on that server with your own MP3 files.

I am not sure the free hosted service (xiaozhi.me) offers a local music library, because the library lives on a server you control. With only a phone you
cannot host the server easily. I have **not tested** running it in Termux, and it can be heavy; a Raspberry Pi or small server is the safe option.

## How it works (plain words)
1. You say "play <song name>".
2. The AI on the server calls its `play_music` function.
3. The server looks in its music folder (`music_dir`, scanned including sub-folders) for a file whose name best matches what you said.
4. The server streams that file to the robot, which plays it on its speaker.
5. While it plays, the robot behaves as if it were speaking (the face mouth moves, NeoPixels pulse green). Music from the server is not controlled by my code,
   so the robot's buttons and phone page have no music controls.

## Server setup (on the server computer)
1. Install the server following its official README (Docker or source). Choose the "local config" mode.
2. In the server's `config.yaml` (or `data/.config.yaml`):
   * enable the function `play_music` in the list of functions the AI may call,
   * under the `play_music` plugin set `music_dir` (for example `./music`), `music_ext` (file types such as `.mp3`) and `refresh_time` (how often the folder is rescanned).
   Exact key names can differ between server versions: check your version's `config.yaml`, which has them.
3. Copy your MP3 files into `music_dir`. Give them clear names ("lofi beats.mp3"), because the AI matches your words to file names.
4. Start the server and note its address, for example `192.168.0.65`.

## Point the robot to your server (one-time rebuild)
The robot normally uses the xiaozhi.me cloud. To use your own server, add the server's OTA address to the robot build.
In Termux:
```
cd ~/Bot2-main
nano main/boards/mdee-robot/config.json
```
In the `sdkconfig_append` list add one line (use your server's address and keep the comma rules of JSON):
```
"CONFIG_OTA_URL=\"http://192.168.0.65:8003/xiaozhi/ota/\"",
```
Save (CTRL+O, Enter), exit (CTRL+X), then:
```
bash termux_push.sh "use my own server"
```
Download the new `mdee_brain_n16r8_…` artifact and flash it. Then the robot connects to your server. (If you remove the line, it goes back to xiaozhi.me.)
The robot must activate/connect with the server as described in the server's README (its OTA service gives the robot the connection address).

## Voice commands
Speak naturally; the AI decides. Examples (depends on file names and the AI model):
* "Play lofi beats."  * "Play some music."  * "Play <part of a file name>."  * "Stop the music." / "Stop." / "Be quiet."
To interrupt music, say the wake word or click the BOOT button (it starts a new conversation turn).

## Radio
No FM radio exists in this robot and no internet-radio player is included any more. Real FM would need an FM module (RDA5807M), an antenna
and an analog amplifier, plus new code. Ask me if you want that as a separate project.

## Troubleshooting
| Problem | Fix |
|---|---|
| "I can't play music" / nothing happens | The `play_music` function is not enabled on the server, or `music_dir` is empty or wrong |
| Wrong song | Rename files clearly; say more of the name |
| Robot cannot reach the server | Server on and on the same Wi-Fi; correct IP; ports 8000/8003 open; re-flash with the right `CONFIG_OTA_URL` |
| Works on xiaozhi.me but not on my server after flashing | Server address or OTA URL typo; check the server log |
| Music stutters | Weak Wi-Fi on the robot or an overloaded server |
