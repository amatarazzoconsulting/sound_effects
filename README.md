

(c) 2026 Anthony Matarazzo

# Sound Effects Engine

A C++17 procedural sound engine for application cues, arcade games, and other interactive media. It provides 84 named presets, caller-defined sounds, a mixer for up to 128 simultaneous voices, stereo positioning, and optional effects. The engine renders 48 kHz floating-point audio into buffers; the included command-line program can export raw PCM and WAV files.

## Contents

- `engine.cpp` — engine, sound library, mixer, effects, and command-line program
- `synthesis.h` — layer parameters and synthesizer state; included by `engine.cpp`
- `presets.h` — built-in preset recipes; included by `engine.cpp`
- `tests/` — engine, synthesis, and CLI regression tests
- `Makefile` — build and test commands in the full repository

Keep the three engine files together. The source-only ZIP contains the engine and test files; obtain `Makefile` and `LICENSE` from the full repository when needed.

## Requirements

- A C++17 compiler. Linux examples use GCC, Make, and Python 3 (for CLI tests).
- Windows example: Windows 10 or later, Visual Studio C++ build tools, and the Windows SDK containing XAudio2. The example links `xaudio2.lib` and `ole32.lib`.
- No audio device is required for WAV or raw PCM export. Windows playback requires a working audio output device.

## Build and test

From the full repository on Linux:

```sh
make
make test
```

With only the engine source files on Linux:

```sh
g++ -std=c++17 -O2 -pthread engine.cpp -o sound_effects
```

From a Visual Studio Developer Command Prompt, in the source directory:

```bat
cl /nologo /std:c++17 /EHsc /O2 engine.cpp /Fe:sound_effects.exe
```

## Command-line usage

Linux examples below use `./sound_effects`; on Windows use `sound_effects.exe`.

```sh
# List all names and categories, or list the UI category.
./sound_effects --list
./sound_effects --list --category ui

# Render a single sound and the complete library.
./sound_effects --wav-output click.wav --sound ui_light_click --seconds 1
./sound_effects --wav-output arcade.wav --category arcade --seconds .001
./sound_effects --wav-output all-sounds.wav --all --seconds .001

# Raw PCM and WAV can be written together. Effects are opt-in.
./sound_effects --output laser.pcm --wav-output laser.wav --sound "high laser left" --seconds 1 --effects
```

`--output` writes headerless, interleaved stereo, 48 kHz, signed 16-bit little-endian PCM. `--wav-output` writes the same sample format in a WAV container. Output files are overwritten. Use different paths when selecting both outputs. A WAV-only request creates no PCM file. Invalid arguments and file errors return a nonzero exit code.

With no arguments the program writes a ten-sound demonstration to `demo.pcm`, using three seconds per sound. `--seconds` accepts 0.001–600 seconds: for a single sound it controls the output length, so a short value can cut off its release. For `--all` or `--category`, it is a minimum per-sound duration; each slot also includes the preset duration, release, and 0.5 seconds. The program prints the start time of each slot. `--all`/`--category` and `--sound` are mutually exclusive. WAV output is limited by the 32-bit RIFF size limit.

Built-in categories include `ui`, `arcade`, `scifi`, `fantasy`, `impact`, `foley`, `creature`, `environment`, `cartoon`, and `percussion`. Use `--list` as the source of truth for preset names. Natural descriptions support `high`, `low`, `fast`, `slow`, `left`, and `right`; fast/slow currently shift pitch. Other adjectives are not general natural-language controls. The CLI renders dry by default; `--effects` enables reverb and stereo delay.

## Use the engine in C++

For the current source layout, define `SOUND_ENGINE_NO_MAIN` and include `engine.cpp` in **one** translation unit. Keep `synthesis.h` and `presets.h` alongside it. Do not also compile `engine.cpp` separately into the same program. This is a source integration example, not a separately packaged library or stable ABI.

```cpp
#define SOUND_ENGINE_NO_MAIN
#include "engine.cpp"
#include <vector>

int main() {
    SoundEngine engine;
    engine.setMasterVolume(0.7);
    engine.play("ui_light_click", 0.8); // volume 0..1
    engine.play("arcade_shoot", 0.5, -0.5); // pan: -1 left, +1 right
    engine.playNatural("high laser right", 0.5);

    constexpr int frames = SAMPLE_RATE / 2; // 0.5 seconds at 48 kHz
    std::vector<float> stereo(frames * 2);
    engine.getAudioBuffer(stereo.data(), frames, 2);
    // Pass the interleaved float frames to your audio backend.
}
```

