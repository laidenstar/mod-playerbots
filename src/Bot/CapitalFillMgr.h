/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_CAPITALFILLMGR_H
#define PLAYERBOTS_CAPITALFILLMGR_H

#include "ObjectGuid.h"
#include "Position.h"
#include <unordered_map>
#include <vector>

class Player;

// Tops up the random bot population of a capital city while a real player is in it, by teleporting
// idle bots in from elsewhere. Bots are not held there; their normal RPG behaviour takes over.
class CapitalFillMgr
{
public:
    static CapitalFillMgr& instance()
    {
        static CapitalFillMgr instance;

        return instance;
    }

    void Update(uint32 diff);

private:
    CapitalFillMgr() = default;
    ~CapitalFillMgr() = default;

    CapitalFillMgr(CapitalFillMgr const&) = delete;
    CapitalFillMgr& operator=(CapitalFillMgr const&) = delete;

    CapitalFillMgr(CapitalFillMgr&&) = delete;
    CapitalFillMgr& operator=(CapitalFillMgr&&) = delete;

    struct OccupiedCapital
    {
        std::vector<Position> playerPositions;
        uint32 botCount = 0;
    };

    void FillCapital(uint32 zoneId, OccupiedCapital const& capital, std::vector<Player*>& candidates);
    bool IsCandidate(Player* bot, time_t now) const;
    bool TeleportIn(Player* bot, uint32 zoneId, OccupiedCapital const& capital);

    struct BroughtIn
    {
        time_t at = 0;
        uint32 zoneId = 0;
    };

    uint32 _timer = 0;
    std::unordered_map<ObjectGuid, BroughtIn> _broughtIn;
};

#define sCapitalFillMgr CapitalFillMgr::instance()

#endif
