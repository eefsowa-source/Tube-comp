# macOS AU/VST3 packaging

The packaging script builds the current `EON-Vari mu DSP` AU and VST3,
copies both bundles into a temporary staging directory, signs and verifies the
staged copies, installs them into the current user's plugin folders, and creates
a timestamped ZIP plus SHA-256 checksum under `Packaging/dist/`.

Each ZIP contains a double-click installer (`Install EON-Vari mu DSP.command`)
and `README.txt` with a manual-install fallback. The installer replaces only
the matching EON-Vari mu DSP AU and VST3 bundles in the current user's Audio
Plug-Ins folders.

## Local prototype package

```sh
./scripts/package_macos.sh
```

The default uses ad-hoc signing. This is suitable for local DAW testing but is
not a public release signature and cannot replace Apple notarization.

Use existing build artifacts without rebuilding:

```sh
./scripts/package_macos.sh --skip-build
```

Add `--skip-install` to create a package without touching the user's Audio
Plug-Ins directories. Add `--auval` to run Apple's AU validation after install.

## Developer ID release candidate

The signing certificate must already be present in the login keychain:

```sh
SIGN_IDENTITY="Developer ID Application: Company Name (TEAMID)" \
  ./scripts/package_macos.sh --sign developer-id
```

The script fails rather than falling back to ad-hoc signing when the requested
identity is unavailable. A Developer ID signature alone is not a notarized
release; submit the resulting distribution to Apple's notary service and staple
the notarization ticket as a separate release step.

The script only replaces the two explicitly named user plugin destinations
during installation. Stale bundles with older product names are left untouched.
