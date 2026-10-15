// Melodyverse core — Standard MIDI File writer (type 1, GM), used by the plugin and tests.
#pragma once

#include "Score.h"

#include <cstdint>
#include <string>
#include <vector>

namespace mv
{

// Encode `score` (or only the named tracks) as SMF bytes. Pitch bends use a ±12 semitone range set via RPN.
std::vector<uint8_t> writeMidi (const Score& score, const std::vector<std::string>& onlyTracks = {}, int ticksPerBeat = 480);

} // namespace mv
