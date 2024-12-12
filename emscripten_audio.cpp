#include "emscripten_audio.h"
#include <iostream>
#include <limits>
#include <magic_enum/magic_enum.hpp>

extern "C" {

EMSCRIPTEN_KEEPALIVE void audio_worklet_unpause_return(void *callback_data) {
  /// Return helper to call C++ member function from a js callback
  auto &parent{*static_cast<emscripten_audio*>(callback_data)};
  parent.audio_worklet_unpause();
}

}

static_assert(std::to_underlying(emscripten_audio::states::suspended  ) == AUDIO_CONTEXT_STATE_SUSPENDED  ); // make sure enums stay in sync in case of future updates to Emscripten
static_assert(std::to_underlying(emscripten_audio::states::running    ) == AUDIO_CONTEXT_STATE_RUNNING    );
static_assert(std::to_underlying(emscripten_audio::states::closed     ) == AUDIO_CONTEXT_STATE_CLOSED     );
static_assert(std::to_underlying(emscripten_audio::states::interrupted) == AUDIO_CONTEXT_STATE_INTERRUPTED);

emscripten_audio::emscripten_audio(construction_options &&options)
  : worklet_name{std::move(options.worklet_name)},
    latency_hint{options.latency_hint},
    inputs{options.inputs},
    output_channels{std::move(options.output_channels)},
    callbacks{std::move(options.callbacks)} {
  /// Initialise an Emscripten audio worklet with the given callbacks
  assert(inputs <= std::numeric_limits<int>::max());
  assert(output_channels.size() <= std::numeric_limits<int>::max());

  sample_rate = static_cast<unsigned int>(EM_ASM_DOUBLE({
    var AudioContext = window.AudioContext || window.webkitAudioContext;
    var ctx = new AudioContext();
    var sr = ctx.sampleRate;
    ctx.close();
    return sr;
  }));

  std::string const &latency_hint_str{magic_enum::enum_name(latency_hint)};
  EmscriptenWebAudioCreateAttributes create_audio_context_options{
    .latencyHint{latency_hint_str.c_str()},                                     // one of "balanced", "interactive" or "playback"
    .sampleRate{sample_rate},                                                   // 44100 or 48000
  };
  EMSCRIPTEN_WEBAUDIO_T emscripten_audio_context{emscripten_create_audio_context(&create_audio_context_options)};

  emscripten_start_wasm_audio_worklet_thread_async(
    emscripten_audio_context,
    audio_thread_stack.data(),
    audio_thread_stack.size(),
    [](EMSCRIPTEN_WEBAUDIO_T audio_context, bool success, void *user_data) {    // EmscriptenStartWebAudioWorkletCallback
      /// Callback that runs when audio creation is complete (either success or fail)
      auto &parent{*static_cast<emscripten_audio*>(user_data)};
      if(!success) {
        std::cerr << "ERROR: Emscripten Audio: Worklet start failed for context " << audio_context << std::endl;
        return;
      }
      parent.context = audio_context;

      WebAudioWorkletProcessorCreateOptions worklet_create_options{
        .name{parent.worklet_name.c_str()},
        .numAudioParams{0},
        .audioParamDescriptors{nullptr},
      };
      emscripten_create_wasm_audio_worklet_processor_async(
        audio_context,
        &worklet_create_options,
        [](EMSCRIPTEN_WEBAUDIO_T audio_context, bool success, void *user_data) { // EmscriptenWorkletProcessorCreatedCallback
          /// Callback that runs when the worklet processor creation has completed (either success or fail)
          auto &parent{*static_cast<emscripten_audio*>(user_data)};
          if(!success) {
            std::cerr << "ERROR: Emscripten Audio: Worklet processor creation failed for context " << audio_context << std::endl;
            return;
          }

          std::vector<int> output_channels_int;
          output_channels_int.reserve(parent.output_channels.size());
          for(auto const &channel : parent.output_channels) {                   // convert container of channels to a vector of non-const signed ints as required by emscripten
            assert(channel < std::numeric_limits<int>::max());
            output_channels_int.emplace_back(static_cast<int>(channel));
          }

          EmscriptenAudioWorkletNodeCreateOptions worklet_node_create_options{
            .numberOfInputs{static_cast<int>(parent.inputs)},
            .numberOfOutputs{static_cast<int>(output_channels_int.size())},
            .outputChannelCounts{output_channels_int.data()},
          };
          EMSCRIPTEN_AUDIO_WORKLET_NODE_T audio_worklet{emscripten_create_wasm_audio_worklet_node( // create node
            audio_context,
            parent.worklet_name.c_str(),                                        // must match the name set in WebAudioWorkletProcessorCreateOptions
            &worklet_node_create_options,
            [](int num_inputs,  AudioSampleFrame const *inputs,                 // EmscriptenWorkletNodeProcessCallback
               int num_outputs, AudioSampleFrame *outputs,
               int num_params,  AudioParamFrame const *params,
               void *user_data) {
              /// Audio processing callback dispatcher
              auto &parent{*static_cast<emscripten_audio*>(user_data)};
              if(parent.callbacks.input ) parent.callbacks.input( {inputs, static_cast<size_t>(num_inputs)});
              if(parent.callbacks.params) parent.callbacks.params({params, static_cast<size_t>(num_params)});
              if(parent.callbacks.output) {
                parent.callbacks.output({outputs, static_cast<size_t>(num_outputs)});
              } else {                                                          // if no output function is provided, output silence to avoid generating noise
                for(auto &output : std::span{outputs, static_cast<size_t>(num_outputs)}) {
                  std::memset(output.data, 0, static_cast<size_t>(output.numberOfChannels) * static_cast<size_t>(output.samplesPerChannel) * sizeof(float)); // could use std::fill here but memset is reportedly faster, https://lemire.me/blog/2020/01/20/filling-large-arrays-with-zeroes-quickly-in-c/
                }
              }
              return true;                                                      // keep the graph output going
            },
            &parent
          )};

          emscripten_audio_node_connect(audio_worklet, audio_context, 0, 0);    // connect the node to an audio destination.  EMSCRIPTEN_WEBAUDIO_T source, EMSCRIPTEN_WEBAUDIO_T destination, int outputIndex, int inputIndex

          // register a once-only click handler to unpause audio, on the first click on the canvas
          EM_ASM({
            window.addEventListener("click", (event) => {
              Module["ccall"]('audio_worklet_unpause_return', null, ['number'], [$0]);
            }, {once : true});
          }, &parent);
        },
        &parent
      );
    },
    this
  );
}

