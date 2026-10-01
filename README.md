<div align="center">

# 📡 Signal Pet for Flipper Zero

**A virtual pet that lives on the radio signals around you.**
Hunt Sub-GHz remotes, NFC cards, 125 kHz tags, infrared remotes and
iButton keys with your Flipper, feed them to your pet and watch it level
up, evolve and learn new moves.

![Platform](https://img.shields.io/badge/platform-Flipper%20Zero-orange)
![Language](https://img.shields.io/badge/language-C-blue)
![Firmware](https://img.shields.io/badge/firmware-official%201.x-lightgrey)
![License](https://img.shields.io/badge/license-MIT-green)
[![Build](https://github.com/King-Kong-341/Flipper-Zero-Games_Signal_Pet/actions/workflows/build.yml/badge.svg)](https://github.com/King-Kong-341/Flipper-Zero-Games_Signal_Pet/actions/workflows/build.yml)

<img src="images/levels.gif" width="49%"> <img src="images/intro.gif" width="49%">

</div>

---

## 📁 What's in here?

| Folder | What it is | Who needs it |
|---|---|---|
| 📦 **[`Signal Pet/`](Signal%20Pet/)** | the finished app: **`signal_pet.fap`** | everyone — this is the file for your Flipper |
| 🧩 [`source/`](source/) | the C source code | only if you want to build or change the app |
| 🖼 [`images/`](images/) | the pictures on this page | — |

## 🎯 What is this?

A collecting game with a pet. You start with an egg — warm it up and it
hatches. Your pet eats **radio signals**: take your Flipper on a hunt, press a
car key or a doorbell, hold a bank card or a door fob to the back, point a TV
remote at it. Every new signal fills the **Signal Dex**, lands in the
**Logbook** and gives XP. The pet grows from a tiny *Bitling* into one of
**6 adult forms** — which one depends on what it ate most. On the way it
unlocks **10 new moves** and new looks, up to level 99.

No hunger, no health bars — your pet can't die. Just hunt, collect and level up.

## 📥 Installation

1. Open the folder **[`Signal Pet`](Signal%20Pet/)** and click
   **`signal_pet.fap`** → **Download** (the ⬇ button on the right).
   It's also on the [Releases page](https://github.com/King-Kong-341/Flipper-Zero-Games_Signal_Pet/releases).
2. Open [qFlipper](https://flipperzero.one/update) on your computer and
   connect your Flipper via USB.
3. In qFlipper's **File Manager**, copy the file to **`SD Card/apps/Games/`**.
4. On the Flipper: **Menu → Apps → Games → Signal Pet**. Have fun!

> Made for the **official Flipper firmware 1.x**. If the app doesn't start
> on your firmware, build it yourself (see the end of this page).

## 🎮 How to play

| Key | In the room | In menus |
|---|---|---|
| ◀ ▶ | pick an icon in the bottom bar | move / change a value |
| **OK** | open it | select |
| ▲ | pet your pet | move |
| ▼ | your pet tells you something | move |
| **Back** | say goodbye and exit | go back |
| hold **Back** | quit from anywhere | |

The bottom bar: **Hunt · Play · Clean · Sleep · Signal Dex · Logbook · Stats · Settings**

### 📡 Hunting — what your pet eats

| Source | Try this | Where on the Flipper |
|---|---|---|
| **Sub-GHz** | car keys, garage gates, doorbells, remotes | listens on **all** Sub-GHz bands (300–928 MHz) |
| **NFC** | bank, transit, hotel and access cards, phones | hold it to the **back** |
| **RFID 125 kHz** | door fobs, office badges, pet microchips | hold it to the **back** |
| **Infrared** | any TV or AC remote | aim at the **top** and press a button |
| **iButton** | intercom keys (Dallas, Cyfral, Metakom) | touch the iButton contacts |

- **New species** (a signal type you never had) gives the most XP, a **new
  signal** of a known type gives a bit less. The very same signal again
  within 20 minutes is "old news".
- **Sub-GHz "All" band:** the radio has one receiver, so it sniffs the signal
  strength of all 54 standard frequencies in a fast loop (about 0.2 s for
  all of them) and jumps onto any channel that lights up to decode it.
  Tip: hold the button of your remote for 1–2 seconds. You can also lock
  onto 433, 868 or 315 MHz with ◀ ▶.
- **External CC1101 board** (e.g. the Rabbit-Labs *Flux Capacitor*) on the
  GPIO header? Signal Pet finds it automatically ("Board: CC1101") and uses
  it for Sub-GHz hunts — the first time gives a gadget bonus.

### 🌱 Growing

- XP comes from new signals, the mini games, cleaning and sleeping.
- **Level 5:** teen. **Level 10:** adult — *Wavern* (Sub-GHz), *Tapkin* (NFC),
  *Coilbit* (RFID), *Irix* (IR), *Keybo* (iButton) or *Omnix* (a balanced diet).
- **New moves** at levels 3, 5, 8, 10, 15, 20, 30, 45, 70 and 99: Radar Ping,
  Dance, Spin, Juggle, Moonwalk, Backflip, Power Up, Levitate, Teleport and
  Legend Glow. High levels also bring orbiting signal orbs, an aura, a halo,
  wings, a sparkle trail, shockwave landings and lightning.
- **Clean:** digested signals leave static on the floor. With a lot of
  static around, catches give only half XP — sweep it up for XP.
- **Sleep:** put your pet to bed; when it wakes up it gets dream XP.

## ✨ Features

- 🐣 **Egg hatching**, naming with your own **keyboard** (or a random name)
- 🎬 **Animations everywhere:** CRT-style boot with a logo built from flying
  pixels, catch cinematic (lock-on, the signal flies into the mouth, chomp,
  result card), level-up and evolution scenes, three screen transitions
- 🌗 **Day & night** from the real clock: sun and clouds, moon and stars,
  lights off when your pet sleeps
- 📖 **Signal Dex** with every signal type your firmware can decode, **Logbook** of the last 40
  catches with time and ID, **16 badges**, stats
- 🎮 **2 mini games:** *Byte Catch* (catch falling packets, dodge static)
  and *Tune In* (match your wave to the target on an oscilloscope)
- 🔊 **Sound, vibration & LED** — every source has its own LED colour
- ⚙️ **Settings** with a one-line explanation for every option
- 💾 Everything is saved on the SD card

## 🖼 Screens

| | | |
|:-:|:-:|:-:|
| <img src="images/home.png"> | <img src="images/hunt_subghz.png"> | <img src="images/catch_card.png"> |
| The room | Sub-GHz hunt (all bands) | A new species! |
| <img src="images/hunt_nfc.png"> | <img src="images/logbook.png"> | <img src="images/signal_dex.png"> |
| NFC hunt | Logbook | Signal Dex |
| <img src="images/profile.png"> | <img src="images/name_keyboard.png"> | <img src="images/hatch.png"> |
| A level 99 legend | Name keyboard | Hatching the egg |
| <img src="images/games.png"> | <img src="images/tune_in.png"> | <img src="images/gpio_board.png"> |
| Mini games | Tune In | External board found |

## 🛠 Build it yourself

Only needed if the ready-made file doesn't work on your firmware or you want
to change something. You need [Python 3](https://www.python.org/downloads/):

```bash
pip install ufbt
cd source
python -m ufbt            # -> source/dist/signal_pet.fap
python -m ufbt launch     # or: build, install and start it on a connected Flipper
```

Signal Pet only **receives** — it never transmits anything.

---

Made by **King-Kong-341**. MIT licensed, see [LICENSE](LICENSE).
