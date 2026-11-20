/* rootclock -- the fixed nickname and faction tables.
 *
 * Faction order is rc_faction_id_t (rc_types.h) == documents/04 section 2:
 *   key        short_name    full_name
 *   marquise   Marquise      Marquise de Cat
 *   eyrie      Eyrie         Eyrie Dynasties
 *   woodland   Woodland      Woodland Alliance
 *   vagabond   Vagabond      Vagabond
 *   riverfolk  Riverfolk     Riverfolk Company
 *   cult       Lizard Cult   Lizard Cult
 *   duchy      Duchy         Underground Duchy
 *   corvid     Corvids       Corvid Conspiracy
 *   hundreds   Hundreds      Lord of the Hundreds
 *   keepers    Keepers       Keepers in Iron
 *   twilight   Twilight      Twilight Council
 *   knaves     Knaves        Knaves of the Deepwood
 *   lilypad    Lilypad       Lilypad Diaspora
 * `key` is the export id (contract v1); `short_name` is at most 11 chars so
 * it fits S1/S2 at text scale 2.
 *
 * Nicknames, in id order: Jacob, Alex, Ahmed, Lorenzo. */
#ifndef RC_CATALOG_H
#define RC_CATALOG_H
#include "rc_types.h"

typedef struct {
    const char *key;
    const char *short_name;
    const char *full_name;
} rc_faction_t;

/* The faction for `id`, or NULL if id >= RC_FACTION_COUNT. */
const rc_faction_t *rc_faction(uint8_t id);

/* The nickname for `id`, or "?" if id >= RC_NICKNAME_COUNT. */
const char *rc_nickname(uint8_t id);

#endif
