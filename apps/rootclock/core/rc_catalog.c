/* rootclock -- nickname and faction tables. See rc_catalog.h.
 *
 * Row order is rc_faction_id_t, which is also the export key table order in
 * documents/04 section 2 and the generated icon order. Do not reorder. */
#include "rc_catalog.h"

static const rc_faction_t rc_factions[RC_FACTION_COUNT] = {
    [RC_FACTION_MARQUISE]  = { "marquise",  "Marquise",    "Marquise de Cat"        },
    [RC_FACTION_EYRIE]     = { "eyrie",     "Eyrie",       "Eyrie Dynasties"        },
    [RC_FACTION_WOODLAND]  = { "woodland",  "Woodland",    "Woodland Alliance"      },
    [RC_FACTION_VAGABOND]  = { "vagabond",  "Vagabond",    "Vagabond"               },
    [RC_FACTION_RIVERFOLK] = { "riverfolk", "Riverfolk",   "Riverfolk Company"      },
    [RC_FACTION_CULT]      = { "cult",      "Lizard Cult", "Lizard Cult"            },
    [RC_FACTION_DUCHY]     = { "duchy",     "Duchy",       "Underground Duchy"      },
    [RC_FACTION_CORVID]    = { "corvid",    "Corvids",     "Corvid Conspiracy"      },
    [RC_FACTION_HUNDREDS]  = { "hundreds",  "Hundreds",    "Lord of the Hundreds"   },
    [RC_FACTION_KEEPERS]   = { "keepers",   "Keepers",     "Keepers in Iron"        },
    [RC_FACTION_TWILIGHT]  = { "twilight",  "Twilight",    "Twilight Council"       },
    [RC_FACTION_KNAVES]    = { "knaves",    "Knaves",      "Knaves of the Deepwood" },
    [RC_FACTION_LILYPAD]   = { "lilypad",   "Lilypad",     "Lilypad Diaspora"       },
};

static const char *const rc_nicknames[RC_NICKNAME_COUNT] = {
    "Jacob", "Alex", "Ahmed", "Lorenzo",
};

const rc_faction_t *rc_faction(uint8_t id) {
    return (id < (uint8_t)RC_FACTION_COUNT) ? &rc_factions[id] : NULL;
}

const char *rc_nickname(uint8_t id) {
    return (id < RC_NICKNAME_COUNT) ? rc_nicknames[id] : "?";
}