`play` schedules a voice; `getAudioBuffer` advances synthesis. Its `numSamples` argument is a **frame** count, with one or two output channels. Stereo output has `2 * frames` floats ordered left, right. Samples are clamped to [-1, 1] after the master volume. Unknown names passed to `play` are ignored; use `--list` or `SoundLibrary::findSound` to validate names. Layered voices can clip when mixed; lower voice and master gains when combining sounds.

The engine does not open an audio device, manage a real-time stream, resample to other rates, or guarantee hard real-time behavior. Rendering and control share a mutex and voice creation allocates memory. Do not call control methods from a hard real-time audio callback; benchmark on target hardware.

## Create a new sound effect

A `SoundDefinition` can combine up to eight `SynthLayer` objects. A layer controls waveform, pitch sweep, filters, attack/decay, optional FM, and optional repeating notes. Times are in milliseconds, frequencies in Hz, pitch offsets in semitones, and gains are linear. The outer envelope shapes the whole sound; each layer has its own envelope and delay. Validation rejects invalid parameters with `std::invalid_argument`.

Save the following as `custom_sound.cpp` next to the engine source files:

```cpp
#define SOUND_ENGINE_NO_MAIN
#include "engine.cpp"
#include <iostream>
#include <vector>

int main() {
    SoundDefinition shot;
    shot.name = "custom_shot";
    shot.base_freq = 600;       // Hz
    shot.amplitude = 0.3;      // 0..1 before the master volume
    shot.duration_ms = 180;
    shot.envelope = {1, 10, 0.8, 40}; // attack, decay, sustain, release

    SynthLayer pulse;
    pulse.wave = Waveform::SQUARE;
    pulse.gain = 0.7;
    pulse.duty = 0.25;
    pulse.pitch_start = 12;    // semitones relative to base_freq
    pulse.pitch_end = -24;
    pulse.sweep_ms = 150;
    pulse.decay_ms = 65;
    pulse.cutoff_start = 6000; // Hz
    pulse.cutoff_end = 1000;
    pulse.filter_ms = 150;

    SynthLayer impact;
    impact.wave = Waveform::NOISE;
    impact.gain = 0.2;
    impact.decay_ms = 20;
    impact.cutoff_start = impact.cutoff_end = 1800;
    shot.layers = {pulse, impact};

    SoundEngine engine;
    engine.play(shot, 0.7); // caller-defined sound, without registering a preset
    constexpr int frames = SAMPLE_RATE / 2;
    std::vector<float> stereo(frames * 2);
    engine.getAudioBuffer(stereo.data(), frames, 2);
    std::cout << "Rendered " << frames << " stereo frames\n";
}
```

Build and run on Linux:

```sh
g++ -std=c++17 -O2 -pthread custom_sound.cpp -o custom_sound
./custom_sound
```

On Windows use `cl /nologo /std:c++17 /EHsc /O2 custom_sound.cpp /Fe:custom_sound.exe`. The example renders to memory; connect that buffer to a device or use an application-specific writer to hear it. Start with conservative gains and audition the result. `engine.play(shot)` uses the supplied definition directly; it does not register a name with the CLI.

### Add a named preset

To make a new preset available to `--list`, `--sound`, and `play("name")`, add a recipe inside `SoundLibrary::designPresets()` in `presets.h`, before its final loop rebuilds `m_soundsByName`:

```cpp
recipe("ui_notice_new", "ui", 660, .12, 300,
       {tone(Waveform::SINE, .7, 100),
        delayed(tone(Waveform::TRIANGLE, .3, 80), 80)}, 60);
```

Arguments are name, category, base frequency, amplitude, duration in milliseconds, layers, and release in milliseconds. `recipe`, `tone`, and `delayed` are library helper functions; see `presets.h` for more examples. Choose a unique name, rebuild, run `make test`, and audition the sound. The library's tests currently assert its preset count and will need updating when presets are added.

## Windows playback with DirectX XAudio2

This finite playback example creates a DirectX XAudio2 source voice and submits one second of **32-bit float** stereo audio produced by the engine. This float format is the engine buffer format, not the 16-bit format used for WAV/PCM export. The sample owns its data until the source voice is destroyed; it also checks initialization failures and limits its wait. It is a minimal integration example, not a continuous game audio manager.

Save it as `xaudio_demo.cpp` next to `engine.cpp`, `synthesis.h`, and `presets.h`:

