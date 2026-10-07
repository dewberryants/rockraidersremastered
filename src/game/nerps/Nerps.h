#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace rr::nerps {

// The level script runtime (NERPsRuntime_*). A compiled script (.npl) is a flat list of 32-bit
// words; every frame the whole list is evaluated from the top:
//   0x20000 | index   call a function from the table (value or condition-guarded action)
//   0x10000 | op      operator: 0 end of value, 1 not, 2 or, 5..10 comparisons
//   0x40000 | n       label: clears the condition
//   0x80000 | target  jump to target when the condition holds
//   other             16-bit literal
// Values fold into a running condition; actions only run while it is non-zero; Stop ends the
// pass. Functions the runtime does not handle itself go to `call`.
class Runtime {
public:
    bool LoadScript(const std::string& file);
    // Message lines for SetMessage (1-based; '_' is a space, ':' lines name images).
    bool LoadMessages(const std::string& file);
    bool loaded() const { return !script_.empty(); }
    const std::string* MessageLine(int index) const;

    // Game functions: (table index, arguments) -> value. Unknown ones return 0.
    std::function<int(int index, const int* args)> call;

    void Execute();
    // Timers count milliseconds of game time (elapsed units x 40).
    void UpdateTimers(float elapsed);

    // State the game side reads or sets.
    std::array<int, 8> registers{};
    std::array<float, 4> timers{};
    float messageTimer = 0.0f;  // ms left on the current message
    int messagePermit = 1;  // despite the name: non-zero blocks SetMessage
    int objectiveSwitch = 0;  // set when the briefing closes; reading it clears it
    int tutorialFlags = 0, trainFlags = 0;

private:
    int Invoke(int index, const int* args);
    int LoadArgument(uint32_t word);

    std::vector<uint32_t> script_;
    std::vector<std::string> messages_;
};

}  // namespace rr::nerps
