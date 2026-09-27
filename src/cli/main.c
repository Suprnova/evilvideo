#include <stdio.h>

static const char help[] =
    "Usage: evilvideo-cli [options] <input>...\n"
    "\n"
    "Converts videos to Bink for Heavy Iron Studios' EvilEngine games.\n"
    "\n"
    "  -g, --game <id>        target game: bfbb, tssm, n100f, incredibles, rotu (required)\n"
    "  -o, --output <path>    output file (one input) or folder (several inputs);\n"
    "                         default: next to each input, with a .bik extension\n"
    "      --stretch          stretch to fill the frame instead of letterboxing\n"
    "      --trim <from>-<to> keep only this range, in seconds (e.g. 1.5-12)\n"
    "  -y, --overwrite        replace existing outputs instead of skipping them\n"
    "      --rad <path>       radvideo64.exe to use (default: saved setting, then the standard install)\n"
    "  -q, --quiet            print only errors and the summary\n"
    "  -h, --help             show this help\n"
    "      --version          show the version\n";

int main(void)
{
    fputs(help, stdout);
    return 0;
}
