use std::{fs::File, path::PathBuf};

use clap::{error::ErrorKind, CommandFactory, Parser, ValueEnum};

#[derive(Parser)]
#[command(version, about)]
struct Cli {
    #[arg(short, long = "input")]
    /// file to convert
    infile: std::path::PathBuf,

    /// target game
    #[arg(short, long, value_enum)]
    format: GameFormat,

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
    ROTU
}

fn main() {
    let args = Cli::parse();
    let _infile = File::open(&args.infile)
        .unwrap_or_else(|_| {
            let mut cmd = Cli::command();
            cmd.error(
                ErrorKind::Io,
                format!("INFILE {} does not exist", args.infile.display())
            )
            .exit()
    });

}
