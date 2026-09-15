# Local ZIP installer design

## Goal

Package `EON-Vari mu DSP` for personal use on an Apple Silicon MacBook Air.
The distribution ZIP must provide a double-click installer and concise manual
installation instructions without requiring a Developer ID certificate.

## Package layout

The ZIP root contains:

- `Install EON-Vari mu DSP.command`
- `README.txt`
- `AU/EON-Vari mu DSP.component`
- `VST3/EON-Vari mu DSP.vst3`

## Installer behavior

The command file locates its own directory, verifies both bundled plug-in
bundles, and installs only these explicit destinations:

- `~/Library/Audio/Plug-Ins/Components/EON-Vari mu DSP.component`
- `~/Library/Audio/Plug-Ins/VST3/EON-Vari mu DSP.vst3`

It replaces those same-name destinations without prompting. It does not remove
any differently named plug-in bundle. The script uses `ditto` to preserve macOS
bundle metadata, then runs strict code-signature verification and prints the
two installed paths.

## README

`README.txt` explains double-click installation, the two manual copy paths,
how to allow the first launch in macOS Privacy & Security if necessary, and
that this current build is arm64-only for Apple Silicon Macs.

## Error handling and validation

The installer exits without copying when either source bundle, Info.plist, or
bundle executable is missing. It stops on a failed copy or failed code-signature
verification. Packaging verification checks that both installer and README are
present in the output archive, and an isolated home-directory dry run confirms
the installer creates the two expected destinations.
