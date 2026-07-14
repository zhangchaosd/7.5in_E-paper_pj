// SafePanelDriver's panel command/write sequence is adapted from
// GxEPD2_750c_Z08 in GxEPD2 1.6.9 by Jean-Marc Zingg
// (https://github.com/ZinggJM/GxEPD2), licensed under GPL-3.0.
// See THIRD_PARTY_NOTICES.md at the repository root.

#include "ui/EpdController.h"

#include <GxEPD2_3C.h>
#include <SPI.h>
#include <driver/gpio.h>
#include <esp_attr.h>
#include <pgmspace.h>

#include "board/BoardConfig.h"

namespace ui {
namespace {

constexpr uint32_t kJournalMagic = 0x45504431UL;  // "EPD1"
constexpr uint32_t kJournalCheckSalt = 0xA75C39E2UL;

struct RtcPanelJournal {
  uint32_t magic;
  uint32_t state;
  uint32_t sequence;
  uint32_t check;
};

// Do not let startup code reinitialize a retained journal. Cold-start garbage
// is rejected by magic + state range + checksum and becomes Unknown.
RTC_NOINIT_ATTR RtcPanelJournal gRtcPanelJournal;

uint32_t journalCheck(const RtcPanelJournal& value) {
  return value.magic ^ value.state ^ value.sequence ^ kJournalCheckSalt;
}

bool validJournal(const RtcPanelJournal& value) {
  return value.magic == kJournalMagic &&
         value.state <= static_cast<uint32_t>(PanelJournalState::Fault) &&
         value.check == journalCheck(value);
}

PanelJournalState readJournal() {
  if (!validJournal(gRtcPanelJournal)) return PanelJournalState::Unknown;
  return static_cast<PanelJournalState>(gRtcPanelJournal.state);
}

class SafePanelDriver : public GxEPD2_750c_Z08 {
 public:
  using GxEPD2_750c_Z08::writeImage;

  SafePanelDriver(int16_t cs, int16_t dc, int16_t rst, int16_t busy)
      : GxEPD2_750c_Z08(cs, dc, rst, busy) {}

  void init(uint32_t serialDiagBitrate = 0) override {
    init(serialDiagBitrate, true, 10, false);
  }

  void init(uint32_t serialDiagBitrate, bool initial,
            uint16_t resetDuration = 10,
            bool pulldownRstMode = false) override {
    operationFailed_ = false;
    failureStage_ = FailureStage::None;
    GxEPD2_750c_Z08::init(serialDiagBitrate, initial, resetDuration,
                          pulldownRstMode);
  }

  // This is the dual-plane overload used by GxEPD2_3C::nextPage().  It is
  // intentionally implemented here instead of calling the upstream Z08
  // method because upstream's void _waitWhileBusy() continues after timeout.
  void writeImage(const uint8_t* black, const uint8_t* color, int16_t x,
                  int16_t y, int16_t w, int16_t h, bool invert = false,
                  bool mirrorY = false, bool pgm = false) {
    if (operationFailed_) return;
    if (x != 0 || w != static_cast<int16_t>(WIDTH) || y < 0 || h <= 0 ||
        y + h > static_cast<int16_t>(HEIGHT) || black == nullptr ||
        color == nullptr) {
      fail(FailureStage::Geometry);
      return;
    }
    if (!initializeDisplay() || !powerOn()) return;

    _initial_write = false;
    const uint16_t widthBytes = static_cast<uint16_t>((w + 7) / 8);
    setPartialRamArea(static_cast<uint16_t>(x), static_cast<uint16_t>(y),
                      static_cast<uint16_t>(w), static_cast<uint16_t>(h));

    _writeCommand(0x10);  // black plane
    _startTransfer();
    for (int16_t row = 0; row < h; ++row) {
      const int16_t sourceRow = mirrorY ? h - 1 - row : row;
      for (uint16_t column = 0; column < widthBytes; ++column) {
        const uint32_t index =
            static_cast<uint32_t>(sourceRow) * widthBytes + column;
        uint8_t value = pgm ? pgm_read_byte(&black[index]) : black[index];
        _transfer(invert ? static_cast<uint8_t>(~value) : value);
      }
    }
    _endTransfer();

    _writeCommand(0x13);  // chromatic plane
    _startTransfer();
    for (int16_t row = 0; row < h; ++row) {
      const int16_t sourceRow = mirrorY ? h - 1 - row : row;
      for (uint16_t column = 0; column < widthBytes; ++column) {
        const uint32_t index =
            static_cast<uint32_t>(sourceRow) * widthBytes + column;
        uint8_t value = pgm ? pgm_read_byte(&color[index]) : color[index];
        if (invert) value = static_cast<uint8_t>(~value);
        _transfer(static_cast<uint8_t>(~value));
      }
    }
    _endTransfer();
    _writeCommand(0x92);  // partial out
    delay(1);
  }

