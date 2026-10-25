#pragma once
#include <cstdint>
// Versioned read-only diagnostic snapshot. An external process writes the disk
// journal; rendering updates this small shared-memory record only.
struct DlssJournal {
 uint32_t abi{},size{},pid{},sequence{};
 uint64_t tick{},frame{},completedFrame{},evaluations[2]{},eyeFrames[2]{};
 uint32_t phase{},thread{},deviceStatus{},pendingQueries{},trialPairs{},pairLimit{};
 uint32_t inputWidth{},inputHeight{},outputWidth{},outputHeight{};
};
