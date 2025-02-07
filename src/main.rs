use std::{fs::File, path::PathBuf};

use clap::{error::ErrorKind, CommandFactory, Parser, ValueEnum};
use which::which;

// TODO: Support multiple inputs and outputs,
// add field for custom Binkc
#[derive(Parser)]
#[command(version, about)]
struct Cli {
    #[arg(short, long = "input")]
    /// file to convert
    infile: PathBuf,

    /// target game
    #[arg(short, long, value_enum)]
    format: GameFormat,

    /// automatically overwrite output file
    #[arg(short = 'y', long, default_value_t = false)]
    overwrite: bool,

    #[arg(short, long)]
    radvideo_path: Option<PathBuf>,

    /// file to output
    outfile: Option<PathBuf>,
}

#[derive(Copy, Clone, PartialEq, Eq, PartialOrd, Ord, ValueEnum)]
enum GameFormat {
    /// Scooby Doo! Night of 100 Frights
    N100F,
    /// SpongeBob SquarePants: Battle for Bikini Bottom
    Battle,
    /// The SpongeBob SquarePants Movie
    Movie,
    /// The Incredibles
    Incredibles,
    /// The Incredibles: Rise of the Underminer
    ROTU,
}

impl GameFormat {
    fn _get_resolution(&self) -> (i32, i32) {
        match *self {
            Self::N100F | Self::Battle => (640, 480),
            Self::Movie | Self::ROTU => (512, 480),
            Self::Incredibles => (512, 448),
        }
    }
}

fn main() {
    let args = Cli::parse();

    let _rad_path = &args.radvideo_path.unwrap_or_else(|| {
        which("radvideo64.exe")
            .unwrap_or_else(|_| {
                let mut cmd = Cli::command();
                cmd.error(
                    ErrorKind::MissingRequiredArgument,
                    "RADVideo doesn't appear to be installed, or it's not located in %PATH%\n \
                please download RADVideo and configure your %PATH% to the RADVideo folder, \
                or set the -r argument to the path of the RADVideo folder.",
                )
                .exit();
            })
            .parent()
            .unwrap()
            .to_path_buf()
    });
    which(_rad_path.join("radvideo64.exe")).unwrap_or_else(|_| {
        let mut cmd = Cli::command();
        cmd.error(
            ErrorKind::InvalidValue,
            format!(
                "RADVideo doesn't appear to be installed in the provided directory {}\n\
            Please ensure the provided directory contains the radvideo64 executable, or clear the \
            -r argument to use your %PATH%, if configured correctly.",
                &_rad_path.display()
            ),
        )
        .exit();
    });

    let _infile = File::open(&args.infile).unwrap_or_else(|_| {
        let mut cmd = Cli::command();
        cmd.error(
            ErrorKind::Io,
            format!("INFILE {} does not exist", args.infile.display()),
        )
        .exit()
    });

    // TODO: prompt to overwrite if flag isn't set
    let _outfile = &args.outfile.unwrap_or(args.infile.with_extension("bik"));
}