  void refresh(bool partialUpdateMode = false) override {
    if (operationFailed_) return;
    if (partialUpdateMode || !initializeDisplay() || !powerOn()) {
      if (partialUpdateMode) fail(FailureStage::Geometry);
      return;
    }
    _writeCommand(0x12);
    waitReady(board::kEpdStageTimeoutMs, FailureStage::Refresh);
  }

  void refresh(int16_t x, int16_t y, int16_t w, int16_t h) override {
    if (x == 0 && y == 0 && w == static_cast<int16_t>(WIDTH) &&
        h == static_cast<int16_t>(HEIGHT)) {
      refresh(false);
    } else {
      fail(FailureStage::Geometry);
    }
  }

  void powerOff() override {
    // A prior timeout is terminal for this attempt.  In particular, the
    // wrapper's automatic powerOff after refresh must not issue command 0x02
    // if the refresh BUSY wait failed.
    if (operationFailed_ || !_power_is_on) return;
    _writeCommand(0x02);
    if (!waitReady(board::kEpdPostResetReadyMs, FailureStage::PowerOff)) return;
    _power_is_on = false;
  }

  void hibernate() override {
    if (operationFailed_) return;
    powerOff();
    if (operationFailed_ || _rst < 0) return;
    _writeCommand(0x07);
    _writeData(0xA5);
    _hibernating = true;
  }

  bool operationFailed() const { return operationFailed_; }

  // Fault cleanup may use this only after the controller has independently
  // observed BUSY HIGH.  It intentionally ignores the latched operation flag.
  bool emergencyPowerOff() {
    if (digitalRead(_busy) == _busy_level) return false;
    if (!_power_is_on) return true;
    _writeCommand(0x02);
    if (!waitReadyRaw(board::kEpdPostResetReadyMs)) return false;
    _power_is_on = false;
    return true;
  }

  const char* failureStageName() const {
    switch (failureStage_) {
      case FailureStage::PowerOn:
        return "power_on";
      case FailureStage::Refresh:
        return "refresh";
      case FailureStage::PowerOff:
        return "power_off";
      case FailureStage::Geometry:
        return "geometry";
      case FailureStage::None:
      default:
        return "none";
    }
  }

 private:
  enum class FailureStage : uint8_t {
    None = 0,
    PowerOn,
    Refresh,
    PowerOff,
    Geometry,
  };

  void fail(FailureStage stage) {
    operationFailed_ = true;
    failureStage_ = stage;
  }

  bool waitReadyRaw(uint32_t timeoutMs) {
    const uint32_t startedAtMs = millis();
    while (digitalRead(_busy) == _busy_level) {
      if (millis() - startedAtMs >= timeoutMs) return false;
      delay(1);
      yield();
    }
    return true;
  }

  bool waitReady(uint32_t timeoutMs, FailureStage stage) {
    if (waitReadyRaw(timeoutMs)) return true;
    fail(stage);
    return false;
  }

  bool initializeDisplay() {
    if (_init_display_done) return true;
    _writeCommand(0x01);  // power setting
    _writeData(0x07);
    _writeData(0x07);
    _writeData(0x3F);
    _writeData(0x3F);
    _writeCommand(0x00);  // panel setting
    _writeData(0x0F);
    _writeCommand(0x61);  // resolution 800x480
    _writeData(WIDTH / 256);
    _writeData(WIDTH % 256);
    _writeData(HEIGHT / 256);
    _writeData(HEIGHT % 256);
    _writeCommand(0x15);
    _writeData(0x00);
    _writeCommand(0x50);  // VCOM and data interval
    _writeData(0x11);
    _writeData(0x07);
    _writeCommand(0x60);  // TCON
    _writeData(0x22);
    _init_display_done = true;
    return true;
  }

