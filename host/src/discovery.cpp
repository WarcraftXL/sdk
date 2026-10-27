// SPDX-License-Identifier: MIT
// Copyright (c) 2026 WarcraftXL

#include "wxl/host/discovery.hpp"

#include <algorithm>
#include <cstdio>
#include <set>
#include <unordered_map>
#include <utility>

namespace wxl::host
{
    Discovery Discover(const std::filesystem::path& mods_directory)
    {
        namespace fs = std::filesystem;

        Discovery       result;
        std::error_code ec;
        if (!fs::is_directory(mods_directory, ec)) return result;

        std::vector<fs::path> folders;
        for (const fs::directory_entry& entry : fs::directory_iterator(mods_directory, ec))
        {
            std::error_code ignored;
            if (entry.is_directory(ignored)) folders.push_back(entry.path());
        }
        std::sort(folders.begin(), folders.end(), [](const fs::path& a, const fs::path& b) {
            return a.filename().string() < b.filename().string();
        });

        for (const fs::path& folder : folders)
        {
            const fs::path  file = folder / kManifestFileName;
            std::error_code ignored;
            if (!fs::is_regular_file(file, ignored)) continue;

            ModCandidate candidate;
            candidate.directory_name = folder.filename().string();
            candidate.directory      = folder;

            std::string error;
            if (!ReadManifest(file, candidate.manifest, error))
            {
                result.problems.push_back(
                    DiscoveryProblem{candidate.directory_name, folder, std::string(), error});
                continue;
            }

            const auto twin = std::find_if(result.mods.begin(), result.mods.end(),
                                           [&](const ModCandidate& other) {
                                               return other.manifest.id == candidate.manifest.id;
                                           });
            if (twin != result.mods.end())
            {
                result.problems.push_back(DiscoveryProblem{
                    candidate.directory_name, folder, candidate.manifest.id,
                    "id '" + candidate.manifest.id + "' is already provided by " +
                        twin->directory_name});
                continue;
            }

            result.mods.push_back(std::move(candidate));
        }
        return result;
    }

    namespace
    {
        /// The load-after graph: an edge a -> m means m loads after a.
        struct Graph
        {
            std::vector<std::vector<size_t>> successors;
            std::vector<size_t>              indegree;
        };

        Graph BuildGraph(const std::vector<ModCandidate>& mods)
        {
            std::unordered_map<std::string, size_t> byId;
            for (size_t i = 0; i < mods.size(); ++i) byId.emplace(mods[i].manifest.id, i);

            Graph graph;
            graph.successors.resize(mods.size());
            graph.indegree.resize(mods.size(), 0);
            for (size_t m = 0; m < mods.size(); ++m)
            {
                std::set<size_t> before; // each named mod once, however often it is listed
                for (const std::string& id : mods[m].manifest.load_after)
                {
                    const auto found = byId.find(id);
                    if (found != byId.end()) before.insert(found->second);
                }
                for (const size_t a : before)
                {
                    graph.successors[a].push_back(m);
                    ++graph.indegree[m];
                }
            }
            return graph;
        }

        /// Finds the cycles among the nodes Kahn's walk could not reach.
        class CycleFinder
        {
        public:
            CycleFinder(const std::vector<ModCandidate>& mods, const Graph& graph)
                : mods_(mods), graph_(graph), colour_(mods.size(), 0), cycleOf_(mods.size(), -1)
            {
            }

            void Visit(size_t v)
            {
                colour_[v] = 1;
                stack_.push_back(v);
                for (const size_t s : graph_.successors[v])
                {
                    if (colour_[s] == 1) Record(s);
                    else if (colour_[s] == 0) Visit(s);
                }
                stack_.pop_back();
                colour_[v] = 2;
            }

            /// The text of the cycle a node is part of, or nullptr.
            const std::string* CycleText(size_t v) const
            {
                return cycleOf_[v] < 0 ? nullptr : &cycles_[static_cast<size_t>(cycleOf_[v])];
            }

        private:
            void Record(size_t start)
            {
                const auto from = std::find(stack_.begin(), stack_.end(), start);
                std::string text;
                for (auto it = from; it != stack_.end(); ++it)
                {
                    text += mods_[*it].manifest.id;
                    text += " -> ";
                    if (cycleOf_[*it] < 0) cycleOf_[*it] = static_cast<int>(cycles_.size());
                }
                text += mods_[start].manifest.id;
                cycles_.push_back(std::move(text));
            }

