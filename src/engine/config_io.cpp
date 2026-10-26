// Copyright (C) 2026 nohuto
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "engine/config_io.h"

#include "platform/file_io.h"
#include "records/json.h"
#include "platform/text.h"

#include <win32ui/json/reader.h>
#include <win32ui/json/writer.h>

#include <set>
#include <span>

namespace thread_sim::engine
{

namespace
{

constexpr wchar_t kFormatName[] = L"thread-sim.run-config";
constexpr size_t kMaxCpuSetIds = 4096;

using win32ui::JsonWriter;

void WriteProcessor(JsonWriter& writer, std::wstring_view key, ProcessorNumber id)
{
    writer.BeginObject(key);
    writer.Unsigned(L"group", id.group);
    writer.Unsigned(L"number", id.number);
    writer.EndObject();
}

void WriteActivity(JsonWriter& writer, const ActivityConfig& activity)
{
    writer.BeginObject(L"activity");
    writer.Unsigned(L"work_percent", activity.work_percent);
    writer.Unsigned(L"period_ms", activity.period_ms);
    writer.String(L"waitable_timer_kind", WaitableTimerKindKey(activity.waitable_timer_kind));
    writer.EndObject();
}
void WriteThread(JsonWriter& writer, const ThreadConfig& thread)
{
    writer.BeginObject();
    writer.String(L"name", thread.name);
    writer.Bool(L"active", thread.active);
    writer.Bool(L"shared_mutex", thread.uses_shared_mutex);
    writer.BeginObject(L"scheduling");
    writer.String(L"relative_priority", RelativeThreadPriorityKey(thread.scheduling.relative_priority));
    writer.Bool(L"priority_boost_enabled", thread.scheduling.priority_boost_enabled);
    writer.EndObject();
    writer.BeginObject(L"affinity");
    writer.String(L"mode", AffinityModeKey(thread.affinity.mode));
    writer.Unsigned(L"group_affinity_group", thread.affinity.group_affinity.group);
    writer.BeginArray(L"group_affinity_processors");
    for (uint32_t bit = 0; bit < 64; ++bit)
    {
        if (thread.affinity.group_affinity.mask & (1ull << bit))
        {
            writer.Unsigned({}, bit);
        }
    }
    writer.EndArray();
    writer.BeginArray(L"selected_cpu_set_ids");
    for (uint32_t id : thread.affinity.selected_cpu_set_ids)
    {
        writer.Unsigned({}, id);
    }
    writer.EndArray();
    if (thread.affinity.ideal_processor_enabled)
    {
        WriteProcessor(writer, L"ideal_processor", thread.affinity.ideal_processor);
    }
    else
    {
        writer.Null(L"ideal_processor");
    }
    writer.EndObject();
    WriteActivity(writer, thread.activity);
    writer.EndObject();
}

// missing keys take their defaults and produce warnings
std::span<const std::wstring_view> RequiredKeys(std::wstring_view path)
{
    if (path == L"document")
    {
        static constexpr std::wstring_view keys[] = {L"format", L"version", L"name", L"duration_s", L"sample_interval_ms", L"threads"};
        return keys;
    }
    if (path == L"processor")
    {
        static constexpr std::wstring_view keys[] = {L"group", L"number"};
        return keys;
    }
    if (path == L"thread")
    {
        static constexpr std::wstring_view keys[] = {L"name", L"active", L"shared_mutex", L"scheduling", L"affinity", L"activity"};
        return keys;
    }
    if (path == L"scheduling")
    {
        static constexpr std::wstring_view keys[] = {L"relative_priority", L"priority_boost_enabled"};
        return keys;
    }
    if (path == L"affinity")
    {
        static constexpr std::wstring_view keys[] = {L"mode", L"group_affinity_group", L"group_affinity_processors", L"selected_cpu_set_ids", L"ideal_processor"};
        return keys;
    }
    if (path == L"activity")
    {
        static constexpr std::wstring_view keys[] = {L"work_percent", L"period_ms", L"waitable_timer_kind"};
        return keys;
    }
    return {};
}

class ConfigParser
{
  public:
    ConfigParser(std::wstring_view text, std::wstring* error)
        : reader_(text, json::Errors(error))
    {
    }

