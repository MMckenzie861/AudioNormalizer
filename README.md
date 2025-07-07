# AudioNormalizer
C++ Program to normalize all audio files in a given directory to the highest level 

## Features
- Supports 8, 16, 24, and 32-bit PCM .wav files

- Detects the peak amplitude of each file

- Applies gain to match the loudest file

- Writes new normalized files to the same directory

## Requirements
C++17 or later

## Usage
./AudioNormalizer <directory_path>

## Output Example

```bash
Loudest File: Alesis-Fusion-Acoustic-Bass-C2.wav
Peak Amplitude: 0.998962
Normalizing "Casio-MT-45-Disco.wav" -> "normalized_Casio-MT-45-Disco.wav" (gain: 1.00012)
Normalizing "Crash-Cymbal-3.wav" -> "normalized_Crash-Cymbal-3.wav" (gain: 1.52103)
```
