# Emscripten-Audio

A lightweight C++ library to make it easy to work with realtime, low latency [WASM audio worklets](https://emscripten.org/docs/api_reference/wasm_audio_worklets.html) under Emscripten.

## Demos

- Live demo: https://armchair-software.github.io/emscripten-sound-demo/
- Demo source code: https://github.com/Armchair-Software/emscripten-sound-demo

## Example

Minimal example, logging when playback starts and creating a simple white noise generator:
```cpp
  emscripten_audio audio{{
    .callbacks{
      .playback_started{[&]{  // called when the user clicks on the window (which is needed to enable audio playback in modern browsers)
        std::cout << "Hello world, playback has started!" << std::endl;
      }},
      .output{[&](std::span<AudioSampleFrame> outputs){
        for(auto const &output : outputs) {
          for(int i{0}; i != output.samplesPerChannel; ++i) {
            for(int channel{0}; channel != output.numberOfChannels; ++channel) {
              // set your samples here however you want them - this example just generates white noise:
              output.data[channel * output.samplesPerChannel + i] = emscripten_random() * 0.2 - 0.1;
            }
          }
        }
      }},
    },
  }};
```

## Usage

`emscripten_audio`'s constructor accepts a `construction_options` struct, which can be used to configure all initial behaviour:
```cpp
  struct construction_options {
    unsigned int inputs{0};                                                     // number of inputs
    std::vector<unsigned int> output_channels{2};                               // number of outputs, and number of channels for each output
    latencies latency_hint{latencies::interactive};                             // hint for requested latency mode
    std::string worklet_name{"emscripten-audio-worklet"};
    callback_types callbacks{};                                                 // action and data processing callbacks
  };
```
All members are optional, with sensible defaults (as defined above).  Callbacks can be set initially, or left unset and set later if desired, or simply not set at all.  To produce any sound, you'll eventually want to set the `output` callback at least.

How latency hints are handled is up to the implementation.  The available hints are:
```cpp
  enum class latencies {
    balanced,
    interactive,
    playback,
  };
```
Here `interactive` will usually be the lowest available latency, and `balanced` will typically aim to use the least device power, but exact results may vary for your device.

Number of inputs, outputs and their channels, latency hint and worklet name are fixed after initial construction.  The values of inputs and outputs and their channels can be accessed as const members `audio.inputs` and `audio.output_channels`.

Callbacks can also be updated at any time after program start, by modifying `audio.callbacks.input`, `audio.callbacks.output` and `audio.callbacks.params`.  If no output callback is created but non-zero output channels exist, they will be filled with zeros to avoid inadvertently generating white noise.

Getters are provided for further information:
```cpp  
  EMSCRIPTEN_WEBAUDIO_T get_context() const;
  states get_state() const;
  unsigned int get_sample_rate() const;
```

The playback states match up with Emscripten's `AUDIO_CONTEXT_STATE_*` macros, as follows:
```cpp
  enum class states {
    suspended,
    running,
    closed,
    interrupted,
  };
```

## Dependencies
- [Emscripten](https://emscripten.org/)
- [magic_enum](https://github.com/Neargye/magic_enum)

## For the other Emscripten utility micro-libraries, see:
- [Emscripten Browser Clipboard](https://github.com/Armchair-Software/emscripten-browser-clipboard)
- [Emscripten Browser Cursor](https://github.com/Armchair-Software/emscripten-browser-cursor)
- [Emscripten Browser File](https://github.com/Armchair-Software/emscripten-browser-file)
