#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "UIAction.hpp"

namespace realmz::presentation {

using EventSequence = uint64_t;

enum class MessageSeverity {
  information,
  success,
  warning,
  error,
};

struct MessageEvent {
  MessageSeverity severity = MessageSeverity::information;
  std::string text;

  bool operator==(const MessageEvent&) const = default;
};

struct ModalButtonView {
  int32_t id = 0;
  std::string label;
  bool enabled = true;

  bool operator==(const ModalButtonView&) const = default;
};

struct ModalRequestEvent {
  uint64_t request_id = 0;
  std::string title;
  std::string body;
  std::vector<ModalButtonView> buttons;
  std::optional<int32_t> default_button;
  std::optional<int32_t> cancel_button;

  bool operator==(const ModalRequestEvent&) const = default;
};

struct AnimationCueEvent {
  std::string cue;
  ActionTarget target;
  bool essential_motion = false;

  bool operator==(const AnimationCueEvent&) const = default;
};

enum class AudioBus {
  interface_sound,
  effect,
  music,
  ambience,
};

struct AudioCueEvent {
  std::string cue;
  AudioBus bus = AudioBus::effect;
  bool stop = false;

  bool operator==(const AudioCueEvent&) const = default;
};

struct ScreenTransitionEvent {
  ScreenContext destination = ScreenContext::title;

  bool operator==(const ScreenTransitionEvent&) const = default;
};

using GameEventPayload = std::variant<
    MessageEvent,
    ModalRequestEvent,
    AnimationCueEvent,
    AudioCueEvent,
    ScreenTransitionEvent>;

struct GameEvent {
  EventSequence sequence = 0;
  GameEventPayload payload;

  bool operator==(const GameEvent&) const = default;
};

} // namespace realmz::presentation
