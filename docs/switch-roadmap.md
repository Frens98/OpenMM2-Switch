# Switch roadmap

The port runs as an NRO and reads game archives supplied by the player. The
items below are the next steps toward a release that is ready for a homebrew
catalog.

## Reliability and performance

- Graphics quality and toggle changes now save as they are made.
- `+` opens the pause menu during a race and the quit confirmation in menus;
  `-` is an alternate pause-menu binding in a race.
- Measure frame time and audio underruns on real hardware before tuning the
  render scale, audio buffer size, or frame cap. The current Switch defaults
  favor performance, but the reported low frame rate and audio stutter still
  need an on-console pass.

## Switch-specific features

| Feature | Feasibility | Next implementation step |
| --- | --- | --- |
| Touch menus | High | Read libnx touch points in handheld mode and translate them to the existing UI pointer input. Add a visible touch target for menu navigation first. |
| Motion steering | High | Read the Joy-Con/Pro Controller six-axis sensor through libnx, then add an opt-in steering mode with calibration, sensitivity and drift filtering. The current SDL Switch joystick patch does not expose sensors yet. |
| Rumble | High | Add optional libnx vibration support and map engine/race feedback to conservative rumble levels. |
| Mod manager | High | Add a local list that enables or disables `.AR` archives. OpenMM2 already discovers `.AR` archives in the selected game directory, so manually installed add-on archives work without a downloader. |
| Mod downloader | Medium | After the local manager is stable, add an opt-in catalog of links to community-hosted mods. Show the source and files before installing; never bundle or redistribute retail game data. |

## Distribution readiness

- Keep the public repository and release packages free of Midtown Madness 2
  game files. Players provide their own game installation.
- Publish a tagged NRO homebrew release after on-console performance, settings
  persistence, and exit controls have been checked.
- Submit to Sphaira's Homebrew App Store only after that release is stable.
  Quiver currently distributes PC ports through its PC launcher; it is not a
  Switch NRO updater. A Quiver listing can be considered later as a showcase
  only if its maintainers accept Switch homebrew entries.
