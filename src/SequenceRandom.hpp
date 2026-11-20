#pragma once

#include <cstddef>
#include <random>
#include <vector>

namespace stoutwall {

// Matches the original sequence generator: draw with replacement, rejecting
// only an immediate repeat. A one-record source is returned once instead of
// reproducing the original retry loop that could never terminate.
template <class Engine>
std::vector<size_t> BuildRandomSequenceIndices(size_t sourceCount, size_t length, Engine& engine,
                                               size_t pinnedPosition = static_cast<size_t>(-1),
                                               size_t pinnedIndex = static_cast<size_t>(-1)) {
    std::vector<size_t> sequence;
    if (!sourceCount || !length) return sequence;
    const bool pin = pinnedPosition < length && pinnedIndex < sourceCount;
    sequence.reserve(length);
    std::uniform_int_distribution<size_t> choose(0, sourceCount - 1);
    const auto chooseDifferent = [&](size_t excluded) {
        size_t index = choose(engine);
        if (sourceCount > 1) {
            while (index == excluded) index = choose(engine);
        }
        return index;
    };
    sequence.resize(length);
    if (!pin) {
        for (size_t i = 0; i < length; ++i)
            sequence[i] = i && sourceCount > 1 ? chooseDifferent(sequence[i - 1]) : choose(engine);
        return sequence;
    }

    sequence[pinnedPosition] = pinnedIndex;
    for (size_t i = pinnedPosition + 1; i < length; ++i)
        sequence[i] = sourceCount > 1 ? chooseDifferent(sequence[i - 1]) : choose(engine);
    for (size_t i = pinnedPosition; i > 0; --i)
        sequence[i - 1] = sourceCount > 1 ? chooseDifferent(sequence[i]) : choose(engine);
    return sequence;
}

template <class Engine>
size_t ChooseRandomSequenceIndex(size_t count, Engine& engine) {
    if (!count) return 0;
    std::uniform_int_distribution<size_t> choose(0, count - 1);
    return choose(engine);
}

} // namespace stoutwall
