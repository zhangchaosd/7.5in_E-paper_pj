#include "board/Buttons.h"

#include "board/BoardConfig.h"

namespace board {
namespace {

constexpr uint8_t kPins[] = {kButtonUpper, kButtonMiddle, kButtonLower};

bool readPressed(uint8_t pin) {
  return digitalRead(pin) == kButtonPressedLevel;
}

}  // namespace

const char* buttonName(ButtonId button) {
  switch (button) {
    case ButtonId::Upper:
      return "upper";
    case ButtonId::Middle:
      return "middle";
    case ButtonId::Lower:
      return "lower";
  }
  return "unknown";
}

const char* buttonGestureName(ButtonGesture gesture) {
  switch (gesture) {
    case ButtonGesture::ShortPress:
      return "short";
    case ButtonGesture::LongPress:
      return "long";
  }
  return "unknown";
}

void Buttons::begin(uint32_t nowMs) {
  clearEvents();
  droppedEvents_ = 0;

  for (uint8_t i = 0; i < kButtonCount; ++i) {
    PinState& state = states_[i];
    state.pin = kPins[i];

    // GPIO34/35/39 are input-only and the PCB supplies the pull-ups. INPUT is
    // intentional here; INPUT_PULLUP must not be used on these pins.
    pinMode(state.pin, INPUT);
    const bool pressed = readPressed(state.pin);
    state.rawPressed = pressed;
    state.stablePressed = pressed;
    state.longReported = false;
    state.rawChangedAtMs = nowMs;
    state.pressedAtMs = pressed ? nowMs : 0;
  }

  begun_ = true;
}

void Buttons::update(uint32_t nowMs) {
  if (!begun_) {
    begin(nowMs);
  }

  updateOne(states_[0], ButtonId::Upper, nowMs);
  updateOne(states_[1], ButtonId::Middle, nowMs);
  updateOne(states_[2], ButtonId::Lower, nowMs);
}

bool Buttons::nextEvent(ButtonEvent& event) {
  if (eventCount_ == 0) {
    return false;
  }

  event = events_[readIndex_];
  readIndex_ = static_cast<uint8_t>((readIndex_ + 1U) % kEventCapacity);
  --eventCount_;
  return true;
}

bool Buttons::isPressed(ButtonId button) const {
  return states_[indexOf(button)].stablePressed;
}

void Buttons::clearEvents() {
  readIndex_ = 0;
  writeIndex_ = 0;
  eventCount_ = 0;
}

void Buttons::updateOne(PinState& state, ButtonId button, uint32_t nowMs) {
  const bool pressed = readPressed(state.pin);
  if (pressed != state.rawPressed) {
    state.rawPressed = pressed;
    state.rawChangedAtMs = nowMs;
  }

  if (state.rawPressed != state.stablePressed &&
      static_cast<uint32_t>(nowMs - state.rawChangedAtMs) >=
          kButtonDebounceMs) {
    state.stablePressed = state.rawPressed;

    if (state.stablePressed) {
      state.pressedAtMs = nowMs;
      state.longReported = false;
    } else {
      if (!state.longReported) {
        enqueue(button, ButtonGesture::ShortPress, nowMs);
      }
      state.pressedAtMs = 0;
    }
  }

  if (state.stablePressed && !state.longReported &&
      static_cast<uint32_t>(nowMs - state.pressedAtMs) >=
          kButtonLongPressMs) {
    state.longReported = true;
    enqueue(button, ButtonGesture::LongPress, nowMs);
  }
}

void Buttons::enqueue(ButtonId button, ButtonGesture gesture, uint32_t atMs) {
  if (eventCount_ == kEventCapacity) {
    ++droppedEvents_;
    return;
  }

  events_[writeIndex_] = ButtonEvent{button, gesture, atMs};
  writeIndex_ = static_cast<uint8_t>((writeIndex_ + 1U) % kEventCapacity);
  ++eventCount_;
}

uint8_t Buttons::indexOf(ButtonId button) {
  const uint8_t index = static_cast<uint8_t>(button);
  return index < kButtonCount ? index : 0;
}

}  // namespace board
