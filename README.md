# evilvideo - a better bik converter

evilvideo is a tool for modders of Heavy Iron Studios' EvilEngine games who wish to include custom videos in their mods. Target a particular game and evilvideo takes care of the resolution, frame rate, audio format, and Bink settings that game needs.

It prepares the video with a bundled [FFmpeg](https://ffmpeg.org) and compresses it with RAD Game Tools' own Bink compressor, which you install separately. It runs on Windows, and on Linux through Wine.

This release is a command-line tool. A graphical version is planned.

## Supported games

| ID | Game | Output |
|---|---|---|
| `n100f` | Scooby-Doo! Night of 100 Frights | 640×480 |
| `bfbb` | SpongeBob SquarePants: Battle for Bikini Bottom | 640×480 |
| `tssm` | The SpongeBob SquarePants Movie | 512×480 |
| `incredibles` | The Incredibles | 512×480 |
| `rotu` | The Incredibles: Rise of the Underminer | 512×480 |

Every video is converted to 29.97 fps with 48 kHz stereo audio.

## Installing

1. Download and install RAD Video Tools from [RAD's Bink download page](https://www.radgametools.com/bnkdown.htm). They are free for non-commercial use, and evilvideo cannot include them.
2. Download the latest `evilvideo-<version>-windows-x64.zip` from [Releases](https://github.com/Suprnova/evilvideo/releases) and extract it anywhere. Keep `ffmpeg.exe` next to `evilvideo-cli.exe`.

evilvideo finds RAD Video Tools in their standard install folder. If you installed them elsewhere, pass `--rad <path to radvideo64.exe>`.

## Usage

```
evilvideo-cli [options] <input>...

  -g, --game <id>        target game: n100f, bfbb, tssm, incredibles, rotu (required)
  -o, --output <path>    output file (one input) or folder (several inputs);
                         default: next to each input, with a .bik extension
      --stretch          stretch to fill the frame instead of letterboxing
      --trim <from>-<to> keep only this range, in seconds (e.g. 1.5-12)
  -y, --overwrite        replace existing outputs instead of skipping them
      --rad <path>       radvideo64.exe to use (default: saved setting, then the standard install)
      --show-rad         show the RAD Video Tools windows instead of hiding them
  -v, --verbose          also print what each step runs and what the tools report
  -q, --quiet            print only errors and the summary
  -h, --help             show this help
      --version          show the version
```

Examples:

```
evilvideo-cli --game bfbb intro.mp4
evilvideo-cli --game tssm --trim 5-35 --output mod\fmv\ clips\*.mkv
evilvideo-cli -g rotu --stretch -o logo.bik "my logo.mov"
```

- Videos that are not 4:3 are letterboxed with black bars to keep their shape. `--stretch` fills the screen instead.
- Inputs may use `*` and `?` wildcards. Each output keeps its input's name with a `.bik` extension.
- Existing outputs are skipped unless `--overwrite` is given.

If a conversion fails and the reason is unclear, run it again with `--verbose` and include that output in a [new issue](https://github.com/Suprnova/evilvideo/issues/new).

## Linux

evilvideo runs under Wine, as RAD Video Tools do.

1. Install Wine from your distribution.
2. Install RAD Video Tools into Wine: `wine radtools.exe` (the installer from RAD's download page).
3. Run evilvideo with Wine:

   ```
   WINEDEBUG=-all wine evilvideo-cli.exe --game bfbb intro.mp4
   ```

`WINEDEBUG=-all` is optional, but hides Wine's own diagnostic messages. Use relative paths, or Wine's `Z:` drive for absolute ones (`Z:/home/me/intro.mp4`). Unix shells expand wildcards before evilvideo sees them, so quote a pattern only if you want evilvideo to expand it.

In the event that you use a custom `WINEPREFIX`, ensure you use the same one that you installed `radtools.exe` with.

If the compression step is significantly slower than expected, limiting Wine to a single CPU may yield faster results.

```
WINEDEBUG=-all taskset -c 0 wine evilvideo-cli.exe --game bfbb intro.mp4
```

## macOS

evilvideo should work with a macOS build of Wine the same way as on Linux, but it has not been tested on a Mac yet. [Reports](https://github.com/Suprnova/evilvideo/issues/new) are welcome.

## License

evilvideo is licensed under the Do What The F*ck You Want To Public License (WTFPL). License terms can be found in [LICENSE](LICENSE).

This software uses code of [FFmpeg](https://ffmpeg.org) licensed under the [LGPLv3](https://www.gnu.org/licenses/lgpl-3.0.html). Each release names the exact FFmpeg source it was built from in `licenses/ffmpeg-build.txt`, with the configure line and the script that built it, [`ffmpeg/build.sh`](ffmpeg/build.sh). The bundled FFmpeg includes [dav1d](https://code.videolan.org/videolan/dav1d), licensed under the BSD 2-Clause license.
