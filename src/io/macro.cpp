// SPDX-License-Identifier: MIT
#include "io/macro.hpp"

#include <fstream>
#include <sstream>

namespace gd {

bool Macro::save(const std::string& path) const {
  if (fps != phys::TPS) return false;
  std::ofstream out(path);
  if (!out) return false;
  out << "# gdlearn macro v2 native-240-tps\n";
  out << "level " << level << "\n";
  out << "fps " << phys::TPS << "\n";
  out << "progress " << progress << "\n";
  out << "frames " << holds.size() << "\n";
  size_t i = 0;
  while (i < holds.size()) {
    size_t j = i;
    while (j < holds.size() && holds[j] == holds[i]) ++j;
    out << "r " << static_cast<int>(holds[i]) << " " << (j - i) << "\n";
    i = j;
  }
  return true;
}

bool Macro::load(const std::string& path, Macro* out) {
  std::ifstream in(path);
  if (!in) return false;
  *out = Macro();
  // Force the file to declare its timing. A missing field is legacy/unknown.
  out->fps = 0;
  bool sawFps = false;
  std::string line;
  while (std::getline(in, line)) {
    if (line.empty() || line[0] == '#') continue;
    std::istringstream ss(line);
    std::string tag;
    ss >> tag;
    if (tag == "level") {
      std::getline(ss, out->level);
      if (!out->level.empty() && out->level[0] == ' ') out->level.erase(0, 1);
    } else if (tag == "fps") {
      ss >> out->fps;
      sawFps = true;
    } else if (tag == "progress") {
      ss >> out->progress;
    } else if (tag == "r") {
      int state = 0;
      size_t count = 0;
      ss >> state >> count;
      if (!ss || count > 100000000u) return false;
      out->holds.insert(out->holds.end(), count,
                        static_cast<uint8_t>(state ? 1 : 0));
    }
  }
  // Never allow a 60-Hz teacher/checkpoint companion to contaminate native
  // 240-TPS training. It must be regenerated and replay-verified instead.
  if (!sawFps || out->fps != phys::TPS) {
    out->holds.clear();
    return false;
  }
  return !out->holds.empty();
}

std::string Macro::summary(int maxEvents) const {
  std::ostringstream os;
  int events = 0;
  uint8_t prev = 0;
  for (size_t i = 0; i < holds.size() && events < maxEvents; ++i) {
    if (holds[i] != prev) {
      os << (holds[i] ? "+" : "-") << i << " ";
      prev = holds[i];
      events++;
    }
  }
  if (events >= maxEvents) os << "...";
  return os.str();
}

}  // namespace gd
