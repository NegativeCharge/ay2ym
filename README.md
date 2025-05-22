# AY2YM Converter

## Overview

**AY2YM** is a command-line tool that converts AY music files (commonly used for ZX Spectrum, Amstrad CPC, and similar systems) into the YM6 format, which is widely supported by modern music players and emulators. The tool emulates a Z80 CPU and AY-3-8910/AY-3-8912 sound chip to accurately render the music data, then outputs an interleaved YM6 file suitable for playback.

## Features

- Emulates Z80 CPU and AY-3-8910/AY-3-8912 sound chip logic
- Supports parsing and conversion of AY files with multiple songs
- **New:** Supports Amstrad CPC AY port mapping and playback logic
- Outputs YM6 files with proper metadata, interleaved register data, and trailing silence trimming
- Handles file and song name sanitization for safe output filenames

## Usage

ay2ym.exe input_file.ay

- The tool will generate a `.ym` file for each song found in the input AY file.
- Output files are named using the pattern:  
  `[input-filename] - [song-name].ym`

## Build Instructions

1. Open the solution in Visual Studio 2022.
2. Ensure you have all source files (`ay2ym.cpp`, `ay2ym.h`, `z80emu.h`, `z80user.h`, and their implementations).
3. Build the project using the default configuration.

## Dependencies

- Standard C++14 library
- No external dependencies required

## File Structure

- `ay2ym.cpp` — Main logic for file parsing, emulation, and YM file writing
- `ay2ym.h` — AY2YM context and function declarations
- `z80emu.h`, `z80user.h` — Z80 CPU emulation headers

## Notes

- The tool is designed for batch conversion and may not handle all edge cases of malformed AY files (in particular, no handling of beeper tunes).
- Output files are sanitized to avoid invalid filename characters on Windows.

## Example of Batch Conversion on Windows

```batch
@echo off
chcp 65001 >nul
cls
for /f "tokens=* delims=" %%a in ('dir "tracks\*.ay" /s /b') do (
	echo "Converting %%a..."
	ay2ym.exe "%%a"
)

echo "Conversion complete."
pause
```

This batch script will convert all AY files in the `tracks` directory and its subdirectories, creating corresponding YM files in the same location.

## Version Change Log

### v1.1.0 (2025-05-22)
- **Added support for Amstrad CPC AY port mapping and playback.**
- Improved filename sanitization for output files.
- Minor bug fixes and code cleanup.

### v1.0.0
- Initial release: ZX Spectrum AY to YM6 conversion, multi-song support, metadata output, and batch conversion.

## License

This project is licensed the same as the original Z80EMU project. This code is free, do whatever you want with it.

## Credits

- Z80EMU Z80 emulation - Copyright (c) 2012-2017 Lin Ke-Fong
- AY/YM format logic based on open specifications and community resources.
- Conversion tool by Negative Charge (@negativecharge.bsky.social)