EMSCRIPTEN_WEBAUDIO_T emscripten_audio::get_context() const {
  return context;
}

emscripten_audio::states emscripten_audio::get_state() const {
  return state;
}

unsigned int emscripten_audio::get_sample_rate() const {
  return sample_rate;
}

void emscripten_audio::audio_worklet_unpause() {
  /// Unpause the audio after the first user click on the canvas
  state = static_cast<states>(emscripten_audio_context_state(context));         // AUDIO_CONTEXT_STATE_SUSPENDED=0, AUDIO_CONTEXT_STATE_RUNNING=1, AUDIO_CONTEXT_STATE_CLOSED=2. AUDIO_CONTEXT_STATE_INTERRUPTED=3
  if(state == states::running) {                                                // if for some reason autoplay was not prevented, we might be running already
    if(callbacks.playback_started) callbacks.playback_started();
    return;
  }

  emscripten_resume_audio_context_async(
    context,
    [](EMSCRIPTEN_WEBAUDIO_T /*audio_context*/, AUDIO_CONTEXT_STATE state, void *user_data){ // EmscriptenResumeAudioContextCallback
      auto &parent{*static_cast<emscripten_audio*>(user_data)};
      parent.state = static_cast<states>(state);
      if(parent.callbacks.playback_started) parent.callbacks.playback_started();
    },
    this
  );
}