    bool Parse(LoadedConfig* loaded)
    {
        // read version first, JSON keys can appear in any order
        win32ui::JsonReader header = reader_;
        if (!header.Object([&](const std::wstring& key) {
                if (key != L"version")
                {
                    return header.Skip();
                }
                uint64_t version = 0;
                if (!header.Unsigned(&version))
                {
                    return false;
                }
                if (version != kConfigFormatVersion)
                {
                    return header.Fail(version >= 1 && version <= 3 ? L"This config was saved by an older version of ThreadSim." : L"Unsupported config version.");
                }
                return true;
            }))
        {
            return false;
        }

        loaded_ = loaded;
        bool format_seen = false;
        bool version_seen = false;
        RunConfig& run = loaded->config;
        run = RunConfig{};
        const bool ok = Object(L"document", [&](const std::wstring& key, bool* handled) {
            *handled = true;
            if (key == L"format")
            {
                std::wstring format;
                if (!reader_.String(&format, 64))
                {
                    return false;
                }
                if (format != kFormatName)
                {
                    return reader_.Fail(L"This is not a ThreadSim run config.");
                }
                format_seen = true;
                return true;
            }
            if (key == L"version")
            {
                uint64_t version = 0;
                if (!reader_.Unsigned(&version))
                {
                    return false;
                }
                version_seen = true;
                return true;
            }
            if (key == L"name")
            {
                return reader_.String(&run.name, 256);
            }
            if (key == L"duration_s")
            {
                if (reader_.Next() == L'n')
                {
                    run.duration_s.reset();
                    return reader_.Skip();
                }
                uint32_t duration = 0;
                if (!U32(&duration))
                {
                    return false;
                }
                run.duration_s = duration;
                return true;
            }
            if (key == L"sample_interval_ms")
            {
                return U32(&run.sample_interval_ms);
            }
            if (key == L"threads")
            {
                return reader_.Array([&] {
                    if (run.threads.size() >= kMaxThreads)
                    {
                        return reader_.Fail(L"The config has more threads than ThreadSim supports.");
                    }
                    run.threads.emplace_back();
                    return Thread(&run.threads.back());
                });
            }
            if (key == L"saved_topology")
            {
                TopologyFingerprint fingerprint;
                if (!Fingerprint(&fingerprint))
                {
                    return false;
                }
                loaded->saved_topology = std::move(fingerprint);
                return true;
            }
            *handled = false;
            return true;
        });
        if (!ok || !reader_.End())
        {
            return false;
        }
        if (!format_seen || !version_seen)
        {
            return reader_.Fail(L"The config is missing its format or version.");
        }
        return true;
    }

  private:
    template <typename Handler>
    bool Object(std::wstring path, Handler&& handler)
    {
        if (reader_.Next() != L'{')
        {
            return reader_.Fail((L"An object was expected for " + path + L".").c_str());
        }
        std::set<std::wstring> seen;
        const bool ok = reader_.Object([&](const std::wstring& key) {
            if (!seen.insert(key).second)
            {
                return reader_.Fail((L"Duplicate key \"" + key + L"\" in " + path + L".").c_str());
            }
            bool handled = false;
            if (!handler(key, &handled))
            {
                return false;
            }
            if (!handled)
            {
                loaded_->warnings.push_back(L"Ignored unknown key \"" + key + L"\" in " + path + L".");
                return reader_.Skip();
            }
            return true;
        });
        if (ok)
        {
            for (std::wstring_view key : RequiredKeys(path))
            {
                if (!seen.contains(std::wstring(key)))
                {
                    loaded_->warnings.push_back(L"Key \"" + std::wstring(key) + L"\" is missing in " + path + L". The default value was used.");
                }
            }
        }
        return ok;
    }

    bool U32(uint32_t* out)
    {
        uint64_t value = 0;
        if (!reader_.Unsigned(&value))
        {
            return false;
        }
        if (value > UINT32_MAX)
        {
            return reader_.Fail(L"A number is out of range.");
        }
        *out = static_cast<uint32_t>(value);
        return true;
    }

    bool U16(uint16_t* out)
    {
        uint32_t value = 0;
        if (!U32(&value))
        {
            return false;
        }
        if (value > UINT16_MAX)
        {
            return reader_.Fail(L"A number is out of range.");
        }
        *out = static_cast<uint16_t>(value);
        return true;
    }

    bool U8(uint8_t* out)
    {
        uint32_t value = 0;
        if (!U32(&value))
        {
            return false;
        }
        if (value > UINT8_MAX)
        {
            return reader_.Fail(L"A number is out of range.");
        }
        *out = static_cast<uint8_t>(value);
        return true;
    }

