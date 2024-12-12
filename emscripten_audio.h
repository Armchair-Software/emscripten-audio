#pragma once
#include <array>
#include <functional>
#include <span>
#include <string>
#include <vector>
#include <emscripten/webaudio.h>

extern "C" {
EMSCRIPTEN_KEEPALIVE void audio_worklet_unpause_return(void *callback_data);
}

class emscripten_audio {
public:
  enum class latencies {
    balanced,
    interactive,
    playback,
  };

  struct callback_types {
    std::function<void()> playback_started{};
    std::function<void(std::span<AudioSampleFrame const>)> input{};
    std::function<void(std::span<AudioSampleFrame      >)> output{};
    std::function<void(std::span<AudioParamFrame const >)> params{};
  };

  struct construction_options {
    unsigned int inputs{0};                                                     // number of inputs
    std::vector<unsigned int> output_channels{2};                               // number of outputs, and number of channels for each output
    latencies latency_hint{latencies::interactive};                             // hint for requested latency mode
    std::string worklet_name{"emscripten-audio-worklet"};
    callback_types callbacks{};                                                 // action and data processing callbacks
  };

  enum class states {                                                           // equivalent to AUDIO_CONTEXT_STATE_* macros
    suspended,
    running,
    closed,
    interrupted,
  };

private:
  struct alignas(16) {
    std::array<uint8_t, 4096> audio_thread_stack;
  };

  EMSCRIPTEN_WEBAUDIO_T context{};
  std::string worklet_name{construction_options{}.worklet_name};
  latencies latency_hint{construction_options{}.latency_hint};
  unsigned int sample_rate{0};
  states state{states::suspended};
public:
  unsigned int const inputs{0};                                                 // number of inputs
  std::vector<unsigned int> const output_channels{2};                           // number of outputs, and channels for each output

  callback_types callbacks;

  emscripten_audio(construction_options &&options);

  EMSCRIPTEN_WEBAUDIO_T get_context() const;
  states get_state() const;
  unsigned int get_sample_rate() const;

private:
  void audio_worklet_unpause();
  friend void audio_worklet_unpause_return(void *callback_data);
};
