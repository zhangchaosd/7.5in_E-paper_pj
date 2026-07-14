#pragma once

#include <Arduino.h>

namespace board {

// Physical positions on the right edge, excluding the separate EN reset key.
enum class ButtonId : uint8_t { Upper = 0, Middle = 1, Lower = 2 };

enum class ButtonGesture : uint8_t { ShortPress = 0, LongPress = 1 };

struct ButtonEvent {
  ButtonId button = ButtonId::Upper;
  ButtonGesture gesture = ButtonGesture::ShortPress;
  uint32_t atMs = 0;
};

const char* buttonName(ButtonId button);
const char* buttonGestureName(ButtonGesture gesture);

// Debounces the three active-low, externally pulled-up PCB buttons.
//
// Call update() regularly from the Arduino loop. A ShortPress is emitted on
// release. A LongPress is emitted once, while the button remains held, and
// suppresses the subsequent ShortPress. No internal pull-up is enabled.
class Buttons {
 public:
  void begin(uint32_t nowMs = millis());
  void update(uint32_t nowMs = millis());

  bool nextEvent(ButtonEvent& event);
  bool isPressed(ButtonId button) const;
  void clearEvents();
  uint32_t droppedEventCount() const { return droppedEvents_; }

 private:
  struct PinState {
    uint8_t pin = 0;
    bool rawPressed = false;
    bool stablePressed = false;
    bool longReported = false;
    uint32_t rawChangedAtMs = 0;
    uint32_t pressedAtMs = 0;
  };

  static constexpr uint8_t kButtonCount = 3;
  static constexpr uint8_t kEventCapacity = 8;

  void updateOne(PinState& state, ButtonId button, uint32_t nowMs);
  void enqueue(ButtonId button, ButtonGesture gesture, uint32_t atMs);
  static uint8_t indexOf(ButtonId button);

  PinState states_[kButtonCount] = {};
  ButtonEvent events_[kEventCapacity] = {};
  uint8_t readIndex_ = 0;
  uint8_t writeIndex_ = 0;
  uint8_t eventCount_ = 0;
  uint32_t droppedEvents_ = 0;
  bool begun_ = false;
};

}  // namespace board