```cpp
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <objbase.h>
#include <mmreg.h>
#include <xaudio2.h>
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <vector>

#define SOUND_ENGINE_NO_MAIN
#include "engine.cpp"

// Destruction order: source voice, mastering voice, XAudio2, then COM.
struct AudioResources {
    IXAudio2* audio = nullptr;
    IXAudio2MasteringVoice* master = nullptr;
    IXAudio2SourceVoice* source = nullptr;
    ~AudioResources() {
        if (source) source->DestroyVoice();
        if (master) master->DestroyVoice();
        if (audio) audio->Release();
    }
};
struct ComScope {
    ComScope() {
        HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        if (FAILED(hr)) throw std::runtime_error("CoInitializeEx failed; use a compatible COM apartment");
    }
    ~ComScope() { CoUninitialize(); }
};
void check(HRESULT hr, const char* operation) {
    if (FAILED(hr)) throw std::runtime_error(operation);
}

int main() {
    try {
        ComScope com;
        // Declare samples before voices: XAudio2 may reference them until the
        // source voice finishes or is destroyed, including on error paths.
        constexpr int frames = SAMPLE_RATE; // 1 second
        std::vector<float> samples(frames * 2);
        SoundEngine synth;
        synth.play("ui_confirm", 0.8);
        synth.getAudioBuffer(samples.data(), frames, 2);

        AudioResources xa;
        check(XAudio2Create(&xa.audio, 0, XAUDIO2_DEFAULT_PROCESSOR), "XAudio2Create failed");
        check(xa.audio->CreateMasteringVoice(&xa.master), "CreateMasteringVoice failed");

        WAVEFORMATEX format{};
        format.wFormatTag = WAVE_FORMAT_IEEE_FLOAT;
        format.nChannels = 2;
        format.nSamplesPerSec = SAMPLE_RATE;
        format.wBitsPerSample = 32;
        format.nBlockAlign = format.nChannels * (format.wBitsPerSample / 8);
        format.nAvgBytesPerSec = format.nSamplesPerSec * format.nBlockAlign;
        check(xa.audio->CreateSourceVoice(&xa.source, &format), "CreateSourceVoice failed");

        XAUDIO2_BUFFER buffer{};
        buffer.Flags = XAUDIO2_END_OF_STREAM;
        buffer.AudioBytes = static_cast<UINT32>(samples.size() * sizeof(float));
        buffer.pAudioData = reinterpret_cast<const BYTE*>(samples.data());
        check(xa.source->SubmitSourceBuffer(&buffer), "SubmitSourceBuffer failed");
        check(xa.source->Start(), "Start failed");

        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        XAUDIO2_VOICE_STATE state{};
        do {
            xa.source->GetState(&state, XAUDIO2_VOICE_NOSAMPLESPLAYED);
            if (state.BuffersQueued == 0) break;
            if (std::chrono::steady_clock::now() >= deadline)
                throw std::runtime_error("Playback did not complete");
            Sleep(10);
        } while (true);
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
```

From a Visual Studio Developer Command Prompt with the Windows SDK installed:

```bat
cl /nologo /std:c++17 /EHsc /O2 xaudio_demo.cpp /Fe:xaudio_demo.exe /link xaudio2.lib ole32.lib
xaudio_demo.exe
```

The example initializes COM on its own thread. In an application with an existing COM apartment, integrate initialization and cleanup with the application's COM ownership; do not call `CoUninitialize` for an initialization you do not own. If COM was already initialized on the thread with an incompatible model, `CoInitializeEx` can fail.

For continuous playback, render successive buffers on a suitable worker thread and queue them on a source voice. Keep every submitted audio buffer alive until XAudio2 signals it has finished, and do not reuse that storage early. Handle device loss, queue starvation, shutdown, and timing in your audio system. Do not perform blocking work in XAudio2 callbacks. The one-buffer example uses polling for clarity, not as a streaming architecture.

Microsoft references: [XAudio2 initialization](https://learn.microsoft.com/en-us/windows/win32/xaudio2/how-to--initialize-xaudio2), [source voice formats](https://learn.microsoft.com/en-us/windows/win32/api/xaudio2/nf-xaudio2-ixaudio2-createsourcevoice), and [buffer lifetime](https://learn.microsoft.com/en-us/windows/win32/api/xaudio2/nf-xaudio2-ixaudio2sourcevoice-submitsourcebuffer).

## Troubleshooting

| Symptom | Check |
| --- | --- |
| Silent output | Confirm the preset with `--list`, the volume, and that the host calls `getAudioBuffer` and sends it to an audio backend. |
| Sound ends early | Increase `--seconds`; include the preset duration, release, and any desired effect tail. |
| Raw audio sounds wrong | Import it as signed 16-bit little-endian, 48 kHz stereo, with no header. |
| Distortion while layering | Lower per-voice or master volume; the output clamp is not a mastering limiter. |
| XAudio2 initialization fails | Check the Windows SDK, COM result, active output device, and HRESULTs. |
| Compiler cannot find `synthesis.h` or `presets.h` | Keep both files beside `engine.cpp`; compile from the appropriate directory. |

## License

MIT. See `LICENSE` in the full repository.
