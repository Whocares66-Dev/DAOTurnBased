#include "game/SettingsFile.h"

#include "dao/Log.h"

#include <fstream>
#include <iterator>
#include <optional>
#include <sstream>
#include <string>
#include <system_error>

namespace tb::game
{

namespace
{

std::optional<std::filesystem::file_time_type> g_stamp;

std::optional<std::string> ReadAll(const std::filesystem::path &path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in)
    {
        return std::nullopt;
    }
    return std::string{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

bool WriteAll(const std::filesystem::path &path, const std::string &text)
{
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << text;
    return static_cast<bool>(out);
}

std::optional<std::filesystem::file_time_type> Stamp()
{
    std::error_code error;
    const auto stamp = std::filesystem::last_write_time(SettingsPath(), error);
    if (error)
    {
        return std::nullopt;
    }
    return stamp;
}

} // namespace

std::filesystem::path SettingsPath()
{
    return dao::LogDirectory() / "turnbased.ini";
}

Settings LoadSettings()
{
    const auto path = SettingsPath();
    auto text = ReadAll(path);
    if (!text)
    {
        text = DefaultSettingsFile();
        if (WriteAll(path, *text))
        {
            spdlog::info("wrote the default settings to {}", path.string());
        }
        else
        {
            spdlog::warn("could not write the default settings to {}; the defaults are used", path.string());
        }
    }
    else if (const std::string whole = WithMissingSettings(*text); whole != *text)
    {
        // Written by an older build: the newer settings are added, each at
        // its default with its comment, the player's own values kept.
        if (WriteAll(path, whole))
        {
            spdlog::info("added the settings this build has and {} lacked", path.filename().string());
            text = whole;
        }
    }
    const ParsedSettings parsed = ParseSettings(*text);
    for (const std::string &warning : parsed.warnings)
    {
        spdlog::warn("{}: {}", path.filename().string(), warning);
    }
    g_stamp = Stamp();
    return parsed.settings;
}

void SaveEnabled(bool enabled)
{
    const auto path = SettingsPath();
    const std::string text = ReadAll(path).value_or(DefaultSettingsFile());
    if (!WriteAll(path, WithValue(text, "turnbased", "enabled", enabled ? "true" : "false")))
    {
        spdlog::warn("could not write {}", path.string());
    }
    // Our own write is not the player's edit.
    g_stamp = Stamp();
}

std::string SettingsText()
{
    return ReadAll(SettingsPath()).value_or(DefaultSettingsFile());
}

std::vector<std::string> SaveSetting(std::string_view section, std::string_view key, std::string_view value)
{
    const auto path = SettingsPath();
    const std::string text = WithValue(SettingsText(), section, key, value);
    if (!WriteAll(path, text))
    {
        return {std::format("could not write {}", path.string())};
    }
    return ParseSettings(text).warnings;
}

bool SettingsFileChanged()
{
    const auto stamp = Stamp();
    if (!stamp || stamp == g_stamp)
    {
        return false;
    }
    g_stamp = stamp;
    return true;
}

} // namespace tb::game
