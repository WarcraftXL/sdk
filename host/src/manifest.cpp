// SPDX-License-Identifier: MIT
// Copyright (c) 2026 WarcraftXL

#include "wxl/host/manifest.hpp"

#include <cstdio>
#include <fstream>
#include <limits>
#include <sstream>

#include "wxl/abi.h"
#include "wxl/host/json.hpp"

namespace wxl::host
{
    const std::string* Manifest::BinaryFor(std::string_view target) const
    {
        for (const Binary& binary : binaries)
            if (binary.target == target) return &binary.path;
        return nullptr;
    }

    const char* CurrentTarget()
    {
#if defined(_WIN64)
        return "win-x64";
#elif defined(_WIN32)
        return "win-x86";
#elif defined(__linux__) && defined(__x86_64__)
        return "linux-x64";
#else
    #error "wxl-host: no manifest target name for this platform"
#endif
    }

    bool IsKnownTarget(std::string_view target)
    {
        return target == "win-x86" || target == "win-x64" || target == "linux-x64";
    }

    namespace
    {
        bool IsIdChar(char c) { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'); }
        bool IsDigit(char c) { return c >= '0' && c <= '9'; }

        bool IsAllDigits(std::string_view text)
        {
            if (text.empty()) return false;
            for (char c : text)
                if (!IsDigit(c)) return false;
            return true;
        }

        /// "0.5": an abi written before the three-part numbering.
        bool IsTwoPartAbi(std::string_view text)
        {
            const size_t dot = text.find('.');
            return dot != std::string_view::npos && IsAllDigits(text.substr(0, dot)) &&
                   IsAllDigits(text.substr(dot + 1));
        }

        std::string SdkAbiText()
        {
            return std::to_string(WXL_ABI_VERSION_MAJOR) + "." +
                   std::to_string(WXL_ABI_VERSION_MINOR) + "." +
                   std::to_string(WXL_ABI_VERSION_FEATURE);
        }

        /// Reads a run of digits as a uint32_t; false on no digits or overflow.
        bool ReadNumber(std::string_view text, size_t& pos, uint32_t& out)
        {
            if (pos >= text.size() || !IsDigit(text[pos])) return false;
            uint64_t value = 0;
            while (pos < text.size() && IsDigit(text[pos]))
            {
                value = value * 10 + static_cast<uint64_t>(text[pos] - '0');
                if (value > std::numeric_limits<uint32_t>::max()) return false;
                ++pos;
            }
            out = static_cast<uint32_t>(value);
            return true;
        }
    } // namespace

    bool IsValidModId(std::string_view id)
    {
        if (id.size() < 3 || id.size() > 64) return false;
        bool runOpen = false; // inside a run of id characters
        for (const char c : id)
        {
            if (IsIdChar(c))
            {
                runOpen = true;
            }
            else if (c == '.' || c == '-')
            {
                if (!runOpen) return false; // a separator needs a run before it
                runOpen = false;
            }
            else
            {
                return false;
            }
        }
        return runOpen; // and must not end on a separator
    }

