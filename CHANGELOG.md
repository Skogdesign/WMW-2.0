# Changelog

All notable changes to **WoW Model Viewer: Midnight** are recorded here.
Format loosely based on [Keep a Changelog](https://keepachangelog.com/).

## [Unreleased] — targeting 0.2.0

_Changes for the next release are collected here as they are made._

### Added

### Changed

### Fixed


## [0.1.5] — 2026-06-15

First public release: the classic WoW Model Viewer (0.10.x) modernized for current
retail World of Warcraft (patch 12.x) and rebranded as **Midnight**.

### Added
- Support for **current retail WoW (12.x)** — modern WDC5 database format with
  DBD-driven schemas.
- **Armory character import** — load a character's race, appearance and equipment from
  a Battle.net Armory link, via a self-hosted proxy (no per-user credentials).
- **Windows installer** (per-user, no admin) with Start-menu/desktop shortcuts and an
  uninstaller.

### Changed
- Character customization: skin, faces, hair, geosets, equipment, and colour swatches.
- M2 multi-texture **combiner shaders** so layered materials (cosmic capes, glowing
  orbs) render correctly instead of solid white.
- **Randomise** is dramatically faster — applies all options then refreshes once
  (previously one full refresh per option).
- Rebranded to **WoW Model Viewer: Midnight** (name, application icon, splash).
- Removed the built-in auto-updater.

### Fixed
- **Dracthyr** (and other newer/allied races) customization — empty option lists and a
  scrambled skin caused by an over-aggressive per-choice race filter.
- **Creature particle colours** (Fyrakk and similar) under the modern ParticleColor schema.
- Blank/missing character faces; customization crashes across several races.
- A model-switch **memory leak** and out-of-range bone/light/texture-lookup reads in the
  per-frame animation/render paths.
- Blank **application / taskbar icon**.
