#include "Xor/XorConfig.hpp"

#include <vector>
#include <unordered_set>
#include <map>

namespace xor
{
    struct RegisteredConfigurables
    {
        std::vector<Configurable *> topLevelSorted;
        std::unordered_set<Configurable *> topLevel;

        struct Registration
        {
            Configurable *cfg = nullptr;
            const void *begin = nullptr;
            const void *end   = nullptr;
        };
        std::map<const void *, Registration> registered;
        std::vector<Registration> newlyRegistered;
        std::vector<Configurable *> newlyUnregistered;

        void sortTopLevelConfigurables()
        {
            topLevelSorted.clear();
            topLevelSorted.insert(topLevelSorted.end(), topLevel.begin(), topLevel.end());

            std::sort(topLevelSorted.begin(), topLevelSorted.end(),
                      [](Configurable *a, Configurable *b)
            {
                return strcmp(a->name(), b->name()) < 0;
            });
        }

        void processRegistrations()
        {
            if (newlyRegistered.empty() && newlyUnregistered.empty())
                return;

            // First, make sure "registered" is up to date.

            {
                for (auto &r : newlyRegistered)
                    registered[r.begin] = r;

                newlyRegistered.clear();
            }

            if (!newlyUnregistered.empty())
            {
                std::unordered_set<Configurable *> unregs(newlyUnregistered.begin(), newlyUnregistered.end());
                auto it = registered.begin();
                while (it != registered.end())
                {
                    if (unregs.count(it->second.cfg))
                        it = registered.erase(it);
                    else
                        ++it;
                }

                newlyUnregistered.clear();
            }

            // "registered" is now the authoritative list of active configurables.
            // Clear member lists of all active configurable groups.

            for (auto &kv : registered)
            {
                if (auto members = kv.second.cfg->configurableMembers())
                    members->clear();
            }

            // Now, repopulate the member lists.

            topLevel.clear();
            std::vector<Registration> groupStack;

            for (auto &kv : registered)
            {
                const void *beginAddr = kv.first;
                Configurable *cfg = kv.second.cfg;

                for (;;)
                {
                    // If the stack is empty, the configurable is top level
                    if (groupStack.empty())
                    {
                        topLevel.emplace(cfg);
                        groupStack.emplace_back(kv.second);
                        break;
                    }
                    // If the top of the stack contains the configurable, insert
                    // it as a member.
                    else if (groupStack.back().end > beginAddr)
                    {
                        if (auto members = groupStack.back().cfg->configurableMembers())
                            members->emplace_back(cfg);

                        // As this configurable may itself be a subgroup, put it on the stack.
                        groupStack.emplace_back(kv.second);

                        break;
                    }
                    // Otherwise, pop the stack once and try again.
                    else
                    {
                        groupStack.pop_back();
                    }
                }
            }

            sortTopLevelConfigurables();
        }
    } g_registeredConfigurables;


    void registerConfigurable(Configurable * cfg, const void * addrBegin, const void * addrEnd)
    {
        g_registeredConfigurables.newlyRegistered.emplace_back(cfg, addrBegin, addrEnd);
    }

    void unregisterConfigurable(Configurable * cfg)
    {
        g_registeredConfigurables.newlyUnregistered.emplace_back(cfg);
    }

    void processConfigurables()
    {
        g_registeredConfigurables.processRegistrations();
        for (auto &cfg : g_registeredConfigurables.topLevelSorted)
            cfg->configure();
    }

    std::vector<String> determineConfigEnumValueNames(const char *stringizedMacroVarags)
    {
        return StringView(stringizedMacroVarags).split(" \t\r\n,");
    }

    std::vector<char> determineConfigEnumValueNamesZeroSeparated(const char *stringizedMacroVarags)
    {
        std::vector<char> valueNames;

        for (auto &n : determineConfigEnumValueNames(stringizedMacroVarags))
        {
            valueNames.insert(valueNames.end(), n.begin(), n.end());
            valueNames.emplace_back('\0');
        }

        valueNames.emplace_back('\0');

        return valueNames;
    }

}
