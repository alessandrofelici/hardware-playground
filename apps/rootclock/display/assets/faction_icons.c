/* rootclock -- faction icons. STUB: replaced by the output of
 * tools/gen_faction_icons.py on feat/rootclock-assets. */
#include "faction_icons.h"

static const rc_icon_t s_none = { RC_ICON_W, RC_ICON_H, 0 };

const rc_icon_t *rc_icon_get(uint8_t faction_id) { (void)faction_id; return &s_none; }
