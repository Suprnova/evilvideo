use std::{fs::File, path::PathBuf};

use clap::{error::ErrorKind, CommandFactory, Parser, ValueEnum};
use which::which;

// TODO: Support multiple inputs and outputs,
// add field for custom Binkconv args,
// add field for custom executable name
/// Represents the launch arguments for the application.
/// Derives from clap::Parser.
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

    /// path to RADVideo folder
    #[arg(short, long)]
    radvideo_path: Option<PathBuf>,

    /// file to output
    outfile: Option<PathBuf>,
}

/// Represents the game to convert a video for
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
    /// Returns a set of width, height variables that correspond to
    /// the appropriate resolution for a video in the appropriate game.
    fn _get_resolution(&self) -> (i32, i32) {
        match *self {
            Self::N100F | Self::Battle => (640, 480),
            Self::Movie | Self::ROTU => (512, 480),
            Self::Incredibles => (512, 448),
        }
    }
}

/// Performs all launch argument parsing to ensure the application's state
/// is valid, then returns parsed arguments in the following order and specifications:
///
/// * infile: File
/// * game_format: GameFormat
/// * overwrite_outfile: bool
/// * path_to_radvideo_folder: PathBuf
/// * path_to_outfile: PathBuf
fn precheck_args(args: Cli) -> (File, GameFormat, bool, PathBuf, PathBuf) {
    // first check if rad_path is provided
    let rad_path = args.radvideo_path.unwrap_or_else(|| {
        // if not, try to find radvideo64.exe in %PATH%
        which("radvideo64.exe")
            .unwrap_or_else(|_| {
                // no binary found, throw error
                let mut cmd = Cli::command();
                cmd.error(
                    ErrorKind::MissingRequiredArgument,
                    "RADVideo doesn't appear to be installed, or it's not located in %PATH%\n \
                please download RADVideo and configure your %PATH% to the RADVideo folder, \
                or set the -r argument to the path of the RADVideo folder.",
                )
                .exit();
            })
            // binary found, set _rad_path to its parent
            .parent()
            // this *should* be safe because which always returns a path to the executable,
            // which must have a parent directory since it's a file
            .unwrap()
            .to_path_buf()
    });

    // rad_path was provided (or created from path), ensure radvideo64.exe exists in it
    which(rad_path.join("radvideo64.exe")).unwrap_or_else(|_| {
        let mut cmd = Cli::command();
        cmd.error(
            ErrorKind::InvalidValue,
            format!(
                "RADVideo doesn't appear to be installed in the provided directory {}\n\
            Please ensure the provided directory contains the radvideo64 executable, or clear the \
            -r argument to use your %PATH%, if configured correctly.",
                &rad_path.display()
            ),
        )
        .exit();
    });

    let infile = File::open(&args.infile).unwrap_or_else(|_| {
        let mut cmd = Cli::command();
        cmd.error(
            ErrorKind::Io,
            format!("INFILE {} does not exist", args.infile.display()),
        )
        .exit()
    });

    // TODO: prompt to overwrite if flag isn't set
    let outfile = args.outfile.unwrap_or(args.infile.with_extension("bik"));
    (infile, args.format, args.overwrite, rad_path, outfile)
}

fn main() {
    let args = Cli::parse();

    let (_infile, _format, _overwrite, _rad_path, _outfile) = precheck_args(args);
}
