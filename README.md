# Arduino-Source

This is the source code for all the main Arduino programs.

[<img src="https://canary.discordapp.com/api/guilds/695809740428673034/widget.png?style=banner2">](https://discord.gg/cQ4gWxN)

# Arduino-Source-LGPE

This is a fork I made to get some tweaks/bug fixes in for LGPE and some quality of life upgrades for Serial Programs. If these progress I'll look at submitting them to the main project.

### Changes:
- LGPE Daily Item Farming [BETA]: date manip is fairly broken for my setup at default. I implemented checks on the sync via internet and a loop to keep the script on track. It's not perfect yet, lots of misclicks especially at high loop counts.
- LGPE Rare Candy [ALPHA]: This is simple but could be dangerous, use with caution. Once you have a rare candy selected and are over the pokemon enter the number of levels you want to upgrade and it'll use that many rare candies. Do not do this if you want to keep a pokemon unevolved or learn specific moves (it's dumb right now so it just wastes button clicks on those and can't stop evolutions yet).
- Rotation of video input. I've noticed some video cards come in rotated. I've implemented a rotation option for those cards to right themselves.
### Known Bugs:
- LGPE Daily Item Farming: still misclicks occasionally in the menus BUT it mostly get's back on track with the changes made. I've noticed my Switch 2 has frozen on my lower spec'd computer a few times. I can't recreate it reliably yet. Switch Oled seems to be chugging along well.
- LGPE Rare Candy: Can't overstate how dumb this is: it simply presses A 3 times in rapid success for the number of times you select. If the pokemon evolves/learns a move, those will eat additional loops. Only do this for max evolution pokemon and be aware that it'll learn new moves and overwrite the old ones. If you are aware of that, it saves a lot of time when leveling with rare candies. 


# Licensing:
- Unless otherwise specified, all source code in this repository is under the MIT license.
- Some files may be under other (compatible) licenses.
- All precompiled binaries and object files are free for non-commercial use only. For all other uses, please contact the Pokémon Automation server admins.

# Dependencies:

| **Dependency** | **License** |
| --- | --- |
| Qt5 and Qt6 | LGPLv3 |
| [QDarkStyleSheet](https://github.com/ColinDuquesnoy/QDarkStyleSheet) | MIT |
| [Qt Wav Reader](https://code.qt.io/cgit/qt/qtmultimedia.git/tree/examples/multimedia/spectrum/app/wavfile.cpp?h=5.15) | BSD |
| [nlohmann json](https://github.com/nlohmann/json) | MIT |
| [D++](https://github.com/brainboxdotcc/DPP) | Apache 2.0 |
| [LUFA](https://github.com/abcminiuser/lufa) | MIT |
| [Tesseract](https://github.com/tesseract-ocr/tesseract) | Apache 2.0 |
| [Tesseract for Windows](https://github.com/peirick/Tesseract-OCR_for_Windows) | Apache 2.0 |
| [OpenCV](https://github.com/opencv/opencv) | Apache 2.0 |
| [ONNX](https://github.com/microsoft/onnxruntime) | MIT |

Vanilla GPL is disallowed, though LGPL is allowed. This is for the following reasons:
1. A tiny portion of the project is not open-sourced. (mostly related to telemetry and internal research experiments)
2. We reserve right to re-license the project in ways that do not abide by the copy-left requirement of GPL.

