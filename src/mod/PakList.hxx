#pragma once

// This copy's paks (PakRules.hxx): found once as the mod starts, every one
// logged with its size and verdict, and the summary kept for every report
// (DesyncReporter) and match log line.

#include <string>

namespace sfve::mod {

// Once, as the mod starts (it reads folders, never game memory): finds
// every .pak the engine mounts, logs each, and keeps the census.
void TakePakCensus();

// The census's summary (paks::Summary); "not taken" before TakePakCensus.
// Any thread.
std::string PakCensusSummary();

} // namespace sfve::mod
