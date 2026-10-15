#include "Styles.h"

#include "Theory.h"

#include <stdexcept>

namespace mv
{
namespace
{
#include "StylesData.inc"
} // namespace

namespace data
{
const std::vector<std::pair<std::string, std::vector<int>>>& scales() { return kScales; }
const std::vector<std::tuple<std::string, std::vector<int>, std::string>>& chordQualities() { return kChordQualities; }
const std::vector<std::pair<std::string, std::string>>& aliases() { return kAliases; }
} // namespace data

const std::vector<Style>& styles() { return kStyles; }

const std::vector<FamousProgression>& famousProgressions() { return kFamous; }

int styleIndex (const std::string& name)
{
    for (size_t i = 0; i < kStyles.size(); ++i)
        if (kStyles[i].name == name) return (int) i;
    return -1;
}

const Style& getStyle (const std::string& nameOrAlias)
{
    std::string key = toLower (nameOrAlias);
    for (auto& c : key)
        if (c == '-' || c == ' ') c = '_';
    for (auto& [alias, target] : kAliases)
        if (alias == key) { key = target; break; }
    int i = styleIndex (key);
    if (i < 0)
        throw std::out_of_range ("unknown style: " + nameOrAlias);
    return kStyles[(size_t) i];
}

} // namespace mv