    bool ParseSemver(std::string_view text, Semver& out)
    {
        Semver parsed;
        size_t pos = 0;
        if (!ReadNumber(text, pos, parsed.major)) return false;
        if (pos >= text.size() || text[pos] != '.') return false;
        ++pos;
        if (!ReadNumber(text, pos, parsed.minor)) return false;
        if (pos >= text.size() || text[pos] != '.') return false;
        ++pos;
        if (!ReadNumber(text, pos, parsed.patch)) return false;

        if (pos < text.size())
        {
            if (text[pos] != '-' && text[pos] != '+') return false;
            if (pos + 1 >= text.size()) return false;
            for (size_t i = pos + 1; i < text.size(); ++i)
            {
                const char c = text[i];
                const bool ok = (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') ||
                                (c >= 'a' && c <= 'z') || c == '.' || c == '-';
                if (!ok) return false;
            }
            parsed.label = std::string(text.substr(pos));
        }

        parsed.text = std::string(text);
        out         = std::move(parsed);
        return true;
    }

    std::string AbiText(const AbiVersion& abi)
    {
        return std::to_string(abi.major) + "." + std::to_string(abi.minor) + "." +
               std::to_string(abi.feature);
    }

    bool ParseAbi(std::string_view text, AbiVersion& out)
    {
        AbiVersion parsed;
        size_t     pos = 0;
        if (!ReadNumber(text, pos, parsed.major)) return false;
        if (pos >= text.size() || text[pos] != '.') return false;
        ++pos;
        if (!ReadNumber(text, pos, parsed.minor)) return false;
        if (pos >= text.size() || text[pos] != '.') return false;
        ++pos;
        if (!ReadNumber(text, pos, parsed.feature)) return false;
        if (pos != text.size()) return false;
        out = parsed;
        return true;
    }

    bool NetManifest::Declares(uint32_t opcode) const
    {
        for (const OpcodeRange& range : opcodes)
            if (opcode >= range.first && opcode <= range.last) return true;
        return false;
    }

    namespace
    {
        bool ParseOpcode(std::string_view text, uint32_t& out)
        {
            int base = 10;
            if (text.size() > 2 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X'))
            {
                base = 16;
                text.remove_prefix(2);
            }
            if (text.empty() || text.size() > (base == 16 ? 4u : 5u)) return false;
            uint32_t value = 0;
            for (const char c : text)
            {
                uint32_t digit = 0;
                if (c >= '0' && c <= '9') digit = static_cast<uint32_t>(c - '0');
                else if (base == 16 && c >= 'a' && c <= 'f') digit = static_cast<uint32_t>(c - 'a' + 10);
                else if (base == 16 && c >= 'A' && c <= 'F') digit = static_cast<uint32_t>(c - 'A' + 10);
                else return false;
                value = value * static_cast<uint32_t>(base) + digit;
            }
            if (value > kOpcodeMax) return false;
            out = value;
            return true;
        }
    } // namespace

    bool ParseOpcodeRange(std::string_view text, OpcodeRange& out)
    {
        const size_t dash = text.find('-');
        OpcodeRange  range;
        if (!ParseOpcode(text.substr(0, dash), range.first)) return false;
        range.last = range.first;
        if (dash != std::string_view::npos && !ParseOpcode(text.substr(dash + 1), range.last)) return false;
        if (range.last < range.first) return false;
        out = range;
        return true;
    }

    namespace
    {
        /// "0x521".
        std::string Hex(uint32_t value)
        {
            char text[16];
            std::snprintf(text, sizeof text, "0x%X", static_cast<unsigned>(value));
            return text;
        }

        /// Walks a parsed document against the schema. Stops at the first problem.
        class Reader
        {
        public:
            Reader(const json::Value& root, Manifest& out, std::string& error)
                : root_(root), out_(out), error_(error)
            {
            }

            bool Run()
            {
                if (!root_.Is(json::Type::Object)) return Fail(root_, "the manifest must be an object");

                for (const json::Member& member : root_.object)
                    if (!ReadField(member)) return false;

                if (!Require("id", seenId_) || !Require("name", seenName_) ||
                    !Require("version", seenVersion_) || !Require("abi", seenAbi_))
                    return false;

                // A mod is native or a script, never both: its code is the "binaries" or the
                // "lua" entry, and both together would run twice.
                if (!seenBinaries_ && out_.lua.empty())
                {
                    error_ = "required field 'binaries' is missing (a script mod names 'lua' instead)";
                    return false;
                }
                if (seenBinaries_ && !out_.lua.empty())
                    return Fail(*binaries_, "'binaries' and 'lua' cannot both be given: a mod is "
                                            "either a binary the runtime loads or a Lua entry "
                                            "wxl.lua runs, and with both its code would run twice. "
                                            "Keep 'binaries' for a C++ mod, 'lua' for a Lua mod, "
                                            "or make two mods");
                return true;
            }

        private:
            bool Fail(const json::Value& at, const std::string& message)
            {
                error_ = "line " + std::to_string(at.line) + ": " + message;
                return false;
            }

            bool Require(const char* field, bool seen)
            {
                if (seen) return true;
                error_ = std::string("required field '") + field + "' is missing";
                return false;
            }

            bool ExpectType(const json::Value& value, json::Type type, const std::string& what)
            {
                if (value.Is(type)) return true;
                return Fail(value, "'" + what + "' must be " + json::TypeName(type) + ", not " +
                                       json::TypeName(value.type));
            }

            bool ReadString(const json::Value& value, const std::string& what, std::string& out,
                            size_t minLength, size_t maxLength)
            {
                if (!ExpectType(value, json::Type::String, what)) return false;
                if (value.string.size() < minLength)
                    return Fail(value, "'" + what + "' must be at least " +
                                           std::to_string(minLength) + " characters long");
                if (value.string.size() > maxLength)
                    return Fail(value, "'" + what + "' must be at most " +
                                           std::to_string(maxLength) + " characters long");
                out = value.string;
                return true;
            }

            bool ReadStringArray(const json::Value& value, const std::string& what,
                                 std::vector<std::string>& out)
            {
                if (!ExpectType(value, json::Type::Array, what)) return false;
                for (size_t i = 0; i < value.array.size(); ++i)
                {
                    std::string item;
                    if (!ReadString(value.array[i], what + "[" + std::to_string(i) + "]", item, 0,
                                    std::numeric_limits<size_t>::max()))
                        return false;
                    out.push_back(std::move(item));
                }
                return true;
            }

            bool ReadServiceRefs(const json::Value& value, const std::string& what,
                                 std::vector<ServiceRef>& out)
            {
                if (!ExpectType(value, json::Type::Array, what)) return false;
                for (size_t i = 0; i < value.array.size(); ++i)
                {
                    const json::Value& entry = value.array[i];
                    const std::string  where = what + "[" + std::to_string(i) + "]";
                    if (!ExpectType(entry, json::Type::Object, where)) return false;

                    ServiceRef ref;
                    bool       seenService = false;
                    bool       seenVersion = false;
                    for (const json::Member& member : entry.object)
                    {
                        if (member.key == "service")
                        {
                            if (!ReadString(member.value, where + ".service", ref.service, 0,
                                            std::numeric_limits<size_t>::max()))
                                return false;
                            seenService = true;
                        }
                        else if (member.key == "version")
                        {
                            const json::Value& v = member.value;
                            if (!v.Is(json::Type::Number) || !v.integral)
                                return Fail(v, "'" + where + ".version' must be an integer");
                            if (v.integer < 1)
                                return Fail(v, "'" + where + ".version' must be at least 1");
                            if (v.integer > std::numeric_limits<uint32_t>::max())
                                return Fail(v, "'" + where + ".version' is out of range");
                            ref.version = static_cast<uint32_t>(v.integer);
                            seenVersion = true;
                        }
                        else
                        {
                            return Fail(member.value, "'" + where + "' has an unknown field '" +
                                                          member.key + "'");
                        }
                    }
                    if (!seenService) return Fail(entry, "'" + where + "' is missing 'service'");
                    if (!seenVersion) return Fail(entry, "'" + where + "' is missing 'version'");
                    out.push_back(std::move(ref));
                }
                return true;
            }

            bool ReadBinaries(const json::Value& value)
            {
                if (!ExpectType(value, json::Type::Object, "binaries")) return false;
                if (value.object.empty())
                    return Fail(value, "'binaries' must name at least one target");
                for (const json::Member& member : value.object)
                {
                    if (!IsKnownTarget(member.key))
                        return Fail(member.value, "'binaries' has an unknown target '" +
                                                      member.key +
                                                      "'; expected win-x86, win-x64 or linux-x64");
                    Binary binary;
                    binary.target = member.key;
                    if (!ReadString(member.value, "binaries." + member.key, binary.path, 1,
                                    std::numeric_limits<size_t>::max()))
                        return false;
                    out_.binaries.push_back(std::move(binary));
                }
                return true;
            }

            bool ReadNet(const json::Value& value)
            {
                if (!ExpectType(value, json::Type::Object, "net")) return false;
                const json::Value* opcodes = nullptr;
                for (const json::Member& member : value.object)
                {
                    if (member.key == "stock")
                    {
                        if (!ExpectType(member.value, json::Type::Bool, "net.stock")) return false;
                        out_.net.stock = member.value.boolean;
                    }
                    else if (member.key == "opcodes")
                    {
                        if (!ExpectType(member.value, json::Type::Array, "net.opcodes")) return false;
                        opcodes = &member.value;
                    }
                    else
                    {
                        return Fail(member.value, "'net' has an unknown field '" + member.key +
                                                      "'; it takes 'opcodes' and 'stock'");
                    }
                }
                if (!opcodes) return true;
                for (size_t i = 0; i < opcodes->array.size(); ++i)
                {
                    const json::Value& entry = opcodes->array[i];
                    const std::string  where = "net.opcodes[" + std::to_string(i) + "]";
                    OpcodeRange        range;
                    if (entry.Is(json::Type::Number))
                    {
                        if (!entry.integral || entry.integer < 0 || entry.integer > static_cast<int64_t>(kOpcodeMax))
                            return Fail(entry, "'" + where + "' must be a whole number from 0 to 0xFFFF");
                        range.first = range.last = static_cast<uint32_t>(entry.integer);
                    }
                    else if (entry.Is(json::Type::String))
                    {
                        if (!ParseOpcodeRange(entry.string, range))
                            return Fail(entry, "'" + where + "' is \"" + entry.string +
                                                   "\"; it takes a number up to 0xFFFF, \"0x521\", or a "
                                                   "range \"0x521-0x52A\" whose first number is the lower");
                    }
                    else
                    {
                        return Fail(entry, "'" + where + "' must be a number or a string, not " +
                                               json::TypeName(entry.type));
                    }
                    if (range.first <= kStockOpcodeLimit && !out_.net.stock)
                        return Fail(entry, "'" + where + "' declares " + Hex(range.first) +
                                               ", one of the client's own numbers (up to " +
                                               Hex(kStockOpcodeLimit) + "); a mod's own start at " +
                                               Hex(kStockOpcodeLimit + 1) +
                                               ", and a tool that must use the client's says "
                                               "\"stock\": true in 'net'");
                    for (const OpcodeRange& other : out_.net.opcodes)
                        if (range.first <= other.last && other.first <= range.last)
                            return Fail(entry, "'" + where + "' overlaps an earlier entry of 'net.opcodes'");
                    out_.net.opcodes.push_back(range);
                }
                return true;
            }

            bool ReadField(const json::Member& member)
            {
                const std::string& key   = member.key;
                const json::Value& value = member.value;

                if (key == "$schema")
                {
                    std::string ignored;
                    return ReadString(value, key, ignored, 0, std::numeric_limits<size_t>::max());
                }
                if (key == "id")
                {
                    seenId_ = true;
                    if (!ReadString(value, key, out_.id, 3, 64)) return false;
                    if (!IsValidModId(out_.id))
                        return Fail(value, "'id' must be lowercase letters, digits and single '.' "
                                           "or '-' separators, like \"wxl.example\"");
                    return true;
                }
                if (key == "name")
                {
                    seenName_ = true;
                    return ReadString(value, key, out_.name, 1, 64);
                }
                if (key == "version")
                {
                    seenVersion_ = true;
                    if (!ExpectType(value, json::Type::String, key)) return false;
                    if (!ParseSemver(value.string, out_.version))
                        return Fail(value, "'version' must be a semver like \"1.0.0\", not \"" +
                                               value.string + "\"");
                    return true;
                }
                if (key == "abi")
                {
                    seenAbi_ = true;
                    if (!ExpectType(value, json::Type::String, key)) return false;
                    if (ParseAbi(value.string, out_.abi)) return true;
                    if (IsTwoPartAbi(value.string))
                        return Fail(value, "'abi' is \"" + value.string +
                                               "\", the two-part numbering the SDK used before "
                                               "0.1.5; rebuild the mod against the current SDK and "
                                               "write the \"major.minor.feature\" its abi.h "
                                               "carries (this runtime's is \"" +
                                               SdkAbiText() + "\")");
                    return Fail(value, "'abi' must be \"major.minor.feature\", not \"" +
                                           value.string + "\"");
                }
                if (key == "binaries")
                {
                    seenBinaries_ = true;
                    binaries_     = &value;
                    return ReadBinaries(value);
                }
                if (key == "requires") return ReadServiceRefs(value, key, out_.required);
                if (key == "optional") return ReadServiceRefs(value, key, out_.optional);
                if (key == "loadAfter") return ReadStringArray(value, key, out_.load_after);
                if (key == "lua")
                    return ReadString(value, key, out_.lua, 1, std::numeric_limits<size_t>::max());
                if (key == "net") return ReadNet(value);
                if (key == "description") return ReadString(value, key, out_.description, 0, 512);
                if (key == "authors") return ReadStringArray(value, key, out_.authors);
                if (key == "homepage")
                    return ReadString(value, key, out_.homepage, 0,
                                      std::numeric_limits<size_t>::max());

                return Fail(value, "'" + key + "' is not a manifest field");
            }

            const json::Value& root_;
            Manifest&          out_;
            std::string&       error_;
            bool               seenId_       = false;
            bool               seenName_     = false;
            bool               seenVersion_  = false;
            bool               seenAbi_      = false;
            bool               seenBinaries_ = false;
            const json::Value* binaries_     = nullptr;
        };
    } // namespace

    bool ParseManifest(std::string_view text, Manifest& out, std::string& error)
    {
        json::Value root;
        json::Error parseError;
        if (!json::Parse(text, root, parseError))
        {
            error = parseError.Text();
            return false;
        }

        Manifest parsed;
        Reader   reader(root, parsed, error);
        if (!reader.Run()) return false;

        out = std::move(parsed);
        return true;
    }

    bool ReadManifest(const std::filesystem::path& file, Manifest& out, std::string& error)
    {
        std::ifstream stream(file, std::ios::binary);
        if (!stream)
        {
            error = file.string() + ": cannot be read";
            return false;
        }
        std::ostringstream buffer;
        buffer << stream.rdbuf();

        std::string problem;
        if (!ParseManifest(buffer.str(), out, problem))
        {
            error = file.string() + ": " + problem;
            return false;
        }
        return true;
    }
} // namespace wxl::host