  bool powerOn() {
    if (_power_is_on) return true;
    _writeCommand(0x04);
    // Voltage generation may already be active even if BUSY never returns.
    // Record that possibility before waiting so emergencyPowerOff() cannot
    // incorrectly treat a timeout as an already-off panel.
    _power_is_on = true;
    if (!waitReady(board::kEpdPostResetReadyMs, FailureStage::PowerOn)) {
      return false;
    }
    return true;
  }

  void setPartialRamArea(uint16_t x, uint16_t y, uint16_t w, uint16_t h) {
    const uint16_t endX = static_cast<uint16_t>((x + w - 1U) | 0x0007U);
    const uint16_t endY = static_cast<uint16_t>(y + h - 1U);
    x &= 0xFFF8U;
    _writeCommand(0x91);  // partial in; used here only to address each page
    _writeCommand(0x90);
    _writeData(x / 256);
    _writeData(x % 256);
    _writeData(endX / 256);
    _writeData(endX % 256);
    _writeData(y / 256);
    _writeData(y % 256);
    _writeData(endY / 256);
    _writeData(endY % 256);
    _writeData(0x00);
  }

  bool operationFailed_ = false;
  FailureStage failureStage_ = FailureStage::None;
};

using PanelDisplay = GxEPD2_3C<SafePanelDriver, board::kEpdPageHeight>;

PanelDisplay gDisplay(SafePanelDriver(board::kEpdCs, board::kEpdDc,
                                     board::kEpdReset, board::kEpdBusy));

void driveHigh(uint8_t pin) {
  gpio_set_level(static_cast<gpio_num_t>(pin), HIGH);
  pinMode(pin, OUTPUT);
  digitalWrite(pin, HIGH);
}

void driveLow(uint8_t pin) {
  gpio_set_level(static_cast<gpio_num_t>(pin), LOW);
  pinMode(pin, OUTPUT);
  digitalWrite(pin, LOW);
}

bool uncertainState(PanelJournalState state) {
  return state == PanelJournalState::Unknown ||
         state == PanelJournalState::Initializing ||
         state == PanelJournalState::Refreshing ||
         state == PanelJournalState::Recovering;
}

}  // namespace

const char* panelJournalStateName(PanelJournalState state) {
  switch (state) {
    case PanelJournalState::Initializing:
      return "initializing";
    case PanelJournalState::Refreshing:
      return "refreshing";
    case PanelJournalState::PoweredOff:
      return "powered_off";
    case PanelJournalState::Hibernated:
      return "hibernated";
    case PanelJournalState::Recovering:
      return "recovering";
    case PanelJournalState::Fault:
      return "fault";
    case PanelJournalState::Unknown:
    default:
      return "unknown";
  }
}

const char* epdErrorName(EpdError error) {
  switch (error) {
    case EpdError::ReentrantRefresh:
      return "reentrant_refresh";
    case EpdError::PersistedFault:
      return "persisted_fault";
    case EpdError::SpiBeginFailed:
      return "spi_begin_failed";
    case EpdError::BusyBeforeInit:
      return "busy_before_init";
    case EpdError::BusyAfterInit:
      return "busy_after_init";
    case EpdError::RefreshTimedOut:
      return "refresh_timed_out";
    case EpdError::DriverOperationFailed:
      return "driver_operation_failed";
    case EpdError::BusyAfterRefresh:
      return "busy_after_refresh";
    case EpdError::None:
    default:
      return "none";
  }
}

EpdController::EpdController() : journalState_(readJournal()) {
  faultLocked_ = journalState_ == PanelJournalState::Fault;
  if (faultLocked_) lastError_ = EpdError::PersistedFault;
}

bool EpdController::refresh(const app::UiModel& model) {
  if (refreshInProgress_) {
    lastError_ = EpdError::ReentrantRefresh;
    return false;
  }
  if (faultLocked_ || readJournal() == PanelJournalState::Fault) {
    faultLocked_ = true;
    journalState_ = PanelJournalState::Fault;
    lastError_ = EpdError::PersistedFault;
    // Reassert the safe locked pin state before refusing the operation. RTC
    // retention is guaranteed only while the RTC domain remains intact; EN or
    // full power loss may instead re-enter through the conservative Unknown
    // path.
    releaseBusHigh();
    return false;
  }

  refreshInProgress_ = true;
  lastError_ = EpdError::None;
  lastRefreshDurationMs_ = 0;

  pinMode(board::kEpdBusy, INPUT);
  const PanelJournalState previousState = readJournal();
  Serial.printf("EPD: journal=%s, startup BUSY=%s (diagnostic)\n",
                panelJournalStateName(previousState),
                digitalRead(board::kEpdBusy) == LOW ? "LOW" : "HIGH");

  // A retained hibernated state intentionally bypasses this wait: init() must
  // pulse RST to wake it.  PoweredOff likewise goes directly through full init.
  prepareInterruptedRefresh(previousState);

  for (uint8_t attempt = 0; attempt < 2; ++attempt) {
    const bool recoveryAttempt = attempt == 1;
    if (recoveryAttempt) {
      Serial.printf("EPD: one recovery attempt after %s\n",
                    epdErrorName(lastError_));
      setJournal(PanelJournalState::Recovering);
      shutdownAfterFailure();
      delay(50);
    }

    if (performRefresh(model, recoveryAttempt)) {
      refreshInProgress_ = false;
      lastError_ = EpdError::None;
      return true;
    }
  }

  const EpdError terminalError = lastError_;
  lockFault(terminalError);
  refreshInProgress_ = false;
  return false;
}

bool EpdController::prepareInterruptedRefresh(
    PanelJournalState previousState) {
  if (!uncertainState(previousState) ||
      digitalRead(board::kEpdBusy) != LOW) {
    return true;
  }

  Serial.println("EPD: interrupted/unknown refresh with BUSY LOW; waiting once");
  if (waitUntilReady(board::kEpdStageTimeoutMs)) return true;

  // BUSY LOW is not a permanent startup gate.  A full hardware reset/init is
  // still attempted below; this error is retained only if recovery fails.
  lastError_ = EpdError::BusyBeforeInit;
  Serial.println("EPD: pre-init BUSY wait expired; forcing full reset/init");
  return false;
}

bool EpdController::performRefresh(const app::UiModel& model,
                                   bool recoveryAttempt) {
  if (!initializePanel(recoveryAttempt)) return false;

  setJournal(PanelJournalState::Refreshing);
  const uint32_t startedAtMs = millis();

  gDisplay.setRotation(0);
  gDisplay.setFullWindow();
  gDisplay.firstPage();
  bool hasMorePages = false;
  do {
    renderer_.render(gDisplay, model);
    hasMorePages = gDisplay.nextPage();
    if (gDisplay.epd2.operationFailed()) {
      lastRefreshDurationMs_ = millis() - startedAtMs;
      lastError_ = EpdError::DriverOperationFailed;
      Serial.printf("EPD: safe driver stopped at %s after %lu ms\n",
                    gDisplay.epd2.failureStageName(),
                    static_cast<unsigned long>(lastRefreshDurationMs_));
      return false;
    }
  } while (hasMorePages);

  lastRefreshDurationMs_ = millis() - startedAtMs;

  if (!waitUntilReady(board::kEpdPostResetReadyMs)) {
    lastError_ = EpdError::BusyAfterRefresh;
    Serial.println("EPD: BUSY stayed LOW after refresh");
    return false;
  }

  // GxEPD2's full paged nextPage() already powers off.  Calling powerOff()
  // again is an idempotent guard and never places the controller in hibernate.
  gDisplay.powerOff();
  if (!waitUntilReady(board::kEpdPostResetReadyMs)) {
    lastError_ = EpdError::BusyAfterRefresh;
    return false;
  }

  setJournal(PanelJournalState::PoweredOff);
  releaseBusHigh();
  Serial.printf("EPD: full refresh complete in %lu ms; panel powered off\n",
                static_cast<unsigned long>(lastRefreshDurationMs_));
  return true;
}

bool EpdController::initializePanel(bool recoveryAttempt) {
  setJournal(recoveryAttempt ? PanelJournalState::Recovering
                             : PanelJournalState::Initializing);

  driveHigh(board::kEpdCs);
  driveHigh(board::kEpdReset);
  driveHigh(board::kEpdDc);
  pinMode(board::kEpdBusy, INPUT);

  // GPIO19 is DC, not MISO.  The panel is write-only.
  if (!SPI.begin(board::kEpdSck, -1, board::kEpdMosi, board::kEpdCs)) {
    lastError_ = EpdError::SpiBeginFailed;
    busActive_ = false;
    Serial.println("EPD: SPI.begin failed");
    return false;
  }
  busActive_ = true;
  gDisplay.epd2.selectSPI(
      SPI, SPISettings(board::kEpdSpiHz, MSBFIRST, SPI_MODE0));

  // initial=true deliberately performs a complete controller reset each time,
  // including direct wake from a retained Hibernated state.
  gDisplay.init(0, true, 10, false);
  gDisplay.setRotation(0);

  if (!waitUntilReady(board::kEpdPostResetReadyMs)) {
    lastError_ = EpdError::BusyAfterInit;
    Serial.println("EPD: BUSY did not return HIGH within 5 s after init");
    return false;
  }
  return true;
}

bool EpdController::waitUntilReady(uint32_t timeoutMs) const {
  const uint32_t startedAtMs = millis();
  while (digitalRead(board::kEpdBusy) == LOW) {
    if (millis() - startedAtMs >= timeoutMs) return false;
    delay(10);
    yield();
  }
  return true;
}

void EpdController::releaseBusHigh() {
  SPI.end();
  busActive_ = false;
  // Arduino-ESP32 3.3.10 detaches the SPI matrix but does not guarantee that
  // the GPIO direction becomes input.  Match the board-wide idle contract.
  pinMode(board::kEpdSck, INPUT);
  pinMode(board::kEpdMosi, INPUT);
  driveHigh(board::kEpdCs);
  driveHigh(board::kEpdReset);
  driveHigh(board::kEpdDc);
}

void EpdController::hardResetPanel() {
  driveHigh(board::kEpdCs);
  driveHigh(board::kEpdDc);
  driveLow(board::kEpdReset);
  delay(25);
  driveHigh(board::kEpdReset);
  delay(10);
}

void EpdController::shutdownAfterFailure() {
  if (!busActive_) {
    releaseBusHigh();
    return;
  }

  bool mustReset = digitalRead(board::kEpdBusy) == LOW;
  if (!mustReset) {
    // BUSY is ready, so make one bounded attempt to disable panel voltages.
    mustReset = !gDisplay.epd2.emergencyPowerOff();
  }

  if (mustReset) {
    // Pulse the controller reset to terminate the uncertain command state.
    // Whether this also guarantees high-voltage shutdown on a physically
    // failed BUSY path remains an explicit hardware-test item for P2.
    hardResetPanel();
  }
  releaseBusHigh();
}

void EpdController::setJournal(PanelJournalState state) {
  uint32_t sequence = validJournal(gRtcPanelJournal)
                          ? gRtcPanelJournal.sequence + 1U
                          : 1U;
  RtcPanelJournal next = {kJournalMagic, static_cast<uint32_t>(state),
                          sequence, 0};
  next.check = journalCheck(next);
  gRtcPanelJournal = next;
  journalState_ = state;
}

void EpdController::lockFault(EpdError error) {
  lastError_ = error == EpdError::None ? EpdError::BusyAfterRefresh : error;
  shutdownAfterFailure();
  setJournal(PanelJournalState::Fault);
  faultLocked_ = true;
  Serial.printf("EPD: FAULT LOCKED for this retained boot (%s)\n",
                epdErrorName(lastError_));
}

}  // namespace ui