    template <typename Enum>
    bool Key(Enum* out, bool (*parse)(std::wstring_view, Enum*), const wchar_t* what)
    {
        std::wstring key;
        if (!reader_.String(&key, 64))
        {
            return false;
        }
        if (!parse(key, out))
        {
            return reader_.Fail((L"Unknown " + std::wstring(what) + L" \"" + key + L"\".").c_str());
        }
        return true;
    }

    bool Processor(ProcessorNumber* out)
    {
        return Object(L"processor", [&](const std::wstring& key, bool* handled) {
            *handled = true;
            if (key == L"group")
            {
                return U16(&out->group);
            }
            if (key == L"number")
            {
                return U8(&out->number);
            }
            *handled = false;
            return true;
        });
    }

    bool Activity(ActivityConfig* out)
    {
        return Object(L"activity", [&](const std::wstring& key, bool* handled) {
            *handled = true;
            if (key == L"work_percent")
            {
                return U32(&out->work_percent);
            }
            if (key == L"period_ms")
            {
                return U32(&out->period_ms);
            }
            if (key == L"waitable_timer_kind")
            {
                return Key(&out->waitable_timer_kind, &ParseWaitableTimerKindKey, L"waitable timer kind");
            }
            *handled = false;
            return true;
        });
    }

    bool ThreadAffinity(ThreadAffinityConfig* out)
    {
        return Object(L"affinity", [&](const std::wstring& key, bool* handled) {
            *handled = true;
            if (key == L"mode")
            {
                return Key(&out->mode, &ParseAffinityModeKey, L"affinity mode");
            }
            if (key == L"group_affinity_group")
            {
                return U16(&out->group_affinity.group);
            }
            if (key == L"group_affinity_processors")
            {
                out->group_affinity.mask = 0;
                return reader_.Array([&] {
                    uint8_t number = 0;
                    if (!U8(&number))
                    {
                        return false;
                    }
                    if (number >= 64)
                    {
                        return reader_.Fail(L"A group-relative processor number must be below 64.");
                    }
                    out->group_affinity.mask |= 1ull << number;
                    return true;
                });
            }
            if (key == L"selected_cpu_set_ids")
            {
                out->selected_cpu_set_ids.clear();
                return reader_.Array([&] {
                    if (out->selected_cpu_set_ids.size() >= kMaxCpuSetIds)
                    {
                        return reader_.Fail(L"Too many CPU Set IDs.");
                    }
                    uint32_t id = 0;
                    if (!U32(&id))
                    {
                        return false;
                    }
                    out->selected_cpu_set_ids.push_back(id);
                    return true;
                });
            }
            if (key == L"ideal_processor")
            {
                if (reader_.Next() == L'n')
                {
                    out->ideal_processor_enabled = false;
                    return reader_.Skip();
                }
                out->ideal_processor_enabled = true;
                return Processor(&out->ideal_processor);
            }
            *handled = false;
            return true;
        });
    }

    bool Thread(ThreadConfig* out)
    {
        const bool ok = Object(L"thread", [&](const std::wstring& key, bool* handled) {
            *handled = true;
            if (key == L"name")
            {
                return reader_.String(&out->name, 256);
            }
            if (key == L"active")
            {
                return reader_.Bool(&out->active);
            }
            if (key == L"shared_mutex")
            {
                return reader_.Bool(&out->uses_shared_mutex);
            }
            if (key == L"scheduling")
            {
                return Object(L"scheduling", [&](const std::wstring& inner, bool* inner_handled) {
                    *inner_handled = true;
                    if (inner == L"relative_priority")
                    {
                        return Key(&out->scheduling.relative_priority, &ParseRelativeThreadPriorityKey, L"relative priority");
                    }
                    if (inner == L"priority_boost_enabled")
                    {
                        return reader_.Bool(&out->scheduling.priority_boost_enabled);
                    }
                    *inner_handled = false;
                    return true;
                });
            }
            if (key == L"affinity")
            {
                return ThreadAffinity(&out->affinity);
            }
            if (key == L"activity")
            {
                return Activity(&out->activity);
            }
            *handled = false;
            return true;
        });
        return ok;
    }

