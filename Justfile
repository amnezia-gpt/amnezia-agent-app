set dotenv-load := false

mod quality "just/quality.just"
mod smoke "just/smoke.just"

default:
    @just --list
