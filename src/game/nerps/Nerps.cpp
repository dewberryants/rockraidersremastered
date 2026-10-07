#include "game/nerps/Nerps.h"

#include <cmath>
#include <cstring>

#include "core/Log.h"
#include "core/Random.h"
#include "fs/FileSystem.h"
#include "game/nerps/NerpsFunctions.h"

namespace rr::nerps {

namespace {
constexpr uint32_t kCall = 0x20000, kOperator = 0x10000, kLabel = 0x40000, kJump = 0x80000;

int IndexOf(const char* name) {
    for (int i = 0; i < kFunctionCount; ++i)
        if (std::strcmp(kFunctions[i].name, name) == 0) return i;
    return -1;
}
}  // namespace

bool Runtime::LoadScript(const std::string& file) {
    script_.clear();
    registers.fill(0);
    timers.fill(0.0f);
    messageTimer = 0.0f;
    messagePermit = 1;
    objectiveSwitch = 0;
    if (file.empty()) return false;
    const auto data = Fs().ReadAll(file);
    if (!data || data->size() < 4) return false;
    script_.resize(data->size() / 4);
    std::memcpy(script_.data(), data->data(), script_.size() * 4);
    return true;
}

bool Runtime::LoadMessages(const std::string& file) {
    messages_.clear();
    if (file.empty()) return false;
    const auto loaded = Fs().ReadText(file);
    if (!loaded) return false;
    const std::string& text = *loaded;
    std::string line;
    auto flush = [&]() {
        // TODO(nerps): ':image path' lines attach pictures to the message panel.
        if (!line.empty() && line[0] != ':') messages_.push_back(line);
        line.clear();
    };
    for (char c : text) {
        if (static_cast<unsigned char>(c) < 0x20) {
            if (c == '\n') flush();
            continue;
        }
        line += c == '_' ? ' ' : c;
    }
    flush();
    return true;
}

const std::string* Runtime::MessageLine(int index) const {
    if (index > 0) --index;  // scripts count from 1; 0 means the first line too
    return index >= 0 && index < int(messages_.size()) ? &messages_[size_t(index)] : nullptr;
}

void Runtime::UpdateTimers(float elapsed) {
    const float ms = elapsed * 1000.0f * 0.04f;
    for (float& t : timers) t += ms;
    // TODO(nerps): messages held until acknowledged (message permit / advisor ends).
    if (messageTimer > 0.0f) messageTimer = std::max(0.0f, messageTimer - ms);
}

int Runtime::LoadArgument(uint32_t word) {
    // NERPsRuntime_LoadLiteral: an argument may itself be a value function without arguments.
    if ((word & 0xffff0000u) == kCall) {
        const int index = int(word & 0xffff);
        if (index < kFunctionCount && kFunctions[index].kind == 0) return Invoke(index, nullptr);
    }
    return int(word);
}

int Runtime::Invoke(int index, const int* args) {
    static const int kGetR0 = IndexOf("GetR0"), kSetR0 = IndexOf("SetR0"), kAddR0 = IndexOf("AddR0"),
                     kSubR0 = IndexOf("SubR0"), kGetTimer0 = IndexOf("GetTimer0"), kSetTimer0 = IndexOf("SetTimer0");
    if (index >= kGetR0 && index < kGetR0 + 8) return registers[size_t(index - kGetR0)];
    if (index >= kSetR0 && index < kSetR0 + 8) return registers[size_t(index - kSetR0)] = args[0];
    if (index >= kAddR0 && index < kAddR0 + 8) return registers[size_t(index - kAddR0)] += args[0];
    if (index >= kSubR0 && index < kSubR0 + 8) return registers[size_t(index - kSubR0)] -= args[0];
    if (index >= kGetTimer0 && index < kGetTimer0 + 4) return int(std::lround(timers[size_t(index - kGetTimer0)]));
    if (index >= kSetTimer0 && index < kSetTimer0 + 4) {
        timers[size_t(index - kSetTimer0)] = float(uint32_t(args[0]));
        return 1;
    }
    const char* name = kFunctions[index].name;
    if (!std::strcmp(name, "True")) return 1;
    if (!std::strcmp(name, "False") || !std::strcmp(name, "Null")) return 0;
    if (!std::strcmp(name, "GetRandom")) return random::Rand() & 0xfff;
    if (!std::strcmp(name, "GetRandom10")) return random::Rand() % 10;
    if (!std::strcmp(name, "GetRandom100")) return random::Rand() % 100;
    if (!std::strcmp(name, "GetRandomTrueFalse")) return random::Rand() & 1;
    if (!std::strcmp(name, "GetMessageTimer")) return int(std::lround(messageTimer));
    if (!std::strcmp(name, "SetMessagePermit")) return messagePermit = args[0];
    if (!std::strcmp(name, "GetObjectiveSwitch")) {
        const int v = objectiveSwitch ? 1 : 0;
        objectiveSwitch = 0;
        return v;
    }
    if (!std::strcmp(name, "SetObjectiveSwitch")) return objectiveSwitch = args[0];
    if (!std::strcmp(name, "GetTutorialFlags")) return tutorialFlags;
    if (!std::strcmp(name, "SetTutorialFlags")) return tutorialFlags = args[0];
    if (!std::strcmp(name, "GetTrainFlags")) return trainFlags;
    if (!std::strcmp(name, "SetTrainFlags")) return trainFlags = args[0];
    return call ? call(index, args) : 0;
}

void Runtime::Execute() {
    int acc = 0;     // the running condition / value
    int op = 0;      // pending combination: 0 none, 1 and, 2 or, 3..8 comparisons
    bool negate = false;
    auto reset = [&]() {
        acc = 0;
        op = 0;
        negate = false;
    };
    // Folds a value into the condition (comparisons keep the game's operand order).
    auto fold = [&](int v) {
        if (negate) v = v == 0;
        int r = v;
        switch (op) {
        case 0: r = v; break;
        case 1: r = (acc != 0 && v != 0) ? 1 : 0; break;
        case 2: r = (acc != 0 || v != 0) ? 1 : 0; break;
        case 3: r = uint32_t(v) < uint32_t(acc); break;
        case 4: r = uint32_t(acc) < uint32_t(v); break;
        case 5: r = acc == v; break;
        case 6: r = uint32_t(v) <= uint32_t(acc); break;
        case 7: r = uint32_t(acc) <= uint32_t(v); break;
        case 8: r = acc != v; break;
        default: r = acc; break;
        }
        acc = r;
        negate = false;
        op = 1;  // values in a row are and-ed
    };

    const size_t n = script_.size();
    for (size_t pc = 0; pc < n; ++pc) {
        const uint32_t w = script_[pc];
        if (w & kCall) {
            const int index = int(w & 0xffff);
            if (index == 0 && (acc != 0 || op == 0)) break;  // Stop
            if (index >= kFunctionCount) continue;
            const int kind = kFunctions[index].kind;
            int args[3] = {};
            if (kind <= 2) {
                for (int i = 0; i < kind && pc + 1 + size_t(i) < n; ++i) args[i] = LoadArgument(script_[pc + 1 + size_t(i)]);
                pc += size_t(kind);
                fold(Invoke(index, args));
            } else {
                const int argc = kind - 3;
                if (acc != 0) {
                    for (int i = 0; i < argc && pc + 1 + size_t(i) < n; ++i)
                        args[i] = LoadArgument(script_[pc + 1 + size_t(i)]);
                    Invoke(index, args);
                }
                pc += size_t(argc);
                reset();
            }
        } else if (w & kOperator) {
            negate = false;
            switch (w & 0xffff) {
            case 0: op = 1; break;  // end of a value: keeps the condition, ands what follows
            case 1: negate = true; break;
            case 2: op = 2; break;
            case 5: op = 3; break;
            case 6: op = 4; break;
            case 7: op = 5; break;
            case 8: op = 6; break;
            case 9: op = 7; break;
            case 10: op = 8; break;
            default: break;
            }
        } else if (w & kLabel) {
            reset();
        } else if (w & kJump) {
            if (acc != 0) pc = size_t(w & 0xffff);  // continues after the label it lands on
            reset();
        } else {
            fold(int(int16_t(w & 0xffff)));
        }
    }
    // TODO(nerps): camera follow/zoom/rotate requests (NERPsRuntime_EndExecute).
}

}  // namespace rr::nerps