    bool Fingerprint(TopologyFingerprint* out)
    {
        return Object(L"saved_topology", [&](const std::wstring& key, bool* handled) {
            *handled = true;
            if (key == L"group_masks")
            {
                return reader_.Array([&] {
                    if (out->group_masks.size() >= 1024)
                    {
                        return reader_.Fail(L"Too many processor groups.");
                    }
                    std::wstring text;
                    if (!reader_.String(&text, 256))
                    {
                        return false;
                    }
                    uint64_t mask = 0;
                    if (!text.empty())
                    {
                        auto parsed = platform::ParseMaskList(text, 64);
                        if (!parsed)
                        {
                            return reader_.Fail(L"A saved group mask is malformed.");
                        }
                        mask = parsed.value();
                    }
                    out->group_masks.push_back(mask);
                    return true;
                });
            }
            if (key == L"cpu_set_count")
            {
                return U32(&out->cpu_set_count);
            }
            if (key == L"cpu_set_hash")
            {
                std::wstring text;
                if (!reader_.String(&text, 32))
                {
                    return false;
                }
                wchar_t* end = nullptr;
                out->cpu_set_hash = text.size() > 2 && text[0] == L'0' && text[1] == L'x' ? wcstoull(text.c_str() + 2, &end, 16) : 0;
                if (!end || *end != L'\0')
                {
                    return reader_.Fail(L"The saved CPU Set hash is malformed.");
                }
                return true;
            }
            *handled = false;
            return true;
        });
    }

    win32ui::JsonReader reader_;
    LoadedConfig* loaded_ = nullptr;
};

} // namespace

TopologyFingerprint FingerprintOf(const platform::Topology& topology)
{
    TopologyFingerprint fingerprint;
    for (const auto& group : topology.groups)
    {
        fingerprint.group_masks.push_back(group.active_processor_mask);
    }
    fingerprint.cpu_set_count = static_cast<uint32_t>(topology.cpu_sets.size());
    uint64_t hash = 1469598103934665603ull;
    auto mix = [&](uint64_t value) {
        for (int byte = 0; byte < 8; ++byte)
        {
            hash = (hash ^ ((value >> (byte * 8)) & 0xFF)) * 1099511628211ull;
        }
    };
    for (const auto& set : topology.cpu_sets)
    {
        mix(set.id);
        mix((static_cast<uint64_t>(set.group) << 8) | set.logical_processor_index);
    }
    fingerprint.cpu_set_hash = topology.cpu_sets.empty() ? 0 : hash;
    return fingerprint;
}

std::wstring SerializeRunConfig(const RunConfig& config, const platform::Topology& topology)
{
    JsonWriter writer;
    WriteRunConfigObject(writer, {}, config, topology);
    return writer.Take();
}

void WriteRunConfigObject(win32ui::JsonWriter& writer, std::wstring_view key, const RunConfig& config, const platform::Topology& topology)
{
    writer.BeginObject(key);
    writer.String(L"format", kFormatName);
    writer.Unsigned(L"version", kConfigFormatVersion);
    writer.String(L"name", config.name);
    config.duration_s ? writer.Unsigned(L"duration_s", *config.duration_s) : writer.Null(L"duration_s");
    writer.Unsigned(L"sample_interval_ms", config.sample_interval_ms);
    writer.BeginArray(L"threads");
    for (const auto& thread : config.threads)
    {
        WriteThread(writer, thread);
    }
    writer.EndArray();
    const TopologyFingerprint fingerprint = FingerprintOf(topology);
    writer.BeginObject(L"saved_topology");
    writer.BeginArray(L"group_masks");
    for (uint64_t mask : fingerprint.group_masks)
    {
        writer.String({}, platform::FormatMaskList(mask));
    }
    writer.EndArray();
    writer.Unsigned(L"cpu_set_count", fingerprint.cpu_set_count);
    writer.String(L"cpu_set_hash", platform::FormatHex(fingerprint.cpu_set_hash, 16));
    writer.EndObject();
    writer.EndObject();
}

platform::Result<LoadedConfig> ParseRunConfig(std::wstring_view text)
{
    std::wstring error;
    LoadedConfig loaded;
    ConfigParser parser(text, &error);
    if (!parser.Parse(&loaded))
    {
        return platform::FormatError(L"Read config", error.empty() ? L"The config is not valid." : error);
    }
    return loaded;
}

platform::Status SaveRunConfig(const std::wstring& path, const RunConfig& config, const platform::Topology& topology)
{
    return platform::WriteUtf8FileAtomic(path, SerializeRunConfig(config, topology));
}

platform::Result<LoadedConfig> LoadRunConfig(const std::wstring& path)
{
    auto text = platform::ReadUtf8File(path);
    if (!text)
    {
        return std::move(text).error();
    }
    auto loaded = ParseRunConfig(text.value());
    if (!loaded)
    {
        platform::Error error = std::move(loaded).error();
        error.context = path;
        return error;
    }
    return loaded;
}

} // namespace thread_sim::engine