            const std::vector<ModCandidate>& mods_;
            const Graph&                     graph_;
            std::vector<int>                 colour_; ///< 0 unseen, 1 on the stack, 2 done
            std::vector<int>                 cycleOf_;
            std::vector<size_t>              stack_;
            std::vector<std::string>         cycles_;
        };
    } // namespace

    LoadOrder OrderByLoadAfter(const std::vector<ModCandidate>& mods)
    {
        LoadOrder result;
        Graph     graph = BuildGraph(mods);

        // Kahn's walk, always taking the smallest directory name among what is ready: the only
        // freedom the constraints leave is spent the same way every time.
        std::set<std::pair<std::string, size_t>> ready;
        for (size_t i = 0; i < mods.size(); ++i)
            if (graph.indegree[i] == 0) ready.emplace(mods[i].directory_name, i);

        std::vector<bool> placed(mods.size(), false);
        while (!ready.empty())
        {
            const size_t i = ready.begin()->second;
            ready.erase(ready.begin());
            placed[i] = true;
            result.ordered.push_back(mods[i]);
            for (const size_t s : graph.successors[i])
                if (--graph.indegree[s] == 0) ready.emplace(mods[s].directory_name, s);
        }
        if (result.ordered.size() == mods.size()) return result;

        // Whatever is left sits on a cycle or behind one.
        std::vector<size_t> remaining;
        for (size_t i = 0; i < mods.size(); ++i)
            if (!placed[i]) remaining.push_back(i);
        std::sort(remaining.begin(), remaining.end(), [&](size_t a, size_t b) {
            return mods[a].directory_name < mods[b].directory_name;
        });

        CycleFinder finder(mods, graph);
        for (const size_t v : remaining) finder.Visit(v);

        for (const size_t v : remaining)
        {
            DiscoveryProblem problem;
            problem.directory_name = mods[v].directory_name;
            problem.directory      = mods[v].directory;
            problem.id             = mods[v].manifest.id;
            if (const std::string* cycle = finder.CycleText(v))
            {
                problem.message = "load-order cycle: " + *cycle;
            }
            else
            {
                std::string blocker;
                for (const std::string& id : mods[v].manifest.load_after)
                {
                    const auto it = std::find_if(remaining.begin(), remaining.end(), [&](size_t r) {
                        return mods[r].manifest.id == id;
                    });
                    if (it != remaining.end())
                    {
                        blocker = id;
                        break;
                    }
                }
                problem.message = "loads after '" + blocker + "', which cannot load";
            }
            result.excluded.push_back(std::move(problem));
        }
        return result;
    }

    const DeclaredOpcodes* OpcodeDeclarations::Find(uint32_t opcode) const
    {
        for (const DeclaredOpcodes& run : runs)
            if (opcode >= run.range.first && opcode <= run.range.last) return &run;
        return nullptr;
    }

    OpcodeDeclarations DeclareOpcodes(LoadOrder& order)
    {
        OpcodeDeclarations declared;
        std::vector<ModCandidate> kept;
        kept.reserve(order.ordered.size());
        for (ModCandidate& candidate : order.ordered)
        {
            const Manifest&        manifest = candidate.manifest;
            const DeclaredOpcodes* clash    = nullptr;
            uint32_t               number   = 0;
            for (const OpcodeRange& range : manifest.net.opcodes)
            {
                for (const DeclaredOpcodes& run : declared.runs)
                {
                    if (range.first > run.range.last || run.range.first > range.last) continue;
                    clash  = &run;
                    number = std::max(range.first, run.range.first);
                    break;
                }
                if (clash) break;
            }
            if (clash)
            {
                char text[16];
                std::snprintf(text, sizeof text, "0x%X", static_cast<unsigned>(number));
                DiscoveryProblem problem;
                problem.directory_name = candidate.directory_name;
                problem.directory      = candidate.directory;
                problem.id             = manifest.id;
                problem.message        = std::string("its mod.json declares opcode ") + text + ", which " +
                                  clash->mod +
                                  " declares and loads first; two mods cannot answer the same message. "
                                  "Give one of them other numbers in 'net.opcodes'";
                order.excluded.push_back(std::move(problem));
                continue;
            }
            const std::string served = manifest.lua.empty() ? manifest.id : std::string(kScriptRuntimeId);
            for (const OpcodeRange& range : manifest.net.opcodes)
                declared.runs.push_back(DeclaredOpcodes{range, manifest.id, served});
            kept.push_back(std::move(candidate));
        }
        order.ordered = std::move(kept);
        return declared;
    }
} // namespace wxl::host
