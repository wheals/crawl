/**
 * @file
 * @brief Functions related to special abilities.
**/

#include "AppHdr.h"

#include "ability.h"

#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <iomanip>
#include <sstream>

#include "abyss.h"
#include "acquire.h"
#include "areas.h"
#include "branch.h"
#include "butcher.h"
#include "cloud.h"
#include "coordit.h"
#include "database.h"
#include "decks.h"
#include "delay.h"
#include "describe.h"
#include "directn.h"
#include "dungeon.h"
#include "evoke.h"
#include "exercise.h"
#include "food.h"
#include "godabil.h"
#include "godconduct.h"
#include "godprayer.h"
#include "godwrath.h"
#include "hints.h"
#include "invent.h"
#include "itemprop.h"
#include "items.h"
#include "item_use.h"
#include "libutil.h"
#include "macro.h"
#include "maps.h"
#include "menu.h"
#include "message.h"
#include "misc.h"
#include "mon-place.h"
#include "mutation.h"
#include "notes.h"
#include "options.h"
#include "output.h"
#include "player-stats.h"
#include "potion.h"
#include "prompt.h"
#include "religion.h"
#include "skills.h"
#include "spl-cast.h"
#include "spl-clouds.h"
#include "spl-damage.h"
#include "spl-goditem.h"
#include "spl-miscast.h"
#include "spl-other.h"
#include "spl-selfench.h"
#include "spl-summoning.h"
#include "spl-transloc.h"
#include "stairs.h"
#include "state.h"
#include "stepdown.h"
#include "stringutil.h"
#include "target.h"
#include "terrain.h"
#include "tilepick.h"
#include "transform.h"
#include "traps.h"
#include "uncancel.h"
#include "unicode.h"
#include "view.h"

enum class abflag
{
    NONE           = 0x00000000,
    BREATH         = 0x00000001, // ability uses DUR_BREATH_WEAPON
    DELAY          = 0x00000002, // ability has its own delay
    PAIN           = 0x00000004, // ability must hurt player (ie torment)
    PIETY          = 0x00000008, // ability has its own piety cost
    EXHAUSTION     = 0x00000010, // fails if you.exhausted
    INSTANT        = 0x00000020, // doesn't take time to use
    PERMANENT_HP   = 0x00000040, // costs permanent HPs
    PERMANENT_MP   = 0x00000080, // costs permanent MPs
    CONF_OK        = 0x00000100, // can use even if confused
    FRUIT          = 0x00000200, // ability requires fruit
    VARIABLE_FRUIT = 0x00000400, // ability requires fruit or piety
    VARIABLE_MP    = 0x00000800, // costs a variable amount of MP
                   //0x00001000,
                   //0x00002000,
                   //0x00004000,
                   //0x00008000,
                   //0x00010000,
                   //0x00020000,
                   //0x00040000,
    SKILL_DRAIN    = 0x00080000, // drains skill levels
    GOLD           = 0x00100000, // costs gold
    SACRIFICE      = 0x00200000, // sacrifice (Ru)
    HOSTILE        = 0x00400000, // failure summons a hostile (Makhleb)
};
DEF_BITFIELD(ability_flags, abflag);

struct generic_cost
{
    int base, add, rolls;

    generic_cost(int num)
        : base(num), add(num == 0 ? 0 : (num + 1) / 2 + 1), rolls(1)
    {
    }
    generic_cost(int num, int _add, int _rolls = 1)
        : base(num), add(_add), rolls(_rolls)
    {
    }
    static generic_cost fixed(int fixed)
    {
        return generic_cost(fixed, 0, 1);
    }
    static generic_cost range(int low, int high, int _rolls = 1)
    {
        return generic_cost(low, high - low + 1, _rolls);
    }

    int cost() const PURE;

    operator bool () const { return base > 0 || add > 0; }
};

struct scaling_cost
{
    int value;

    scaling_cost(int permille) : value(permille) {}

    static scaling_cost fixed(int fixed)
    {
        return scaling_cost(-fixed);
    }

    int cost(int max) const;

    operator bool () const { return value != 0; }
};

// Structure for representing an ability:
struct ability_def
{
    ability_type        ability;
    const char *        name;
    unsigned int        mp_cost;        // magic cost of ability
    scaling_cost        hp_cost;        // hit point cost of ability
    unsigned int        food_cost;      // + rand2avg(food_cost, 2)
    generic_cost        piety_cost;     // + random2((piety_cost + 1) / 2 + 1)
    ability_flags       flags;          // used for additional cost notices
};

static int _lookup_ability_slot(ability_type abil);
static spret_type _do_ability(const ability_def& abil, bool fail);
static void _pay_ability_costs(const ability_def& abil);
static int _scale_piety_cost(ability_type abil, int original_cost);

// The description screen was way out of date with the actual costs.
// This table puts all the information in one place... -- bwr
//
// The four numerical fields are: MP, HP, food, and piety.
// Note:  food_cost  = val + random2avg(val, 2)
//        piety_cost = val + random2((val + 1) / 2 + 1);
//        hp cost is in per-mil of maxhp (i.e. 20 = 2% of hp, rounded up)
static const ability_def Ability_List[] =
{
    // NON_ABILITY should always come first
    { ABIL_NON_ABILITY, "No ability", 0, 0, 0, 0, abflag::NONE },
    { ABIL_SPIT_POISON, "Spit Poison", 0, 0, 40, 0, abflag::BREATH },

    { ABIL_BLINK, "Blink", 0, 50, 50, 0, abflag::NONE },

    { ABIL_BREATHE_FIRE, "Breathe Fire", 0, 0, 125, 0, abflag::BREATH },
    { ABIL_BREATHE_FROST, "Breathe Frost", 0, 0, 125, 0, abflag::BREATH },
    { ABIL_BREATHE_POISON, "Breathe Poison Gas",
      0, 0, 125, 0, abflag::BREATH },
    { ABIL_BREATHE_MEPHITIC, "Breathe Noxious Fumes",
      0, 0, 125, 0, abflag::BREATH },
    { ABIL_BREATHE_LIGHTNING, "Breathe Lightning",
      0, 0, 125, 0, abflag::BREATH },
    { ABIL_BREATHE_POWER, "Breathe Dispelling Energy", 0, 0, 125, 0, abflag::BREATH },
    { ABIL_BREATHE_STICKY_FLAME, "Breathe Sticky Flame",
      0, 0, 125, 0, abflag::BREATH },
    { ABIL_BREATHE_STEAM, "Breathe Steam", 0, 0, 75, 0, abflag::BREATH },
    { ABIL_TRAN_BAT, "Bat Form", 2, 0, 0, 0, abflag::NONE },

    { ABIL_SPIT_ACID, "Spit Acid", 0, 0, 125, 0, abflag::BREATH },

    { ABIL_FLY, "Fly", 3, 0, 100, 0, abflag::NONE },
    { ABIL_STOP_FLYING, "Stop Flying", 0, 0, 0, 0, abflag::NONE },
    { ABIL_HELLFIRE, "Hellfire", 0, 150, 200, 0, abflag::NONE },

    { ABIL_DELAYED_FIREBALL, "Release Delayed Fireball",
      0, 0, 0, 0, abflag::INSTANT },
    { ABIL_STOP_SINGING, "Stop Singing",
      0, 0, 0, 0, abflag::NONE },
    { ABIL_MUMMY_RESTORATION, "Self-Restoration",
      1, 0, 0, 0, abflag::PERMANENT_MP },

    { ABIL_DIG, "Dig", 0, 0, 0, 0, abflag::INSTANT },
    { ABIL_SHAFT_SELF, "Shaft Self", 0, 0, 250, 0, abflag::DELAY },

    // EVOKE abilities use Evocations and come from items.
    // Teleportation and Blink can also come from mutations
    // so we have to distinguish them (see above). The off items
    // below are labeled EVOKE because they only work now if the
    // player has an item with the evocable power (not just because
    // you used a wand, potion, or miscast effect). I didn't see
    // any reason to label them as "Evoke" in the text, they don't
    // use or train Evocations (the others do).  -- bwr
    { ABIL_EVOKE_BLINK, "Evoke Blink", 1, 0, 50, 0, abflag::NONE },
    { ABIL_RECHARGING, "Device Recharging", 1, 0, 0, 0, abflag::PERMANENT_MP },

    { ABIL_EVOKE_BERSERK, "Evoke Berserk Rage", 0, 0, 0, 0, abflag::NONE },

    { ABIL_EVOKE_TURN_INVISIBLE, "Evoke Invisibility",
      2, 0, 250, 0, abflag::NONE },
    { ABIL_EVOKE_TURN_VISIBLE, "Turn Visible", 0, 0, 0, 0, abflag::NONE },
    { ABIL_EVOKE_FLIGHT, "Evoke Flight", 1, 0, 100, 0, abflag::NONE },
    { ABIL_EVOKE_FOG, "Evoke Fog", 2, 0, 250, 0, abflag::NONE },

    { ABIL_END_TRANSFORMATION, "End Transformation", 0, 0, 0, 0, abflag::NONE },

    // INVOCATIONS:
    // Zin
    { ABIL_ZIN_RECITE, "Recite", 0, 0, 0, 0, abflag::BREATH },
    { ABIL_ZIN_VITALISATION, "Vitalisation", 2, 0, 0, 1, abflag::NONE },
    { ABIL_ZIN_IMPRISON, "Imprison", 5, 0, 125, 4, abflag::NONE },
    { ABIL_ZIN_SANCTUARY, "Sanctuary", 7, 0, 150, 15, abflag::NONE },
    { ABIL_ZIN_CURE_ALL_MUTATIONS, "Cure All Mutations",
      0, 0, 0, 0, abflag::NONE },
    { ABIL_ZIN_DONATE_GOLD, "Donate Gold", 0, 0, 0, 0, abflag::NONE },

    // The Shining One
    { ABIL_TSO_DIVINE_SHIELD, "Divine Shield", 3, 0, 50, 2, abflag::NONE },
    { ABIL_TSO_CLEANSING_FLAME, "Cleansing Flame",
      5, 0, 100, 2, abflag::NONE },
    { ABIL_TSO_SUMMON_DIVINE_WARRIOR, "Summon Divine Warrior",
      8, 0, 150, 5, abflag::NONE },
    { ABIL_TSO_BLESS_WEAPON, "Brand Weapon With Holy Wrath", 0, 0, 0, 0,
      abflag::NONE },

    // Kikubaaqudgha
    { ABIL_KIKU_RECEIVE_CORPSES, "Receive Corpses",
      3, 0, 50, 2, abflag::NONE },
    { ABIL_KIKU_TORMENT, "Torment", 4, 0, 0, 8, abflag::NONE },
    { ABIL_KIKU_GIFT_NECRONOMICON, "Receive Necronomicon", 0, 0, 0, 0,
      abflag::NONE },
    { ABIL_KIKU_BLESS_WEAPON, "Brand Weapon With Pain", 0, 0, 0, 0,
      abflag::PAIN },

    // Yredelemnul
    { ABIL_YRED_INJURY_MIRROR, "Injury Mirror", 0, 0, 0, 0, abflag::PIETY },
    { ABIL_YRED_ANIMATE_REMAINS, "Animate Remains",
      2, 0, 50, 0, abflag::NONE },
    { ABIL_YRED_RECALL_UNDEAD_SLAVES, "Recall Undead Slaves",
      2, 0, 50, 0, abflag::NONE },
    { ABIL_YRED_ANIMATE_DEAD, "Animate Dead", 2, 0, 50, 0, abflag::NONE },
    { ABIL_YRED_DRAIN_LIFE, "Drain Life", 6, 0, 200, 2, abflag::NONE },
    { ABIL_YRED_ENSLAVE_SOUL, "Enslave Soul", 8, 0, 150, 4, abflag::NONE },

    // Okawaru
    { ABIL_OKAWARU_HEROISM, "Heroism", 2, 0, 50, 1, abflag::NONE },
    { ABIL_OKAWARU_FINESSE, "Finesse", 5, 0, 100, 3, abflag::NONE },

    // Makhleb
    { ABIL_MAKHLEB_MINOR_DESTRUCTION, "Minor Destruction",
      0, scaling_cost::fixed(1), 20, 0, abflag::NONE },
    { ABIL_MAKHLEB_LESSER_SERVANT_OF_MAKHLEB, "Lesser Servant of Makhleb",
      0, scaling_cost::fixed(4), 50, 2, abflag::HOSTILE },
    { ABIL_MAKHLEB_MAJOR_DESTRUCTION, "Major Destruction",
      0, scaling_cost::fixed(6), 100, generic_cost::range(0, 1), abflag::NONE },
    { ABIL_MAKHLEB_GREATER_SERVANT_OF_MAKHLEB, "Greater Servant of Makhleb",
      0, scaling_cost::fixed(10), 100, 5, abflag::HOSTILE },

    // Sif Muna
    { ABIL_SIF_MUNA_CHANNEL_ENERGY, "Channel Energy",
      0, 0, 100, 0, abflag::NONE },
    { ABIL_SIF_MUNA_FORGET_SPELL, "Forget Spell", 5, 0, 0, 8, abflag::NONE },

    // Trog
    { ABIL_TROG_BURN_SPELLBOOKS, "Burn Spellbooks",
      0, 0, 10, 0, abflag::NONE },
    { ABIL_TROG_BERSERK, "Berserk", 0, 0, 200, 0, abflag::NONE },
    { ABIL_TROG_REGEN_MR, "Trog's Hand",
      0, 0, 50, 2, abflag::NONE },
    { ABIL_TROG_BROTHERS_IN_ARMS, "Brothers in Arms",
      0, 0, 100, generic_cost::range(5, 6), abflag::NONE },

    // Elyvilon
    { ABIL_ELYVILON_LIFESAVING, "Divine Protection",
      0, 0, 0, 0, abflag::NONE },
    { ABIL_ELYVILON_LESSER_HEALING, "Lesser Healing",
      1, 0, 100, generic_cost::range(0, 1), abflag::CONF_OK },
    { ABIL_ELYVILON_HEAL_OTHER, "Heal Other",
      2, 0, 250, 2, abflag::NONE },
    { ABIL_ELYVILON_PURIFICATION, "Purification", 3, 0, 300, 3, abflag::CONF_OK },
    { ABIL_ELYVILON_GREATER_HEALING, "Greater Healing",
      2, 0, 250, 3, abflag::CONF_OK },
    { ABIL_ELYVILON_DIVINE_VIGOUR, "Divine Vigour", 0, 0, 600, 6, abflag::CONF_OK },

    // Lugonu
    { ABIL_LUGONU_ABYSS_EXIT, "Depart the Abyss",
      1, 0, 150, 10, abflag::NONE },
    { ABIL_LUGONU_BEND_SPACE, "Bend Space", 1, 0, 50, 0, abflag::PAIN },
    { ABIL_LUGONU_BANISH, "Banish",
      4, 0, 200, generic_cost::range(3, 4), abflag::NONE },
    { ABIL_LUGONU_CORRUPT, "Corrupt",
      7, scaling_cost::fixed(5), 500, 10, abflag::NONE },
    { ABIL_LUGONU_ABYSS_ENTER, "Enter the Abyss",
      9, 0, 500, generic_cost::fixed(35), abflag::PAIN },
    { ABIL_LUGONU_BLESS_WEAPON, "Brand Weapon With Distortion", 0, 0, 0, 0,
      abflag::NONE },

    // Nemelex
    { ABIL_NEMELEX_TRIPLE_DRAW, "Triple Draw", 2, 0, 100, 2, abflag::NONE },
    { ABIL_NEMELEX_DEAL_FOUR, "Deal Four", 8, 0, 200, 8, abflag::NONE },
    { ABIL_NEMELEX_STACK_FIVE, "Stack Five", 5, 0, 250, 10, abflag::NONE },

    // Beogh
    { ABIL_BEOGH_SMITING, "Smiting",
      3, 0, 80, generic_cost::fixed(3), abflag::NONE },
    { ABIL_BEOGH_RECALL_ORCISH_FOLLOWERS, "Recall Orcish Followers",
      2, 0, 50, 0, abflag::NONE },
    { ABIL_BEOGH_GIFT_ITEM, "Give Item to Named Follower",
      0, 0, 0, 0, abflag::NONE },

    // Jiyva
    { ABIL_JIYVA_CALL_JELLY, "Request Jelly", 2, 0, 20, 1, abflag::NONE },
    { ABIL_JIYVA_JELLY_PARALYSE, "Jelly Paralyse", 3, 0, 0, 0, abflag::PIETY },
    { ABIL_JIYVA_SLIMIFY, "Slimify", 4, 0, 100, 8, abflag::NONE },
    { ABIL_JIYVA_CURE_BAD_MUTATION, "Cure Bad Mutation",
      8, 0, 200, 15, abflag::NONE },

    // Fedhas
    { ABIL_FEDHAS_EVOLUTION, "Evolution", 2, 0, 0, 0, abflag::VARIABLE_FRUIT },
    { ABIL_FEDHAS_SUNLIGHT, "Sunlight", 2, 0, 50, 0, abflag::NONE },
    { ABIL_FEDHAS_PLANT_RING, "Growth", 2, 0, 0, 0, abflag::FRUIT },
    { ABIL_FEDHAS_SPAWN_SPORES, "Reproduction", 4, 0, 100, 1, abflag::NONE },
    { ABIL_FEDHAS_RAIN, "Rain", 4, 0, 150, 4, abflag::NONE },

    // Cheibriados
    { ABIL_CHEIBRIADOS_TIME_BEND, "Bend Time", 3, 0, 50, 1, abflag::NONE },
    { ABIL_CHEIBRIADOS_DISTORTION, "Temporal Distortion",
      4, 0, 200, 3, abflag::INSTANT },
    { ABIL_CHEIBRIADOS_SLOUCH, "Slouch", 5, 0, 100, 8, abflag::NONE },
    { ABIL_CHEIBRIADOS_TIME_STEP, "Step From Time",
      10, 0, 200, 10, abflag::NONE },

    // Ashenzari
    { ABIL_ASHENZARI_SCRYING, "Scrying",
      4, 0, 50, 2, abflag::INSTANT },
    { ABIL_ASHENZARI_TRANSFER_KNOWLEDGE, "Transfer Knowledge",
      0, 0, 0, 10, abflag::NONE },
    { ABIL_ASHENZARI_END_TRANSFER, "End Transfer Knowledge",
      0, 0, 0, 0, abflag::NONE },

    // Dithmenos
    { ABIL_DITHMENOS_SHADOW_STEP, "Shadow Step",
      4, 0, 0, 4, abflag::NONE },
    { ABIL_DITHMENOS_SHADOW_FORM, "Shadow Form",
      9, 0, 0, 10, abflag::SKILL_DRAIN },

    // Ru
    { ABIL_RU_DRAW_OUT_POWER, "Draw Out Power",
      0, 0, 0, 0, abflag::EXHAUSTION|abflag::SKILL_DRAIN|abflag::CONF_OK },
    { ABIL_RU_POWER_LEAP, "Power Leap",
      5, 0, 0, 0, abflag::EXHAUSTION },
    { ABIL_RU_APOCALYPSE, "Apocalypse",
      8, 0, 0, 0, abflag::EXHAUSTION|abflag::SKILL_DRAIN },

    { ABIL_RU_SACRIFICE_PURITY, "Sacrifice Purity",
      0, 0, 0, 0, abflag::SACRIFICE },
    { ABIL_RU_SACRIFICE_WORDS, "Sacrifice Words",
      0, 0, 0, 0, abflag::SACRIFICE },
    { ABIL_RU_SACRIFICE_DRINK, "Sacrifice Drink",
      0, 0, 0, 0, abflag::SACRIFICE },
    { ABIL_RU_SACRIFICE_ESSENCE, "Sacrifice Essence",
      0, 0, 0, 0, abflag::SACRIFICE },
    { ABIL_RU_SACRIFICE_HEALTH, "Sacrifice Health",
      0, 0, 0, 0, abflag::SACRIFICE },
    { ABIL_RU_SACRIFICE_STEALTH, "Sacrifice Stealth",
      0, 0, 0, 0, abflag::SACRIFICE },
    { ABIL_RU_SACRIFICE_ARTIFICE, "Sacrifice Artifice",
      0, 0, 0, 0, abflag::SACRIFICE },
    { ABIL_RU_SACRIFICE_LOVE, "Sacrifice Love",
      0, 0, 0, 0, abflag::SACRIFICE },
    { ABIL_RU_SACRIFICE_COURAGE, "Sacrifice Courage",
      0, 0, 0, 0, abflag::SACRIFICE },
    { ABIL_RU_SACRIFICE_ARCANA, "Sacrifice Arcana",
      0, 0, 0, 0, abflag::SACRIFICE },
    { ABIL_RU_SACRIFICE_NIMBLENESS, "Sacrifice Nimbleness",
      0, 0, 0, 0, abflag::SACRIFICE },
    { ABIL_RU_SACRIFICE_DURABILITY, "Sacrifice Durability",
      0, 0, 0, 0, abflag::SACRIFICE },
    { ABIL_RU_SACRIFICE_HAND, "Sacrifice a Hand",
      0, 0, 0, 0, abflag::SACRIFICE },
    { ABIL_RU_SACRIFICE_EXPERIENCE, "Sacrifice Experience",
      0, 0, 0, 0, abflag::SACRIFICE },
    { ABIL_RU_SACRIFICE_SKILL, "Sacrifice Skill",
      0, 0, 0, 0, abflag::SACRIFICE },
    { ABIL_RU_SACRIFICE_EYE, "Sacrifice an Eye",
      0, 0, 0, 0, abflag::SACRIFICE },
    { ABIL_RU_SACRIFICE_RESISTANCE, "Sacrifice Resistance",
      0, 0, 0, 0, abflag::SACRIFICE },
    { ABIL_RU_REJECT_SACRIFICES, "Reject Sacrifices",
      0, 0, 0, 0, abflag::NONE },

    // Gozag
    { ABIL_GOZAG_POTION_PETITION, "Potion Petition",
      0, 0, 0, 0, abflag::GOLD },
    { ABIL_GOZAG_CALL_MERCHANT, "Call Merchant",
      0, 0, 0, 0, abflag::GOLD },
    { ABIL_GOZAG_BRIBE_BRANCH, "Bribe Branch",
      0, 0, 0, 0, abflag::GOLD },

    // Qazlal
    { ABIL_QAZLAL_UPHEAVAL, "Upheaval", 4, 0, 0, 3, abflag::NONE },
    { ABIL_QAZLAL_ELEMENTAL_FORCE, "Elemental Force",
      6, 0, 0, 6, abflag::NONE },
    { ABIL_QAZLAL_DISASTER_AREA, "Disaster Area", 7, 0, 0, 10, abflag::NONE },

    // Pakellas
    { ABIL_PAKELLAS_DEVICE_SURGE, "Device Surge",
        0, 0, 100, generic_cost::fixed(1),
        abflag::VARIABLE_MP | abflag::INSTANT },
    { ABIL_PAKELLAS_QUICK_CHARGE, "Quick Charge",
        0, 0, 100, 2, abflag::NONE },
    { ABIL_PAKELLAS_SUPERCHARGE, "Supercharge", 0, 0, 0, 0, abflag::NONE },

    { ABIL_STOP_RECALL, "Stop Recall", 0, 0, 0, 0, abflag::NONE },
    { ABIL_RENOUNCE_RELIGION, "Renounce Religion", 0, 0, 0, 0, abflag::NONE },
    { ABIL_CONVERT_TO_BEOGH, "Convert to Beogh", 0, 0, 0, 0, abflag::NONE },
};

static const ability_def& get_ability_def(ability_type abil)
{
    for (const ability_def &ab_def : Ability_List)
        if (ab_def.ability == abil)
            return ab_def;

    return Ability_List[0];
}

unsigned int ability_mp_cost(ability_type abil)
{
    return get_ability_def(abil).mp_cost;
}

/**
 * Is there a valid ability with a name matching that given?
 *
 * @param key   The name in question. (Not case sensitive.)
 * @return      true if such an ability exists; false if not.
 */
bool string_matches_ability_name(const string& key)
{
    return ability_by_name(key) != ABIL_NON_ABILITY;
}

/**
 * Find an ability whose name matches the given key.
 *
 * @param name      The name in question. (Not case sensitive.)
 * @return          The enum of the relevant ability, if there was one; else
 *                  ABIL_NON_ABILITY.
 */
ability_type ability_by_name(const string &key)
{
    for (const auto &abil : Ability_List)
    {
        if (abil.ability == ABIL_NON_ABILITY)
            continue;

        const string name = lowercase_string(ability_name(abil.ability));
        if (name == lowercase_string(key))
            return abil.ability;
    }

    return ABIL_NON_ABILITY;
}

string print_abilities()
{
    string text = "\n<w>a:</w> ";

    const vector<talent> talents = your_talents(false);

    if (talents.empty())
        text += "no special abilities";
    else
    {
        for (unsigned int i = 0; i < talents.size(); ++i)
        {
            if (i)
                text += ", ";
            text += ability_name(talents[i].which);
        }
    }

    return text;
}

int get_gold_cost(ability_type ability)
{
    switch (ability)
    {
    case ABIL_GOZAG_CALL_MERCHANT:
        return gozag_price_for_shop(true);
    case ABIL_GOZAG_POTION_PETITION:
        return gozag_potion_price();
    case ABIL_GOZAG_BRIBE_BRANCH:
        return GOZAG_BRIBE_AMOUNT;
    default:
        return 0;
    }
}

static const int _pakellas_quick_charge_mp_cost()
{
    return max(1, you.magic_points * 2 / 3);
}

const string make_cost_description(ability_type ability)
{
    const ability_def& abil = get_ability_def(ability);
    string ret;
    if (abil.mp_cost)
    {
        ret += make_stringf(", %d %sMP", abil.mp_cost,
            abil.flags & abflag::PERMANENT_MP ? "Permanent " : "");
    }

    if (abil.flags & abflag::VARIABLE_MP)
        ret += ", MP";

    // TODO: make this less hard-coded
    if (ability == ABIL_PAKELLAS_QUICK_CHARGE)
        ret += make_stringf(", %d MP", _pakellas_quick_charge_mp_cost());

    if (abil.hp_cost)
    {
        ret += make_stringf(", %d %sHP", abil.hp_cost.cost(you.hp_max),
            abil.flags & abflag::PERMANENT_HP ? "Permanent " : "");
    }

    if (abil.food_cost && !you_foodless(true)
        && (you.undead_state() != US_SEMI_UNDEAD
            || you.hunger_state > HS_STARVING))
    {
        ret += ", Hunger"; // randomised and exact amount hidden from player
    }

    if (abil.piety_cost || abil.flags & abflag::PIETY)
        ret += ", Piety"; // randomised and exact amount hidden from player

    if (abil.flags & abflag::BREATH)
        ret += ", Breath";

    if (abil.flags & abflag::DELAY)
        ret += ", Delay";

    if (abil.flags & abflag::PAIN)
        ret += ", Pain";

    if (abil.flags & abflag::EXHAUSTION)
        ret += ", Exhaustion";

    if (abil.flags & abflag::INSTANT)
        ret += ", Instant"; // not really a cost, more of a bonus - bwr

    if (abil.flags & abflag::FRUIT)
        ret += ", Fruit";

    if (abil.flags & abflag::VARIABLE_FRUIT)
        ret += ", Fruit or Piety";

    if (abil.flags & abflag::SKILL_DRAIN)
        ret += ", Skill drain";

    if (abil.flags & abflag::GOLD)
    {
        const int amount = get_gold_cost(ability);
        if (amount)
            ret += make_stringf(", %d Gold", amount);
        else if (ability == ABIL_GOZAG_POTION_PETITION)
            ret += ", Free";
        else
            ret += ", Gold";
    }

    if (abil.flags & abflag::SACRIFICE)
    {
        ret += ", ";
        const string prefix = "Sacrifice ";
        ret += string(ability_name(ability)).substr(prefix.size());
        ret += ru_sac_text(ability);
    }

    // If we haven't output anything so far, then the effect has no cost
    if (ret.empty())
        return "None";

    ret.erase(0, 2);
    return ret;
}

static string _get_piety_amount_str(int value)
{
    return value > 15 ? "extremely large" :
           value > 10 ? "large" :
           value > 5  ? "moderate" :
                        "small";
}

static const string _detailed_cost_description(ability_type ability)
{
    const ability_def& abil = get_ability_def(ability);
    ostringstream ret;

    bool have_cost = false;
    ret << "This ability costs: ";

    if (abil.mp_cost > 0)
    {
        have_cost = true;
        if (abil.flags & abflag::PERMANENT_MP)
            ret << "\nMax MP : ";
        else
            ret << "\nMP     : ";
        ret << abil.mp_cost;
    }
    if (abil.hp_cost)
    {
        have_cost = true;
        if (abil.flags & abflag::PERMANENT_HP)
            ret << "\nMax HP : ";
        else
            ret << "\nHP     : ";
        ret << abil.hp_cost.cost(you.hp_max);
    }

    if (abil.food_cost && !you_foodless(true)
        && (you.undead_state() != US_SEMI_UNDEAD
            || you.hunger_state > HS_STARVING))
    {
        have_cost = true;
        ret << "\nHunger : ";
        ret << hunger_cost_string(abil.food_cost + abil.food_cost / 2);
    }

    if (abil.piety_cost || abil.flags & abflag::PIETY)
    {
        have_cost = true;
        ret << "\nPiety  : ";
        if (abil.flags & abflag::PIETY)
            ret << "variable";
        else
        {
            int avgcost = abil.piety_cost.base + abil.piety_cost.add / 2;
            ret << _get_piety_amount_str(avgcost);
        }
    }

    if (abil.flags & abflag::GOLD)
    {
        have_cost = true;
        ret << "\nGold   : ";
        int gold_amount = get_gold_cost(ability);
        if (gold_amount)
            ret << gold_amount;
        else if (ability == ABIL_GOZAG_POTION_PETITION)
            ret << "free";
        else
            ret << "variable";
    }

    if (!have_cost)
        ret << "nothing.";

    if (abil.flags & abflag::BREATH)
        ret << "\nYou must catch your breath between uses of this ability.";

    if (abil.flags & abflag::DELAY)
        ret << "\nIt takes some time before being effective.";

    if (abil.flags & abflag::PAIN)
        ret << "\nUsing this ability will hurt you.";

    if (abil.flags & abflag::EXHAUSTION)
        ret << "\nIt cannot be used when exhausted.";

    if (abil.flags & abflag::INSTANT)
        ret << "\nIt is instantaneous.";

    if (abil.flags & abflag::CONF_OK)
        ret << "\nYou can use it even if confused.";

    if (abil.flags & abflag::SKILL_DRAIN)
        ret << "\nIt will temporarily drain your skills when used.";

    return ret.str();
}

ability_type fixup_ability(ability_type ability)
{
    switch (ability)
    {
    case ABIL_YRED_ANIMATE_REMAINS:
        // suppress animate remains once animate dead is unlocked (ugh)
        if (in_good_standing(GOD_YREDELEMNUL, 2))
            return ABIL_NON_ABILITY;
        return ability;

    case ABIL_YRED_RECALL_UNDEAD_SLAVES:
    case ABIL_BEOGH_RECALL_ORCISH_FOLLOWERS:
        if (!you.recall_list.empty())
            return ABIL_STOP_RECALL;
        return ability;

    case ABIL_EVOKE_BERSERK:
    case ABIL_TROG_BERSERK:
        if (you.is_lifeless_undead(false)
            || you.species == SP_FORMICID)
        {
            return ABIL_NON_ABILITY;
        }
        return ability;

    case ABIL_BLINK:
    case ABIL_EVOKE_BLINK:
        if (you.species == SP_FORMICID)
            return ABIL_NON_ABILITY;
        else
            return ability;

    case ABIL_LUGONU_ABYSS_EXIT:
    case ABIL_LUGONU_ABYSS_ENTER:
        if (brdepth[BRANCH_ABYSS] == -1)
            return ABIL_NON_ABILITY;
        else
            return ability;

    case ABIL_TSO_BLESS_WEAPON:
    case ABIL_KIKU_BLESS_WEAPON:
    case ABIL_LUGONU_BLESS_WEAPON:
        if (you.species == SP_FELID)
            return ABIL_NON_ABILITY;
        else
            return ability;

    default:
        return ability;
    }
}

talent get_talent(ability_type ability, bool check_confused)
{
    ASSERT(ability != ABIL_NON_ABILITY);

    // Placeholder handling, part 1: The ability we have might be a
    // placeholder, so convert it into its corresponding ability before
    // doing anything else, so that we'll handle its flags properly.
    talent result { fixup_ability(ability), 0, 0, false };
    const ability_def &abil = get_ability_def(result.which);

    int failure = 0;
    bool invoc = false;

    if (check_confused && you.confused()
        && !testbits(abil.flags, abflag::CONF_OK))
    {
        result.which = ABIL_NON_ABILITY;
        return result;
    }

    // Look through the table to see if there's a preference, else find
    // a new empty slot for this ability. - bwr
    const int index = find_ability_slot(abil.ability);
    result.hotkey = index >= 0 ? index_to_letter(index) : 0;

    switch (ability)
    {
    // begin spell abilities
    case ABIL_DELAYED_FIREBALL:
    case ABIL_MUMMY_RESTORATION:
    case ABIL_STOP_SINGING:
        failure = 0;
        break;

    // begin species abilities - some are mutagenic, too {dlb}
    case ABIL_SPIT_POISON:
        failure = 40
                  - 10 * player_mutation_level(MUT_SPIT_POISON)
                  - you.experience_level;
        break;

    case ABIL_BREATHE_FIRE:
    case ABIL_BREATHE_FROST:
    case ABIL_BREATHE_POISON:
    case ABIL_SPIT_ACID:
    case ABIL_BREATHE_LIGHTNING:
    case ABIL_BREATHE_POWER:
    case ABIL_BREATHE_STICKY_FLAME:
    case ABIL_BREATHE_MEPHITIC:
        failure = 30 - you.experience_level;

        if (you.form == TRAN_DRAGON)
            failure -= 20;
        break;

    case ABIL_BREATHE_STEAM:
        failure = 20 - you.experience_level;

        if (you.form == TRAN_DRAGON)
            failure -= 20;
        break;

    case ABIL_FLY:
        failure = 42 - (3 * you.experience_level);
        break;

    case ABIL_TRAN_BAT:
        failure = 45 - (2 * you.experience_level);
        break;

    case ABIL_RECHARGING:       // this is for deep dwarves {1KB}
        failure = 45 - (2 * you.experience_level);
        break;

    case ABIL_DIG:
    case ABIL_SHAFT_SELF:
        failure = 0;
        break;
        // end species abilities (some mutagenic)

        // begin demonic powers {dlb}
    case ABIL_HELLFIRE:
        failure = 50 - you.experience_level;
        break;
        // end demonic powers {dlb}

    case ABIL_BLINK:
        failure = 48 - (17 * player_mutation_level(MUT_BLINK))
                  - you.experience_level / 2;
        break;

        // begin transformation abilities {dlb}
    case ABIL_END_TRANSFORMATION:
        failure = 0;
        break;
        // end transformation abilities {dlb}

        // begin item abilities - some possibly mutagenic {dlb}
    case ABIL_EVOKE_TURN_INVISIBLE:
        failure = 60 - you.skill(SK_EVOCATIONS, 2);
        break;

    case ABIL_EVOKE_TURN_VISIBLE:
    case ABIL_STOP_FLYING:
        failure = 0;
        break;

    case ABIL_EVOKE_FLIGHT:
    case ABIL_EVOKE_BLINK:
        failure = 40 - you.skill(SK_EVOCATIONS, 2);
        break;
    case ABIL_EVOKE_BERSERK:
    case ABIL_EVOKE_FOG:
        failure = 50 - you.skill(SK_EVOCATIONS, 2);
        break;
        // end item abilities - some possibly mutagenic {dlb}

        // begin invocations {dlb}
    // Abilities with no fail rate.
    case ABIL_ZIN_CURE_ALL_MUTATIONS:
    case ABIL_ZIN_DONATE_GOLD:
    case ABIL_KIKU_BLESS_WEAPON:
    case ABIL_KIKU_GIFT_NECRONOMICON:
    case ABIL_TSO_BLESS_WEAPON:
    case ABIL_LUGONU_BLESS_WEAPON:
    case ABIL_ELYVILON_LIFESAVING:
    case ABIL_TROG_BURN_SPELLBOOKS:
    case ABIL_ASHENZARI_TRANSFER_KNOWLEDGE:
    case ABIL_ASHENZARI_END_TRANSFER:
    case ABIL_ASHENZARI_SCRYING:
    case ABIL_BEOGH_GIFT_ITEM:
    case ABIL_JIYVA_CALL_JELLY:
    case ABIL_JIYVA_CURE_BAD_MUTATION:
    case ABIL_JIYVA_JELLY_PARALYSE:
    case ABIL_GOZAG_POTION_PETITION:
    case ABIL_GOZAG_CALL_MERCHANT:
    case ABIL_GOZAG_BRIBE_BRANCH:
    case ABIL_RU_DRAW_OUT_POWER:
    case ABIL_RU_POWER_LEAP:
    case ABIL_RU_APOCALYPSE:
    case ABIL_RU_SACRIFICE_PURITY:
    case ABIL_RU_SACRIFICE_WORDS:
    case ABIL_RU_SACRIFICE_DRINK:
    case ABIL_RU_SACRIFICE_ESSENCE:
    case ABIL_RU_SACRIFICE_HEALTH:
    case ABIL_RU_SACRIFICE_STEALTH:
    case ABIL_RU_SACRIFICE_ARTIFICE:
    case ABIL_RU_SACRIFICE_LOVE:
    case ABIL_RU_SACRIFICE_COURAGE:
    case ABIL_RU_SACRIFICE_ARCANA:
    case ABIL_RU_SACRIFICE_NIMBLENESS:
    case ABIL_RU_SACRIFICE_DURABILITY:
    case ABIL_RU_SACRIFICE_HAND:
    case ABIL_RU_SACRIFICE_EXPERIENCE:
    case ABIL_RU_SACRIFICE_SKILL:
    case ABIL_RU_SACRIFICE_EYE:
    case ABIL_RU_SACRIFICE_RESISTANCE:
    case ABIL_RU_REJECT_SACRIFICES:
    case ABIL_PAKELLAS_SUPERCHARGE:
    case ABIL_STOP_RECALL:
    case ABIL_RENOUNCE_RELIGION:
    case ABIL_CONVERT_TO_BEOGH:
        invoc = true;
        failure = 0;
        break;

    // Trog and Jiyva abilities, only based on piety.
    case ABIL_TROG_BERSERK:    // piety >= 30
        invoc = true;
        failure = 0;
        break;

    case ABIL_TROG_REGEN_MR:            // piety >= 50
        invoc = true;
        failure = piety_breakpoint(2) - you.piety; // starts at 25%
        break;

    case ABIL_TROG_BROTHERS_IN_ARMS:    // piety >= 100
        invoc = true;
        failure = piety_breakpoint(5) - you.piety; // starts at 60%
        break;

    case ABIL_JIYVA_SLIMIFY:
        invoc = true;
        failure = 90 - you.piety / 2;
        break;

    // Other invocations, based on piety and Invocations skill.
    case ABIL_ELYVILON_PURIFICATION:
        invoc = true;
        failure = 20 - (you.piety / 20) - you.skill(SK_INVOCATIONS, 5);
        break;

    case ABIL_ZIN_RECITE:
    case ABIL_BEOGH_RECALL_ORCISH_FOLLOWERS:
    case ABIL_OKAWARU_HEROISM:
    case ABIL_ELYVILON_LESSER_HEALING:
    case ABIL_LUGONU_ABYSS_EXIT:
    case ABIL_FEDHAS_SUNLIGHT:
    case ABIL_FEDHAS_EVOLUTION:
    case ABIL_DITHMENOS_SHADOW_STEP:
        invoc = true;
        failure = 30 - (you.piety / 20) - you.skill(SK_INVOCATIONS, 6);
        break;

    case ABIL_YRED_ANIMATE_REMAINS:
    case ABIL_YRED_ANIMATE_DEAD:
    case ABIL_YRED_INJURY_MIRROR:
    case ABIL_CHEIBRIADOS_TIME_BEND:
        invoc = true;
        failure = 40 - (you.piety / 20) - you.skill(SK_INVOCATIONS, 4);
        break;

    case ABIL_PAKELLAS_QUICK_CHARGE:
        invoc = true;
        failure = 40 - (you.piety / 25) - you.skill(SK_EVOCATIONS, 5);
        break;

    case ABIL_ZIN_VITALISATION:
    case ABIL_TSO_DIVINE_SHIELD:
    case ABIL_BEOGH_SMITING:
    case ABIL_SIF_MUNA_FORGET_SPELL:
    case ABIL_MAKHLEB_MINOR_DESTRUCTION:
    case ABIL_MAKHLEB_LESSER_SERVANT_OF_MAKHLEB:
    case ABIL_ELYVILON_GREATER_HEALING:
    case ABIL_ELYVILON_HEAL_OTHER:
    case ABIL_LUGONU_BEND_SPACE:
    case ABIL_FEDHAS_PLANT_RING:
    case ABIL_QAZLAL_UPHEAVAL:
        invoc = true;
        failure = 40 - (you.piety / 20) - you.skill(SK_INVOCATIONS, 5);
        break;

    case ABIL_KIKU_RECEIVE_CORPSES:
        invoc = true;
        failure = 40 - (you.piety / 20) - you.skill(SK_NECROMANCY, 5);
        break;

    case ABIL_SIF_MUNA_CHANNEL_ENERGY:
        invoc = true;
        failure = 40 - (you.piety / 20) - you.skill(SK_INVOCATIONS, 2);
        break;

    case ABIL_YRED_RECALL_UNDEAD_SLAVES:
        invoc = true;
        failure = 50 - (you.piety / 20) - you.skill(SK_INVOCATIONS, 4);
        break;

    case ABIL_PAKELLAS_DEVICE_SURGE:
        invoc = true;
        failure = 40 - (you.piety / 20) - you.skill(SK_EVOCATIONS, 5);
        break;

    case ABIL_ZIN_IMPRISON:
    case ABIL_LUGONU_BANISH:
    case ABIL_CHEIBRIADOS_DISTORTION:
    case ABIL_QAZLAL_ELEMENTAL_FORCE:
        invoc = true;
        failure = 60 - (you.piety / 20) - you.skill(SK_INVOCATIONS, 5);
        break;

    case ABIL_KIKU_TORMENT:
        invoc = true;
        failure = 60 - (you.piety / 20) - you.skill(SK_NECROMANCY, 5);
        break;

    case ABIL_MAKHLEB_MAJOR_DESTRUCTION:
    case ABIL_FEDHAS_SPAWN_SPORES:
    case ABIL_YRED_DRAIN_LIFE:
    case ABIL_CHEIBRIADOS_SLOUCH:
    case ABIL_OKAWARU_FINESSE:
        invoc = true;
        failure = 60 - (you.piety / 25) - you.skill(SK_INVOCATIONS, 4);
        break;

    case ABIL_TSO_CLEANSING_FLAME:
    case ABIL_LUGONU_CORRUPT:
    case ABIL_FEDHAS_RAIN:
    case ABIL_QAZLAL_DISASTER_AREA:
        invoc = true;
        failure = 70 - (you.piety / 25) - you.skill(SK_INVOCATIONS, 4);
        break;

    case ABIL_ZIN_SANCTUARY:
    case ABIL_TSO_SUMMON_DIVINE_WARRIOR:
    case ABIL_YRED_ENSLAVE_SOUL:
    case ABIL_ELYVILON_DIVINE_VIGOUR:
    case ABIL_LUGONU_ABYSS_ENTER:
    case ABIL_CHEIBRIADOS_TIME_STEP:
    case ABIL_DITHMENOS_SHADOW_FORM:
        invoc = true;
        failure = 80 - (you.piety / 25) - you.skill(SK_INVOCATIONS, 4);
        break;

    case ABIL_MAKHLEB_GREATER_SERVANT_OF_MAKHLEB:
        invoc = true;
        failure = 90 - (you.piety / 5) - you.skill(SK_INVOCATIONS, 2);
        break;

    case ABIL_NEMELEX_STACK_FIVE:
        invoc = true;
        failure = 80 - (you.piety / 25) - you.skill(SK_EVOCATIONS, 4);
        break;

    case ABIL_NEMELEX_DEAL_FOUR:
        invoc = true;
        failure = 70 - (you.piety * 2 / 45) - you.skill(SK_EVOCATIONS, 9) / 2;
        break;

    case ABIL_NEMELEX_TRIPLE_DRAW:
        invoc = true;
        failure = 60 - (you.piety / 20) - you.skill(SK_EVOCATIONS, 5);
        break;

        // end invocations {dlb}
    default:
        failure = -1;
        break;
    }

    if (failure < 0)
        failure = 0;

    if (failure > 100)
        failure = 100;

    result.fail = failure;
    result.is_invocation = invoc;

    return result;
}

const char* ability_name(ability_type ability)
{
    return get_ability_def(ability).name;
}

vector<const char*> get_ability_names()
{
    vector<const char*> result;
    for (const talent &tal : your_talents(false))
        result.push_back(ability_name(tal.which));
    return result;
}

// XXX: should this be in describe.cc?
string get_ability_desc(const ability_type ability)
{
    const string& name = ability_name(ability);

    string lookup = getLongDescription(name + " ability");

    if (lookup.empty()) // Nothing found?
        lookup = "No description found.\n";

    if (testbits(get_ability_def(ability).flags, abflag::SACRIFICE))
    {
        lookup += "\nIf you make this sacrifice, your powers granted by Ru "
                  "will become stronger in proportion to the value of the "
                  "sacrifice, and you may gain new powers as well.\n\n"
                  "Sacrifices cannot be taken back.\n";
    }

    if (god_hates_ability(ability, you.religion))
    {
        lookup += uppercase_first(god_name(you.religion))
                  + " frowns upon the use of this ability.\n";
    }

    ostringstream res;
    res << name << "\n\n" << lookup << "\n"
        << _detailed_cost_description(ability);

    const string quote = getQuoteString(name + " ability");
    if (!quote.empty())
        res << "\n\n" << quote;

    return res.str();
}

static void _print_talent_description(const talent& tal)
{
    clrscr();

    print_description(get_ability_desc(tal.which));

    getchm();
    clrscr();
}

void no_ability_msg()
{
    // Give messages if the character cannot use innate talents right now.
    // * Vampires can't turn into bats when full of blood.
    // * Tengu can't start to fly if already flying.
    if (you.species == SP_VAMPIRE && you.experience_level >= 3)
    {
        ASSERT(you.hunger_state > HS_SATIATED);
        mpr("Sorry, you're too full to transform right now.");
    }
    else if (player_mutation_level(MUT_TENGU_FLIGHT)
             || player_mutation_level(MUT_BIG_WINGS))
    {
        if (you.airborne())
            mpr("You're already flying!");
    }
    else
        mpr("Sorry, you're not good enough to have a special ability.");
}

bool activate_ability()
{
    if (you.berserk())
    {
        canned_msg(MSG_TOO_BERSERK);
        crawl_state.zero_turns_taken();
        return false;
    }

    const bool confused = you.confused();
    vector<talent> talents = your_talents(confused);
    if (talents.empty())
    {
        if (confused)
            canned_msg(MSG_TOO_CONFUSED);
        else
            no_ability_msg();
        crawl_state.zero_turns_taken();
        return false;
    }

    int selected = -1;
#ifndef TOUCH_UI
    if (Options.ability_menu)
#endif
    {
        selected = choose_ability_menu(talents);
        if (selected == -1)
        {
            canned_msg(MSG_OK);
            crawl_state.zero_turns_taken();
            return false;
        }
    }
#ifndef TOUCH_UI
    else
    {
        while (selected < 0)
        {
            msg::streams(MSGCH_PROMPT) << "Use which ability? (? or * to list) "
                                       << endl;

            const int keyin = get_ch();

            if (keyin == '?' || keyin == '*')
            {
                selected = choose_ability_menu(talents);
                if (selected == -1)
                {
                    canned_msg(MSG_OK);
                    crawl_state.zero_turns_taken();
                    return false;
                }
            }
            else if (key_is_escape(keyin) || keyin == ' ' || keyin == '\r'
                     || keyin == '\n')
            {
                canned_msg(MSG_OK);
                crawl_state.zero_turns_taken();
                return false;
            }
            else if (isaalpha(keyin))
            {
                // Try to find the hotkey.
                for (unsigned int i = 0; i < talents.size(); ++i)
                {
                    if (talents[i].hotkey == keyin)
                    {
                        selected = static_cast<int>(i);
                        break;
                    }
                }

                // If we can't, cancel out.
                if (selected < 0)
                {
                    mpr("You can't do that.");
                    crawl_state.zero_turns_taken();
                    return false;
                }
            }
        }
    }
#endif
    return activate_talent(talents[selected]);
}

// Check prerequisites for a number of abilities.
// Abort any attempt if these cannot be met, without losing the turn.
// TODO: Many more cases need to be added!
static bool _check_ability_possible(const ability_def& abil,
                                    bool hungerCheck = true,
                                    bool quiet = false)
{
    if (you.berserk())
    {
        if (!quiet)
            canned_msg(MSG_TOO_BERSERK);
        return false;
    }

    if (you.confused() && !testbits(abil.flags, abflag::CONF_OK))
    {
        if (!quiet)
            canned_msg(MSG_TOO_CONFUSED);
        return false;
    }

    if (silenced(you.pos()))
    {
        talent tal = get_talent(abil.ability, false);
        if (tal.is_invocation)
        {
            if (!quiet)
            {
                mprf("You cannot call out to %s while silenced.",
                     god_name(you.religion).c_str());
            }
            return false;
        }
    }
    // Don't insta-starve the player.
    // (Losing consciousness possible from 400 downward.)
    if (hungerCheck && !you.undead_state())
    {
        const int expected_hunger = you.hunger - abil.food_cost * 2;
        if (!quiet)
        {
            dprf("hunger: %d, max. food_cost: %d, expected hunger: %d",
                 you.hunger, abil.food_cost * 2, expected_hunger);
        }
        // Safety margin for natural hunger, mutations etc.
        if (expected_hunger <= 50)
        {
            if (!quiet)
                canned_msg(MSG_TOO_HUNGRY);
            return false;
        }
    }

    // in case of mp rot ability, check is the player have enough natural MP
    // (avoid use of ring/staf of magical power)
    if ((abil.flags & abflag::PERMANENT_MP)
        && get_real_mp(false) < (int)abil.mp_cost)
    {
        if (!quiet)
            mpr("You don't have enough innate magic capacity to sacrifice.");
        return false;
    }

    vector<text_pattern> &actions = Options.confirm_action;
    if (!actions.empty())
    {
        const char* name = ability_name(abil.ability);
        for (const text_pattern &action : actions)
        {
            if (action.matches(name))
            {
                string prompt = "Really use " + string(name) + "?";
                if (!yesno(prompt.c_str(), false, 'n'))
                {
                    canned_msg(MSG_OK);
                    return false;
                }
                break;
            }
        }
    }

    switch (abil.ability)
    {
    case ABIL_ZIN_RECITE:
    {
        if (!zin_check_able_to_recite(quiet))
            return false;

        if (zin_check_recite_to_monsters(quiet) != 1)
        {
            if (!quiet)
                mpr("There's no appreciative audience!");
            return false;
        }
        return true;
    }

    case ABIL_ZIN_CURE_ALL_MUTATIONS:
        if (!how_mutated())
        {
            if (!quiet)
                mpr("You have no mutations to be cured!");
            return false;
        }
        return true;

    case ABIL_ZIN_SANCTUARY:
        if (env.sanctuary_time)
        {
            if (!quiet)
                mpr("There's already a sanctuary in place on this level.");
            return false;
        }
        return true;

    case ABIL_ZIN_DONATE_GOLD:
        if (!you.gold)
        {
            if (!quiet)
                mpr("You have nothing to donate!");
            return false;
        }
        return true;

    case ABIL_ELYVILON_PURIFICATION:
        if (!you.disease && !you.duration[DUR_POISONING]
            && !you.duration[DUR_CONF] && !you.duration[DUR_SLOW]
            && !you.petrifying()
            && you.strength(false) == you.max_strength()
            && you.intel(false) == you.max_intel()
            && you.dex(false) == you.max_dex()
            && !player_rotted()
            && !you.duration[DUR_WEAK])
        {
            if (!quiet)
                mpr("Nothing ails you!");
            return false;
        }
        return true;

    case ABIL_MUMMY_RESTORATION:
        if (you.strength(false) == you.max_strength()
            && you.intel(false) == you.max_intel()
            && you.dex(false) == you.max_dex()
            && !player_rotted())
        {
            if (!quiet)
                mpr("You don't need to restore your attributes or health!");
            return false;
        }
        return true;

    case ABIL_LUGONU_ABYSS_EXIT:
        if (!player_in_branch(BRANCH_ABYSS))
        {
            if (!quiet)
                mpr("You aren't in the Abyss!");
            return false;
        }
        return true;

    case ABIL_LUGONU_CORRUPT:
        return !is_level_incorruptible(quiet);

    case ABIL_LUGONU_ABYSS_ENTER:
        if (player_in_branch(BRANCH_ABYSS))
        {
            if (!quiet)
                mpr("You're already here!");
            return false;
        }
        return true;

    case ABIL_SIF_MUNA_FORGET_SPELL:
        if (you.spell_no == 0)
        {
            if (!quiet)
                canned_msg(MSG_NO_SPELLS);
            return false;
        }
        return true;

    case ABIL_ASHENZARI_TRANSFER_KNOWLEDGE:
        if (all_skills_maxed(true))
        {
            if (!quiet)
                mpr("You have nothing more to learn.");
            return false;
        }
        return true;

    case ABIL_FEDHAS_EVOLUTION:
        return fedhas_check_evolve_flora(quiet);

    case ABIL_FEDHAS_SPAWN_SPORES:
    {
        const int retval = fedhas_check_corpse_spores(quiet);
        if (retval <= 0)
        {
            if (!quiet)
            {
                if (retval == 0)
                    mpr("No corpses are in range.");
                else
                    canned_msg(MSG_OK);
            }
            return false;
        }
        return true;
    }

    case ABIL_SPIT_POISON:
    case ABIL_BREATHE_FIRE:
    case ABIL_BREATHE_FROST:
    case ABIL_BREATHE_POISON:
    case ABIL_BREATHE_LIGHTNING:
    case ABIL_SPIT_ACID:
    case ABIL_BREATHE_POWER:
    case ABIL_BREATHE_STICKY_FLAME:
    case ABIL_BREATHE_STEAM:
    case ABIL_BREATHE_MEPHITIC:
        if (you.duration[DUR_BREATH_WEAPON])
        {
            if (!quiet)
                canned_msg(MSG_CANNOT_DO_YET);
            return false;
        }
        return true;

    case ABIL_BLINK:
    case ABIL_EVOKE_BLINK:
    {
        const string no_tele_reason = you.no_tele_reason(false, true);
        if (no_tele_reason.empty())
            return true;

        if (!quiet)
             mpr(no_tele_reason);
        return false;
    }

    case ABIL_EVOKE_BERSERK:
    case ABIL_TROG_BERSERK:
        return you.can_go_berserk(true, false, true)
               && (quiet || berserk_check_wielded_weapon());

    case ABIL_EVOKE_FOG:
        if (cloud_at(you.pos()))
        {
            if (!quiet)
                mpr("It's too cloudy to do that here.");
            return false;
        }
        return true;

    case ABIL_GOZAG_POTION_PETITION:
        return gozag_setup_potion_petition(quiet);

    case ABIL_GOZAG_CALL_MERCHANT:
        return gozag_setup_call_merchant(quiet);

    case ABIL_GOZAG_BRIBE_BRANCH:
        return gozag_check_bribe_branch(quiet);

    case ABIL_RU_SACRIFICE_EXPERIENCE:
        if (you.experience_level <= RU_SAC_XP_LEVELS)
        {
            if (!quiet)
                mpr("You don't have enough experience to sacrifice.");
            return false;
        }
        return true;

    case ABIL_PAKELLAS_DEVICE_SURGE:
        if (you.magic_points == 0)
        {
            if (!quiet)
                mpr("You have no magic power.");
            return false;
        }
        return true;

    case ABIL_PAKELLAS_QUICK_CHARGE:
        return pakellas_check_quick_charge(quiet);

    default:
        return true;
    }
}

bool check_ability_possible(const ability_type ability, bool hungerCheck,
                            bool quiet)
{
    return _check_ability_possible(get_ability_def(ability), hungerCheck,
                                   quiet);
}

bool activate_talent(const talent& tal)
{
    if (you.berserk())
    {
        canned_msg(MSG_TOO_BERSERK);
        crawl_state.zero_turns_taken();
        return false;
    }

    // Doing these would outright kill the player.
    // (or, in the case of the stat-zeros, they'd at least be extremely
    // dangerous.)
    if (tal.which == ABIL_STOP_FLYING)
    {
        if (is_feat_dangerous(grd(you.pos()), false, true))
        {
            mpr("Stopping flight right now would be fatal!");
            crawl_state.zero_turns_taken();
            return false;
        }
    }
    else if (tal.which == ABIL_TRAN_BAT)
    {
        if (!check_form_stat_safety(TRAN_BAT))
        {
            crawl_state.zero_turns_taken();
            return false;
        }
    }
    else if (tal.which == ABIL_END_TRANSFORMATION)
    {
        if (feat_dangerous_for_form(TRAN_NONE, env.grid(you.pos())))
        {
            mprf("Turning back right now would cause you to %s!",
                 env.grid(you.pos()) == DNGN_LAVA ? "burn" : "drown");

            crawl_state.zero_turns_taken();
            return false;
        }

        if (!check_form_stat_safety(TRAN_NONE))
        {
            crawl_state.zero_turns_taken();
            return false;
        }
    }

    if ((tal.which == ABIL_EVOKE_BERSERK || tal.which == ABIL_TROG_BERSERK)
        && !you.can_go_berserk(true))
    {
        crawl_state.zero_turns_taken();
        return false;
    }

    if ((tal.which == ABIL_EVOKE_FLIGHT || tal.which == ABIL_TRAN_BAT || tal.which == ABIL_FLY)
        && !flight_allowed())
    {
        crawl_state.zero_turns_taken();
        return false;
    }

    // Some abilities don't need a hunger check.
    bool hungerCheck = true;
    switch (tal.which)
    {
        case ABIL_RENOUNCE_RELIGION:
        case ABIL_CONVERT_TO_BEOGH:
        case ABIL_STOP_FLYING:
        case ABIL_EVOKE_TURN_VISIBLE:
        case ABIL_END_TRANSFORMATION:
        case ABIL_DELAYED_FIREBALL:
        case ABIL_STOP_SINGING:
        case ABIL_MUMMY_RESTORATION:
        case ABIL_TRAN_BAT:
        case ABIL_ASHENZARI_END_TRANSFER:
            hungerCheck = false;
            break;
        default:
            break;
    }

    if (hungerCheck && !you.undead_state() && !you_foodless()
        && you.hunger_state <= HS_STARVING)
    {
        canned_msg(MSG_TOO_HUNGRY);
        crawl_state.zero_turns_taken();
        return false;
    }

    const ability_def& abil = get_ability_def(tal.which);

    // Check that we can afford to pay the costs.
    // Note that mutation shenanigans might leave us with negative MP,
    // so don't fail in that case if there's no MP cost.
    if (abil.mp_cost > 0 && !enough_mp(abil.mp_cost, false, true))
    {
        crawl_state.zero_turns_taken();
        return false;
    }

    const int hpcost = abil.hp_cost.cost(you.hp_max);
    if (hpcost > 0 && !enough_hp(hpcost, false))
    {
        crawl_state.zero_turns_taken();
        return false;
    }

    if (!_check_ability_possible(abil, hungerCheck))
    {
        crawl_state.zero_turns_taken();
        return false;
    }

    bool fail = random2avg(100, 3) < tal.fail;

    const spret_type ability_result = _do_ability(abil, fail);
    switch (ability_result)
    {
        case SPRET_SUCCESS:
            ASSERT(!fail || testbits(abil.flags, abflag::HOSTILE));
            practise(EX_USED_ABIL, abil.ability);
            _pay_ability_costs(abil);
            count_action(tal.is_invocation ? CACT_INVOKE : CACT_ABIL, abil.ability);
            return true;
        case SPRET_FAIL:
            mpr("You fail to use your ability.");
            you.turn_is_over = true;
            return false;
        case SPRET_ABORT:
            crawl_state.zero_turns_taken();
            return false;
        case SPRET_NONE:
        default:
            die("Weird ability return type");
            return false;
    }
}

static int _calc_breath_ability_range(ability_type ability)
{
    // Following monster draconian abilities.
    switch (ability)
    {
    case ABIL_BREATHE_FIRE:         return 6;
    case ABIL_BREATHE_FROST:        return 6;
    case ABIL_BREATHE_MEPHITIC:     return 7;
    case ABIL_BREATHE_LIGHTNING:    return 8;
    case ABIL_SPIT_ACID:            return 8;
    case ABIL_BREATHE_POWER:        return 8;
    case ABIL_BREATHE_STICKY_FLAME: return 1;
    case ABIL_BREATHE_STEAM:        return 7;
    case ABIL_BREATHE_POISON:       return 7;
    default:
        die("Bad breath type!");
        break;
    }
    return -2;
}

static bool _sticky_flame_can_hit(const actor *act)
{
    if (act->is_monster())
    {
        const monster* mons = act->as_monster();
        bolt testbeam;
        testbeam.thrower = KILL_YOU;
        zappy(ZAP_BREATHE_STICKY_FLAME, 100, testbeam);

        return !testbeam.ignores_monster(mons);
    }
    else
        return false;
}

/*
 * Use an ability.
 *
 * @param abil The actual ability used.
 * @param fail If true, the ability is doomed to fail, and SPRET_FAIL will
 * be returned if the ability is not SPRET_ABORTed.
 * @returns Whether the spell succeeded (SPRET_SUCCESS), failed (SPRET_FAIL),
 *  or was canceled (SPRET_ABORT). Never returns SPRET_NONE.
 */
static spret_type _do_ability(const ability_def& abil, bool fail)
{
    dist abild;
    bolt beam;
    dist spd;

    // Note: the costs will not be applied until after this switch
    // statement... it's assumed that only failures have returned! - bwr
    switch (abil.ability)
    {
    case ABIL_MUMMY_RESTORATION:
    {
        fail_check();
        mpr("You infuse your body with magical energy.");
        bool did_restore = restore_stat(STAT_ALL, 0, false);

        const int oldhpmax = you.hp_max;
        unrot_hp(9999);
        if (you.hp_max > oldhpmax)
            did_restore = true;

        // If nothing happened, don't take one max MP, don't use a turn.
        if (!did_restore)
        {
            canned_msg(MSG_NOTHING_HAPPENS);
            return SPRET_ABORT;
        }

        break;
    }

    case ABIL_RECHARGING:
        fail_check();
        if (recharge_wand() <= 0)
            return SPRET_ABORT; // fail message is already given
        break;

    case ABIL_DIG:
        fail_check();
        if (!you.digging)
        {
            you.digging = true;
            mpr("You extend your mandibles.");
        }
        else
        {
            mpr("You are already prepared to dig.");
            return SPRET_ABORT;
        }
        break;

    case ABIL_SHAFT_SELF:
        fail_check();
        if (you.can_do_shaft_ability(false))
        {
            if (yesno("Are you sure you want to shaft yourself?", true, 'n'))
                start_delay(DELAY_SHAFT_SELF, 1);
            else
                return SPRET_ABORT;
        }
        else
            return SPRET_ABORT;
        break;

    case ABIL_DELAYED_FIREBALL:
    {
        fail_check();
        // Note: Power level of ball calculated at release. - bwr
        int power = calc_spell_power(SPELL_DELAYED_FIREBALL, true);
        beam.range = spell_range(SPELL_FIREBALL, power);

        targetter_beam tgt(&you, beam.range, ZAP_FIREBALL, power, 1, 1);

        direction_chooser_args args;
        args.mode = TARG_HOSTILE;
        args.top_prompt = "Aiming: <white>Delayed Fireball</white>";
        args.hitfunc = &tgt;
        if (!spell_direction(spd, beam, &args))
            return SPRET_ABORT;

        if (!zapping(ZAP_FIREBALL, power, beam, true, nullptr, false))
            return SPRET_ABORT;

        // Only one allowed, since this is instantaneous. - bwr
        you.attribute[ATTR_DELAYED_FIREBALL] = 0;
        break;
    }

    case ABIL_SPIT_POISON:      // Spit poison mutation
    {
        int power = you.experience_level
                + player_mutation_level(MUT_SPIT_POISON) * 5;
        beam.range = 6;         // following Venom Bolt

        if (!spell_direction(abild, beam)
            || !player_tracer(ZAP_SPIT_POISON, power, beam))
        {
            return SPRET_ABORT;
        }
        else
        {
            fail_check();
            zapping(ZAP_SPIT_POISON, power, beam);
            you.set_duration(DUR_BREATH_WEAPON, 3 + random2(5));
        }
        break;
    }

    case ABIL_BREATHE_STICKY_FLAME:
    {
        targetter_splash hitfunc(&you);
        beam.range = 1;
        direction_chooser_args args;
        args.mode = TARG_HOSTILE;
        args.hitfunc = &hitfunc;
        if (!spell_direction(abild, beam, &args))
            return SPRET_ABORT;

        if (stop_attack_prompt(hitfunc, "spit at", _sticky_flame_can_hit))
            return SPRET_ABORT;

        fail_check();
        zapping(ZAP_BREATHE_STICKY_FLAME, (you.form == TRAN_DRAGON) ?
                2 * you.experience_level : you.experience_level,
            beam, false, "You spit a glob of burning liquid.");

        you.increase_duration(DUR_BREATH_WEAPON,
                      3 + random2(10) + random2(30 - you.experience_level));
        break;
    }

    case ABIL_BREATHE_FIRE:
    case ABIL_BREATHE_FROST:
    case ABIL_BREATHE_POISON:
    case ABIL_SPIT_ACID:
    case ABIL_BREATHE_POWER:
    case ABIL_BREATHE_STEAM:
    case ABIL_BREATHE_MEPHITIC:
        beam.range = _calc_breath_ability_range(abil.ability);
        if (!spell_direction(abild, beam))
            return SPRET_ABORT;

        // fallthrough to ABIL_BREATHE_LIGHTNING

    case ABIL_BREATHE_LIGHTNING: // not targeted
        fail_check();

        // TODO: refactor this to use only one call to zapping(), don't
        // duplicate its fail_check(), split out breathe_lightning, etc

        switch (abil.ability)
        {
        case ABIL_BREATHE_FIRE:
        {
            int power = you.experience_level;

            if (you.form == TRAN_DRAGON)
                power += 12;

            string msg = "You breathe a blast of fire";
            msg += (power < 15) ? '.' : '!';

            if (!zapping(ZAP_BREATHE_FIRE, power, beam, true, msg.c_str()))
                return SPRET_ABORT;
            break;
        }

        case ABIL_BREATHE_FROST:
            if (!zapping(ZAP_BREATHE_FROST,
                 (you.form == TRAN_DRAGON) ?
                     2 * you.experience_level : you.experience_level,
                 beam, true,
                         "You exhale a wave of freezing cold."))
            {
                return SPRET_ABORT;
            }
            break;

        case ABIL_BREATHE_POISON:
            if (!zapping(ZAP_BREATHE_POISON, you.experience_level, beam, true,
                         "You exhale a blast of poison gas."))
            {
                return SPRET_ABORT;
            }
            break;

        case ABIL_BREATHE_LIGHTNING:
            mpr("You breathe a wild blast of lightning!");
            black_drac_breath();
            break;

        case ABIL_SPIT_ACID:
            if (!zapping(ZAP_BREATHE_ACID,
                (you.form == TRAN_DRAGON) ?
                    2 * you.experience_level : you.experience_level,
                beam, true, "You spit a glob of acid."))
            {
                return SPRET_ABORT;
            }
            break;

        case ABIL_BREATHE_POWER:
            if (!zapping(ZAP_BREATHE_POWER,
                (you.form == TRAN_DRAGON) ?
                    2 * you.experience_level : you.experience_level,
                beam, true,
                         "You breathe a bolt of dispelling energy."))
            {
                return SPRET_ABORT;
            }
            break;

        case ABIL_BREATHE_STICKY_FLAME:
            if (!zapping(ZAP_BREATHE_STICKY_FLAME,
                (you.form == TRAN_DRAGON) ?
                    2 * you.experience_level : you.experience_level,
                beam, true,
                         "You spit a glob of burning liquid."))
            {
                return SPRET_ABORT;
            }
            break;

        case ABIL_BREATHE_STEAM:
            if (!zapping(ZAP_BREATHE_STEAM,
                (you.form == TRAN_DRAGON) ?
                    2 * you.experience_level : you.experience_level,
                beam, true,
                         "You exhale a blast of scalding steam."))
            {
                return SPRET_ABORT;
            }
            break;

        case ABIL_BREATHE_MEPHITIC:
            if (!zapping(ZAP_BREATHE_MEPHITIC,
                (you.form == TRAN_DRAGON) ?
                    2 * you.experience_level : you.experience_level,
                beam, true,
                         "You exhale a blast of noxious fumes."))
            {
                return SPRET_ABORT;
            }
            break;

        default:
            break;
        }

        you.increase_duration(DUR_BREATH_WEAPON,
                      3 + random2(10) + random2(30 - you.experience_level));

        if (abil.ability == ABIL_BREATHE_STEAM
            || abil.ability == ABIL_SPIT_ACID)
        {
            you.duration[DUR_BREATH_WEAPON] /= 2;
        }

        break;

    case ABIL_EVOKE_BLINK:      // randarts
        fail_check();
        if (!you_worship(GOD_PAKELLAS) && you.penance[GOD_PAKELLAS])
            pakellas_evoke_backfire(SPELL_BLINK);
        else if (!pakellas_device_surge())
            return SPRET_FAIL;
        // deliberate fall-through
    case ABIL_BLINK:            // mutation
        return cast_blink(fail);
        break;

    case ABIL_EVOKE_BERSERK:    // amulet of rage, randarts
        fail_check();
        if (!you_worship(GOD_PAKELLAS) && you.penance[GOD_PAKELLAS])
            pakellas_evoke_backfire(SPELL_BERSERKER_RAGE);
        else if (!pakellas_device_surge())
            return SPRET_FAIL;
        you.go_berserk(true);
        break;

    case ABIL_FLY:
        fail_check();
        // high level Te or Dr/Gr wings
        if (you.racial_permanent_flight())
        {
            you.attribute[ATTR_PERM_FLIGHT] = 1;
            float_player();
        }
        // low level Te
        else
        {
            int power = you.experience_level * 4;
            const int dur_change = 25 + random2(power) + random2(power);

            you.increase_duration(DUR_FLIGHT, dur_change, 100);
            you.attribute[ATTR_FLIGHT_UNCANCELLABLE] = 1;

            float_player();
        }
        if (you.species == SP_TENGU)
            mpr("You feel very comfortable in the air.");
        break;

    // DEMONIC POWERS:
    case ABIL_HELLFIRE:
        fail_check();
        if (your_spells(SPELL_HELLFIRE,
                        you.experience_level * 10,
                        false, false, true) == SPRET_ABORT)
        {
            return SPRET_ABORT;
        }
        break;

    case ABIL_EVOKE_TURN_INVISIBLE:     // ring, cloaks, randarts
        fail_check();
        if (!you_worship(GOD_PAKELLAS) && you.penance[GOD_PAKELLAS])
            pakellas_evoke_backfire(SPELL_INVISIBILITY);
        else if (!pakellas_device_surge())
            return SPRET_FAIL;
        surge_power(you.spec_evoke());
        potionlike_effect(POT_INVISIBILITY,
                          player_adjust_evoc_power(
                              you.skill(SK_EVOCATIONS, 2) + 5));
        contaminate_player(1000 + random2(2000), true);
        break;

    case ABIL_EVOKE_TURN_VISIBLE:
        fail_check();
        ASSERT(!you.attribute[ATTR_INVIS_UNCANCELLABLE]);
        mpr("You feel less transparent.");
        you.duration[DUR_INVIS] = 1;
        break;

    case ABIL_EVOKE_FLIGHT:             // ring, boots, randarts
        fail_check();
        ASSERT(!get_form()->forbids_flight());
        if (you.wearing_ego(EQ_ALL_ARMOUR, SPARM_FLYING))
        {
            bool standing = !you.airborne();
            you.attribute[ATTR_PERM_FLIGHT] = 1;
            if (standing)
                float_player();
            else
                mpr("You feel more buoyant.");
        }
        else
        {
            if (!you_worship(GOD_PAKELLAS) && you.penance[GOD_PAKELLAS])
                pakellas_evoke_backfire(SPELL_FLY);
            else if (!pakellas_device_surge())
                return SPRET_FAIL;
            surge_power(you.spec_evoke());
            fly_player(
                player_adjust_evoc_power(you.skill(SK_EVOCATIONS, 2) + 30));
        }
        break;
    case ABIL_EVOKE_FOG:     // cloak of the Thief
        fail_check();
        mpr("With a swish of your cloak, you release a cloud of fog.");
        big_cloud(random_smoke_type(), &you, you.pos(), 50, 8 + random2(8));
        break;

    case ABIL_STOP_SINGING:
        fail_check();
        you.duration[DUR_SONG_OF_SLAYING] = 0;
        mpr("You stop singing.");
        break;

    case ABIL_STOP_FLYING:
        fail_check();
        you.duration[DUR_FLIGHT] = 0;
        you.attribute[ATTR_PERM_FLIGHT] = 0;
        land_player();
        break;

    case ABIL_END_TRANSFORMATION:
        fail_check();
        untransform();
        break;

    // INVOCATIONS:
    case ABIL_ZIN_RECITE:
    {
        fail_check();
        surge_power(you.spec_invoc(), "divine");
        if (zin_check_recite_to_monsters() == 1)
        {
            you.attribute[ATTR_RECITE_TYPE] = (recite_type) random2(NUM_RECITE_TYPES); // This is just flavor
            you.attribute[ATTR_RECITE_SEED] = random2(2187); // 3^7
            you.duration[DUR_RECITE] = 3 * BASELINE_DELAY;
            mprf("You clear your throat and prepare to recite.");
            you.increase_duration(DUR_BREATH_WEAPON,
                                  3 + random2(10) + random2(30));
        }
        else
        {
            canned_msg(MSG_OK);
            return SPRET_ABORT;
        }
        break;
    }
    case ABIL_ZIN_VITALISATION:
        fail_check();
        zin_vitalisation();
        break;

    case ABIL_ZIN_IMPRISON:
    {
        beam.range = LOS_RADIUS;
        direction_chooser_args args;
        args.restricts = DIR_TARGET;
        args.mode = TARG_HOSTILE;
        args.needs_path = false;
        if (!spell_direction(spd, beam, &args))
            return SPRET_ABORT;

        if (beam.target == you.pos())
        {
            mpr("You cannot imprison yourself!");
            return SPRET_ABORT;
        }

        monster* mons = monster_at(beam.target);

        if (mons == nullptr || !you.can_see(*mons))
        {
            mpr("There is no monster there to imprison!");
            return SPRET_ABORT;
        }

        if (mons_is_firewood(mons) || mons_is_conjured(mons->type))
        {
            mpr("You cannot imprison that!");
            return SPRET_ABORT;
        }

        if (mons->friendly() || mons->good_neutral())
        {
            mpr("You cannot imprison a law-abiding creature!");
            return SPRET_ABORT;
        }

        fail_check();

        int power = player_adjust_invoc_power(
            3 + (roll_dice(5, you.skill(SK_INVOCATIONS, 5) + 12) / 26));

        if (!cast_imprison(power, mons, -GOD_ZIN))
            return SPRET_ABORT;
        break;
    }

    case ABIL_ZIN_SANCTUARY:
        fail_check();
        zin_sanctuary();
        break;

    case ABIL_ZIN_CURE_ALL_MUTATIONS:
        fail_check();
        if (!zin_remove_all_mutations())
            return SPRET_ABORT;
        break;

    case ABIL_ZIN_DONATE_GOLD:
        fail_check();
        zin_donate_gold();
        break;

    case ABIL_TSO_DIVINE_SHIELD:
        fail_check();
        tso_divine_shield();
        break;

    case ABIL_TSO_CLEANSING_FLAME:
        fail_check();
        surge_power(you.spec_invoc(), "divine");
        cleansing_flame(
            player_adjust_invoc_power(10 + you.skill_rdiv(SK_INVOCATIONS, 7, 6)),
            CLEANSING_FLAME_INVOCATION, you.pos(), &you);
        break;

    case ABIL_TSO_SUMMON_DIVINE_WARRIOR:
        fail_check();
        surge_power(you.spec_invoc(), "divine");
        summon_holy_warrior(
            player_adjust_invoc_power(you.skill(SK_INVOCATIONS, 4)), false);
        break;

    case ABIL_TSO_BLESS_WEAPON:
        fail_check();
        simple_god_message(" will bless one of your weapons.");
        // included in default force_more_message
        if (!bless_weapon(GOD_SHINING_ONE, SPWPN_HOLY_WRATH, YELLOW))
            return SPRET_ABORT;
        break;

    case ABIL_KIKU_RECEIVE_CORPSES:
        fail_check();
        kiku_receive_corpses(you.skill(SK_NECROMANCY, 4));
        break;

    case ABIL_KIKU_TORMENT:
        fail_check();
        if (!kiku_take_corpse())
        {
            mpr("There are no corpses to sacrifice!");
            return SPRET_ABORT;
        }
        simple_god_message(" torments the living!");
        torment(&you, TORMENT_KIKUBAAQUDGHA, you.pos());
        break;

    case ABIL_KIKU_BLESS_WEAPON:
        fail_check();
        simple_god_message(" will bloody one of your weapons with pain.");
        // included in default force_more_message
        if (!bless_weapon(GOD_KIKUBAAQUDGHA, SPWPN_PAIN, RED))
            return SPRET_ABORT;
        break;

    case ABIL_KIKU_GIFT_NECRONOMICON:
    {
        fail_check();
        if (!kiku_gift_necronomicon())
            return SPRET_ABORT;
        break;
    }

    case ABIL_YRED_INJURY_MIRROR:
        fail_check();
        if (yred_injury_mirror())
            mpr("Another wave of unholy energy enters you.");
        else
        {
            mprf("You offer yourself to %s, and fill with unholy energy.",
                 god_name(you.religion).c_str());
        }
        you.duration[DUR_MIRROR_DAMAGE] = 9 * BASELINE_DELAY
                     + random2avg(you.piety * BASELINE_DELAY, 2) / 10;
        break;

    case ABIL_YRED_ANIMATE_REMAINS:
        fail_check();
        canned_msg(MSG_ANIMATE_REMAINS);
        if (animate_remains(you.pos(), CORPSE_BODY, BEH_FRIENDLY,
                            MHITYOU, &you, "", GOD_YREDELEMNUL) < 0)
        {
            mpr("There are no remains here to animate!");
            return SPRET_ABORT;
        }
        break;

    case ABIL_YRED_ANIMATE_DEAD:
        fail_check();
        surge_power(you.spec_invoc(), "divine");
        canned_msg(MSG_CALL_DEAD);

        animate_dead(&you,
                     player_adjust_invoc_power(
                         you.skill_rdiv(SK_INVOCATIONS) + 1),
                     BEH_FRIENDLY, MHITYOU, &you, "", GOD_YREDELEMNUL);
        break;

    case ABIL_YRED_RECALL_UNDEAD_SLAVES:
        fail_check();
        start_recall(RECALL_YRED);
        break;

    case ABIL_YRED_DRAIN_LIFE:
        fail_check();
        surge_power(you.spec_invoc(), "divine");
        cast_los_attack_spell(
            SPELL_DRAIN_LIFE,
            player_adjust_invoc_power(you.skill_rdiv(SK_INVOCATIONS)),
            &you, true);
        break;

    case ABIL_YRED_ENSLAVE_SOUL:
    {
        god_acting gdact;
        int power = player_adjust_invoc_power(you.skill(SK_INVOCATIONS, 4));
        beam.range = LOS_RADIUS;

        if (!spell_direction(spd, beam))
            return SPRET_ABORT;

        if (beam.target == you.pos())
        {
            mpr("Your soul already belongs to Yredelemnul.");
            return SPRET_ABORT;
        }

        monster* mons = monster_at(beam.target);
        if (mons == nullptr || !you.can_see(*mons)
            || !ench_flavour_affects_monster(BEAM_ENSLAVE_SOUL, mons))
        {
            mpr("You see nothing there you can enslave the soul of!");
            return SPRET_ABORT;
        }

        // The monster can be no more than lightly wounded/damaged.
        if (mons_get_damage_level(mons) > MDAM_LIGHTLY_DAMAGED)
        {
            simple_monster_message(mons, "'s soul is too badly injured.");
            return SPRET_ABORT;
        }
        fail_check();
        surge_power(you.spec_invoc(), "divine");
        return zapping(ZAP_ENSLAVE_SOUL, power, beam, false, nullptr, fail);
    }

    case ABIL_SIF_MUNA_CHANNEL_ENERGY:
        fail_check();
        surge_power(you.spec_invoc(), "divine");
        mpr("You channel some magical energy.");

        inc_mp(player_adjust_invoc_power(
                   1 + random2(you.skill_rdiv(SK_INVOCATIONS, 1, 4) + 2)));
        break;

    case ABIL_OKAWARU_HEROISM:
        fail_check();
        surge_power(you.spec_invoc(), "divine");
        mprf(MSGCH_DURATION, you.duration[DUR_HEROISM]
             ? "You feel more confident with your borrowed prowess."
             : "You gain the combat prowess of a mighty hero.");

        you.increase_duration(DUR_HEROISM,
            player_adjust_invoc_power(
                10 + random2avg(you.skill(SK_INVOCATIONS, 6), 2)),
            100);
        you.redraw_evasion      = true;
        you.redraw_armour_class = true;
        break;

    case ABIL_OKAWARU_FINESSE:
        fail_check();
        surge_power(you.spec_invoc(), "divine");
        if (you.duration[DUR_FINESSE])
        {
            // "Your [hand(s)] get{s} new energy."
            mprf(MSGCH_DURATION, "%s",
                 you.hands_act("get", "new energy.").c_str());
        }
        else
            mprf(MSGCH_DURATION, "You can now deal lightning-fast blows.");

        you.increase_duration(DUR_FINESSE,
            player_adjust_invoc_power(
                10 + random2avg(you.skill(SK_INVOCATIONS, 6), 2)),
            100);

        did_god_conduct(DID_HASTY, 8); // Currently irrelevant.
        break;

    case ABIL_MAKHLEB_MINOR_DESTRUCTION:
    {
        beam.range = LOS_RADIUS;

        if (!spell_direction(spd, beam))
            return SPRET_ABORT;

        int power = player_adjust_invoc_power(
                    you.skill(SK_INVOCATIONS, 1)
                    + random2(1 + you.skill(SK_INVOCATIONS, 1))
                    + random2(1 + you.skill(SK_INVOCATIONS, 1)));

        // Since the actual beam is random, check with BEAM_MMISSILE and the
        // highest range possible.
        if (!player_tracer(ZAP_DEBUGGING_RAY, power, beam, LOS_RADIUS))
            return SPRET_ABORT;

        fail_check();
        surge_power(you.spec_invoc(), "divine");

        switch (random2(5))
        {
        case 0: zapping(ZAP_THROW_FLAME, power, beam); break;
        case 1: zapping(ZAP_PAIN,  power, beam); break;
        case 2: zapping(ZAP_STONE_ARROW, power, beam); break;
        case 3: zapping(ZAP_SHOCK, power, beam); break;
        case 4: zapping(ZAP_BREATHE_ACID, power/2, beam); break;
        }
        break;
    }

    case ABIL_MAKHLEB_LESSER_SERVANT_OF_MAKHLEB:
        surge_power(you.spec_invoc(), "divine");
        summon_demon_type(random_choose(MONS_HELLWING, MONS_NEQOXEC,
                          MONS_ORANGE_DEMON, MONS_SMOKE_DEMON, MONS_YNOXINUL),
                          player_adjust_invoc_power(
                              20 + you.skill(SK_INVOCATIONS, 3)),
                          GOD_MAKHLEB, 0, !fail);
        break;

    case ABIL_MAKHLEB_MAJOR_DESTRUCTION:
    {
        beam.range = 6;

        if (!spell_direction(spd, beam))
            return SPRET_ABORT;

        int power = player_adjust_invoc_power(
                    you.skill(SK_INVOCATIONS, 1)
                    + random2(1 + you.skill(SK_INVOCATIONS, 1))
                    + random2(1 + you.skill(SK_INVOCATIONS, 1)));

        // Since the actual beam is random, check with BEAM_MMISSILE and the
        // highest range possible.
        if (!player_tracer(ZAP_DEBUGGING_RAY, power, beam, LOS_RADIUS))
            return SPRET_ABORT;

        fail_check();
        surge_power(you.spec_invoc(), "divine");
        {
            zap_type ztype =
                random_choose(ZAP_BOLT_OF_FIRE,
                              ZAP_FIREBALL,
                              ZAP_LIGHTNING_BOLT,
                              ZAP_STICKY_FLAME,
                              ZAP_IRON_SHOT,
                              ZAP_BOLT_OF_DRAINING,
                              ZAP_ORB_OF_ELECTRICITY);
            zapping(ztype, power, beam);
        }
        break;
    }

    case ABIL_MAKHLEB_GREATER_SERVANT_OF_MAKHLEB:
        surge_power(you.spec_invoc(), "divine");
        summon_demon_type(random_choose(MONS_EXECUTIONER, MONS_GREEN_DEATH,
                          MONS_BLIZZARD_DEMON, MONS_BALRUG, MONS_CACODEMON),
                          player_adjust_invoc_power(
                              20 + you.skill(SK_INVOCATIONS, 3)),
                          GOD_MAKHLEB, 0, !fail);
        break;

    case ABIL_TROG_BURN_SPELLBOOKS:
        fail_check();
        if (!trog_burn_spellbooks())
            return SPRET_ABORT;
        break;

    case ABIL_TROG_BERSERK:
        fail_check();
        // Trog abilities don't use or train invocations.
        you.go_berserk(true);
        break;

    case ABIL_TROG_REGEN_MR:
        fail_check();
        // Trog abilities don't use or train invocations.
        trog_do_trogs_hand(you.piety / 2);
        break;

    case ABIL_TROG_BROTHERS_IN_ARMS:
        fail_check();
        // Trog abilities don't use or train invocations.
        summon_berserker(you.piety +
                         random2(you.piety/4) - random2(you.piety/4),
                         &you);
        break;

    case ABIL_SIF_MUNA_FORGET_SPELL:
        fail_check();
        if (cast_selective_amnesia() <= 0)
            return SPRET_ABORT;
        break;

    case ABIL_ELYVILON_LIFESAVING:
        fail_check();
        if (you.duration[DUR_LIFESAVING])
            mpr("You renew your call for help.");
        else
        {
            mprf("You beseech %s to protect your life.",
                 god_name(you.religion).c_str());
        }
        // Might be a decrease, this is intentional (like Yred).
        you.duration[DUR_LIFESAVING] = 9 * BASELINE_DELAY
                     + random2avg(you.piety * BASELINE_DELAY, 2) / 10;
        break;

    case ABIL_ELYVILON_LESSER_HEALING:
    case ABIL_ELYVILON_GREATER_HEALING:
    {
        fail_check();
        surge_power(you.spec_invoc(), "divine");
        int pow = 0;
        if (abil.ability == ABIL_ELYVILON_LESSER_HEALING)
        {
            pow = player_adjust_invoc_power(
                3 + (you.skill_rdiv(SK_INVOCATIONS, 1, 6)));
        }
        else
        {
            pow = player_adjust_invoc_power(
                10 + (you.skill_rdiv(SK_INVOCATIONS, 1, 3)));
        }
#if TAG_MAJOR_VERSION == 34
        if (you.species == SP_DJINNI)
            pow /= 2;
#endif
        pow = min(50, pow);
        const int healed = pow + roll_dice(2, pow) - 2;
        mpr("You are healed.");
        inc_hp(healed);
        break;
    }

    case ABIL_ELYVILON_PURIFICATION:
        fail_check();
        elyvilon_purification();
        break;

    case ABIL_ELYVILON_HEAL_OTHER:
    {
        int pow = player_adjust_invoc_power(
            10 + (you.skill_rdiv(SK_INVOCATIONS, 1, 3)));
        pow = min(50, pow);
        int max_pow = player_adjust_invoc_power(
            10 + (int) ceil(you.skill(SK_INVOCATIONS, 1) / 3.0));
        max_pow = min(50, max_pow);
        return cast_healing(pow, max_pow, fail);
    }

    case ABIL_ELYVILON_DIVINE_VIGOUR:
        fail_check();
        if (!elyvilon_divine_vigour())
            return SPRET_ABORT;
        break;

    case ABIL_LUGONU_ABYSS_EXIT:
        fail_check();
        down_stairs(DNGN_EXIT_ABYSS);
        break;

    case ABIL_LUGONU_BEND_SPACE:
        fail_check();
        lugonu_bend_space();
        break;

    case ABIL_LUGONU_BANISH:
    {
        beam.range = LOS_RADIUS;
        const int pow =
            player_adjust_invoc_power(16 + you.skill(SK_INVOCATIONS, 8));

        direction_chooser_args args;
        args.mode = TARG_HOSTILE;
        args.get_desc_func = bind(desc_success_chance, placeholders::_1,
                                  zap_ench_power(ZAP_BANISHMENT, pow), false,
                                  nullptr);
        if (!spell_direction(spd, beam, &args))
            return SPRET_ABORT;

        if (beam.target == you.pos())
        {
            mpr("You cannot banish yourself!");
            return SPRET_ABORT;
        }

        fail_check();
        surge_power(you.spec_invoc(), "divine");

        return zapping(ZAP_BANISHMENT, pow, beam, true, nullptr, fail);
    }

    case ABIL_LUGONU_CORRUPT:
        fail_check();
        surge_power(you.spec_invoc(), "divine");
        if (!lugonu_corrupt_level(300 + you.skill(SK_INVOCATIONS, 15)))
            return SPRET_ABORT;
        break;

    case ABIL_LUGONU_ABYSS_ENTER:
    {
        fail_check();
        // Deflate HP.
        dec_hp(random2avg(you.hp, 2), false);

        // Deflate MP.
        if (you.magic_points)
            dec_mp(random2avg(you.magic_points, 2));

        no_notes nx; // This banishment shouldn't be noted.
        banished();
        break;
    }

    case ABIL_LUGONU_BLESS_WEAPON:
        fail_check();
        simple_god_message(" will brand one of your weapons with the "
                           "corruption of the Abyss.");
        // included in default force_more_message
        if (!bless_weapon(GOD_LUGONU, SPWPN_DISTORTION, MAGENTA))
            return SPRET_ABORT;
        break;

    case ABIL_NEMELEX_TRIPLE_DRAW:
        fail_check();
        if (!deck_triple_draw())
            return SPRET_ABORT;
        break;

    case ABIL_NEMELEX_DEAL_FOUR:
        fail_check();
        if (!deck_deal())
            return SPRET_ABORT;
        break;

    case ABIL_NEMELEX_STACK_FIVE:
        fail_check();
        if (!deck_stack())
            return SPRET_ABORT;
        break;

    case ABIL_BEOGH_SMITING:
        fail_check();
        surge_power(you.spec_invoc(), "divine");
        if (your_spells(SPELL_SMITING,
                        player_adjust_invoc_power(
                            12 + skill_bump(SK_INVOCATIONS, 6)),
                        false, false, true) == SPRET_ABORT)
        {
            return SPRET_ABORT;
        }
        break;

    case ABIL_BEOGH_GIFT_ITEM:
        if (!beogh_gift_item())
            return SPRET_ABORT;
        break;

    case ABIL_BEOGH_RECALL_ORCISH_FOLLOWERS:
        fail_check();
        start_recall(RECALL_BEOGH);
        break;

    case ABIL_STOP_RECALL:
        fail_check();
        mpr("You stop recalling your allies.");
        end_recall();
        break;

    case ABIL_FEDHAS_SUNLIGHT:
        return fedhas_sunlight(fail);

    case ABIL_FEDHAS_PLANT_RING:
        fail_check();
        if (!fedhas_plant_ring_from_fruit())
            return SPRET_ABORT;
        break;

    case ABIL_FEDHAS_RAIN:
        fail_check();
        if (!fedhas_rain(you.pos()))
        {
            canned_msg(MSG_NOTHING_HAPPENS);
            return SPRET_ABORT;
        }
        break;

    case ABIL_FEDHAS_SPAWN_SPORES:
    {
        fail_check();
        const int num = fedhas_corpse_spores();
        ASSERT(num > 0);
        break;
    }

    case ABIL_FEDHAS_EVOLUTION:
        return fedhas_evolve_flora(fail);

    case ABIL_TRAN_BAT:
        fail_check();
        if (!transform(100, TRAN_BAT))
        {
            crawl_state.zero_turns_taken();
            return SPRET_ABORT;
        }
        break;

    case ABIL_JIYVA_CALL_JELLY:
    {
        fail_check();
        mgen_data mg(MONS_JELLY, BEH_STRICT_NEUTRAL, 0, 0, 0, you.pos(),
                     MHITNOT, MG_NONE, GOD_JIYVA);

        mg.non_actor_summoner = "Jiyva";

        if (!create_monster(mg))
            return SPRET_ABORT;
        break;
    }

    case ABIL_JIYVA_JELLY_PARALYSE:
        fail_check();
        jiyva_paralyse_jellies();
        break;

    case ABIL_JIYVA_SLIMIFY:
    {
        fail_check();
        const item_def* const weapon = you.weapon();
        const string msg = (weapon) ? weapon->name(DESC_YOUR)
                                    : ("your " + you.hand_name(true));
        mprf(MSGCH_DURATION, "A thick mucus forms on %s.", msg.c_str());
        you.increase_duration(DUR_SLIMIFY,
                              random2avg(you.piety / 4, 2) + 3, 100);
        break;
    }

    case ABIL_JIYVA_CURE_BAD_MUTATION:
        fail_check();
        jiyva_remove_bad_mutation();
        break;

    case ABIL_CHEIBRIADOS_TIME_STEP:
        fail_check();
        surge_power(you.spec_invoc(), "divine");
        cheibriados_time_step(
            player_adjust_invoc_power(
                you.skill(SK_INVOCATIONS, 10) * you.piety / 100));
        break;

    case ABIL_CHEIBRIADOS_TIME_BEND:
        fail_check();
        surge_power(you.spec_invoc(), "divine");
        cheibriados_time_bend(
            player_adjust_invoc_power(16 + you.skill(SK_INVOCATIONS, 8)));
        break;

    case ABIL_CHEIBRIADOS_DISTORTION:
        fail_check();
        cheibriados_temporal_distortion();
        break;

    case ABIL_CHEIBRIADOS_SLOUCH:
        fail_check();
        if (!cheibriados_slouch())
            return SPRET_ABORT;
        break;

    case ABIL_ASHENZARI_SCRYING:
        fail_check();
        if (you.duration[DUR_SCRYING])
            mpr("You extend your astral sight.");
        else
            mpr("You gain astral sight.");
        you.duration[DUR_SCRYING] = 100 + random2avg(you.piety * 2, 2);
        you.xray_vision = true;
        viewwindow(true);
        break;

    case ABIL_ASHENZARI_TRANSFER_KNOWLEDGE:
        fail_check();
        if (!ashenzari_transfer_knowledge())
        {
            canned_msg(MSG_OK);
            return SPRET_ABORT;
        }
        break;

    case ABIL_ASHENZARI_END_TRANSFER:
        fail_check();
        if (!ashenzari_end_transfer())
        {
            canned_msg(MSG_OK);
            return SPRET_ABORT;
        }
        break;

    case ABIL_DITHMENOS_SHADOW_STEP:
        fail_check();
        if (!dithmenos_shadow_step())
        {
            canned_msg(MSG_OK);
            return SPRET_ABORT;
        }
        break;

    case ABIL_DITHMENOS_SHADOW_FORM:
        fail_check();
        surge_power(you.spec_invoc(), "divine");
        if (!transform(
                player_adjust_invoc_power(you.skill(SK_INVOCATIONS, 2)),
                TRAN_SHADOW))
        {
            crawl_state.zero_turns_taken();
            return SPRET_ABORT;
        }
        break;

    case ABIL_GOZAG_POTION_PETITION:
        fail_check();
        run_uncancel(UNC_POTION_PETITION, 0);
        break;

    case ABIL_GOZAG_CALL_MERCHANT:
        fail_check();
        run_uncancel(UNC_CALL_MERCHANT, 0);
        break;

    case ABIL_GOZAG_BRIBE_BRANCH:
        fail_check();
        if (!gozag_bribe_branch())
            return SPRET_ABORT;
        break;

    case ABIL_QAZLAL_UPHEAVAL:
        return qazlal_upheaval(coord_def(), false, fail);

    case ABIL_QAZLAL_ELEMENTAL_FORCE:
        fail_check();
        qazlal_elemental_force();
        break;

    case ABIL_QAZLAL_DISASTER_AREA:
        fail_check();
        if (!qazlal_disaster_area())
            return SPRET_ABORT;
        break;

    case ABIL_RU_SACRIFICE_PURITY:
    case ABIL_RU_SACRIFICE_WORDS:
    case ABIL_RU_SACRIFICE_DRINK:
    case ABIL_RU_SACRIFICE_ESSENCE:
    case ABIL_RU_SACRIFICE_HEALTH:
    case ABIL_RU_SACRIFICE_STEALTH:
    case ABIL_RU_SACRIFICE_ARTIFICE:
    case ABIL_RU_SACRIFICE_LOVE:
    case ABIL_RU_SACRIFICE_COURAGE:
    case ABIL_RU_SACRIFICE_ARCANA:
    case ABIL_RU_SACRIFICE_NIMBLENESS:
    case ABIL_RU_SACRIFICE_DURABILITY:
    case ABIL_RU_SACRIFICE_HAND:
    case ABIL_RU_SACRIFICE_EXPERIENCE:
    case ABIL_RU_SACRIFICE_SKILL:
    case ABIL_RU_SACRIFICE_EYE:
    case ABIL_RU_SACRIFICE_RESISTANCE:
        fail_check();
        if (!ru_do_sacrifice(abil.ability))
            return SPRET_ABORT;
        break;

    case ABIL_RU_REJECT_SACRIFICES:
        fail_check();
        if (!ru_reject_sacrifices())
            return SPRET_ABORT;
        break;

    case ABIL_RU_DRAW_OUT_POWER:
        fail_check();
        if (you.duration[DUR_EXHAUSTED])
        {
            mpr("You're too exhausted to draw out your power.");
            return SPRET_ABORT;
        }
        if (you.hp == you.hp_max && you.magic_points == you.max_magic_points
            && !you.duration[DUR_CONF]
            && !you.duration[DUR_SLOW]
            && !you.attribute[ATTR_HELD]
            && !you.petrifying()
            && !you.is_constricted())
        {
            mpr("You have no need to draw out power.");
            return SPRET_ABORT;
        }
        ru_draw_out_power();
        you.increase_duration(DUR_EXHAUSTED, 12 + random2(5));
        break;

    case ABIL_RU_POWER_LEAP:
        fail_check();
        if (you.duration[DUR_EXHAUSTED])
        {
            mpr("You're too exhausted to power leap.");
            return SPRET_ABORT;
        }
        if (!ru_power_leap())
        {
            canned_msg(MSG_OK);
            return SPRET_ABORT;
        }
        you.increase_duration(DUR_EXHAUSTED, 18 + random2(8));
        break;

    case ABIL_RU_APOCALYPSE:
        fail_check();
        if (you.duration[DUR_EXHAUSTED])
        {
            mpr("You're too exhausted to unleash your apocalyptic power.");
            return SPRET_ABORT;
        }
        if (!ru_apocalypse())
            return SPRET_ABORT;
        you.increase_duration(DUR_EXHAUSTED, 30 + random2(20));
        break;

    case ABIL_PAKELLAS_DEVICE_SURGE:
    {
        fail_check();

        mprf(MSGCH_DURATION, "You feel a buildup of energy.");
        you.increase_duration(DUR_DEVICE_SURGE,
                              random2avg(you.piety / 4, 2) + 3, 100);
        break;
    }

    case ABIL_PAKELLAS_QUICK_CHARGE:
    {
        fail_check();

        const int mp_to_use = _pakellas_quick_charge_mp_cost();
        ASSERT(mp_to_use > 0);

        const int den = 100 * (get_real_mp(false) - you.mp_max_adj);
        const int num =
            stepdown(random2avg(you.skill(SK_EVOCATIONS, 10), 2) * mp_to_use,
                     den / 3);

        if (recharge_wand(true, "", num, den) <= 0)
        {
            canned_msg(MSG_OK);
            return SPRET_ABORT;
        }

        dec_mp(mp_to_use);

        break;
    }

    case ABIL_PAKELLAS_SUPERCHARGE:
    {
        fail_check();
        simple_god_message(" will supercharge a wand or rod.");
        // included in default force_more_message

        int item_slot = prompt_invent_item("Supercharge what?", MT_INVLIST,
                                           OSEL_SUPERCHARGE, true, true, false);

        if (item_slot == PROMPT_NOTHING || item_slot == PROMPT_ABORT)
            return SPRET_ABORT;

        item_def& wand(you.inv[item_slot]);

        string prompt = "Do you wish to have " + wand.name(DESC_YOUR)
                           + " supercharged?";

        if (!yesno(prompt.c_str(), true, 'n'))
        {
            canned_msg(MSG_OK);
            return SPRET_ABORT;
        }

        if (wand.base_type == OBJ_RODS)
        {
            wand.charge_cap = wand.charges =
                (MAX_ROD_CHARGE + 1) * ROD_CHARGE_MULT;
            wand.rod_plus = MAX_WPN_ENCHANT + 1;
        }
        else
        {
            set_ident_flags(wand, ISFLAG_KNOW_PLUSES);
            wand.charges = 9 * wand_charge_value(wand.sub_type) / 2;
            wand.used_count = ZAPCOUNT_RECHARGED;
            wand.props[PAKELLAS_SUPERCHARGE_KEY].get_bool() = true;
        }

        you.wield_change = true;
        you.one_time_ability_used.set(GOD_PAKELLAS);

        take_note(Note(NOTE_ID_ITEM, 0, 0, wand.name(DESC_A).c_str(),
                  "supercharged by Pakellas"));

        mprf(MSGCH_GOD, "Your %s glows brightly!",
             wand.name(DESC_QUALNAME).c_str());

        flash_view(UA_PLAYER, LIGHTGREEN);

        simple_god_message(" booms: Use this gift wisely!");

#ifndef USE_TILE_LOCAL
        // Allow extra time for the flash to linger.
        delay(1000);
#endif
        break;
    }

    case ABIL_RENOUNCE_RELIGION:
        fail_check();
        if (yesno("Really renounce your faith, foregoing its fabulous benefits?",
                  false, 'n')
            && yesno("Are you sure you won't change your mind later?",
                     false, 'n'))
        {
            excommunication(true);
        }
        else
        {
            canned_msg(MSG_OK);
            return SPRET_ABORT;
        }
        break;

    case ABIL_CONVERT_TO_BEOGH:
        fail_check();
        god_pitch(GOD_BEOGH);
        if (you_worship(GOD_BEOGH))
        {
            spare_beogh_convert();
            break;
        }
        return SPRET_ABORT;

    case ABIL_NON_ABILITY:
        fail_check();
        mpr("Sorry, you can't do that.");
        break;

    default:
        die("invalid ability");
    }

    return SPRET_SUCCESS;
}

// [ds] Increase piety cost for god abilities that are particularly
// overpowered in Sprint. Yes, this is a hack. No, I don't care.
static int _scale_piety_cost(ability_type abil, int original_cost)
{
    // Abilities that have aroused our ire earn 2.5x their classic
    // Crawl piety cost.
    return (crawl_state.game_is_sprint()
            && (abil == ABIL_TROG_BROTHERS_IN_ARMS
                || abil == ABIL_MAKHLEB_GREATER_SERVANT_OF_MAKHLEB))
           ? div_rand_round(original_cost * 5, 2)
           : original_cost;
}

static void _pay_ability_costs(const ability_def& abil)
{
    if (abil.flags & abflag::INSTANT)
    {
        you.turn_is_over = false;
        you.elapsed_time_at_last_input = you.elapsed_time;
        update_turn_count();
    }
    else
        you.turn_is_over = true;

    const int food_cost  = abil.food_cost + random2avg(abil.food_cost, 2);
    const int piety_cost =
        _scale_piety_cost(abil.ability, abil.piety_cost.cost());
    const int hp_cost    = abil.hp_cost.cost(you.hp_max);

    dprf("Cost: mp=%d; hp=%d; food=%d; piety=%d",
         abil.mp_cost, hp_cost, food_cost, piety_cost);

    if (abil.mp_cost)
    {
        dec_mp(abil.mp_cost);
        if (abil.flags & abflag::PERMANENT_MP)
            rot_mp(1);
    }

    if (abil.hp_cost)
    {
        dec_hp(hp_cost, false);
        if (abil.flags & abflag::PERMANENT_HP)
            rot_hp(hp_cost);
    }

    if (food_cost)
        make_hungry(food_cost, false, true);

    if (piety_cost)
        lose_piety(piety_cost);
}

int choose_ability_menu(const vector<talent>& talents)
{
#ifdef USE_TILE_LOCAL
    const bool text_only = false;
#else
    const bool text_only = true;
#endif

    ToggleableMenu abil_menu(MF_SINGLESELECT | MF_ANYPRINTABLE
                             | MF_TOGGLE_ACTION | MF_ALWAYS_SHOW_MORE,
                             text_only);

    abil_menu.set_highlighter(nullptr);
#ifdef USE_TILE_LOCAL
    {
        // Hack like the one in spl-cast.cc:list_spells() to align the title.
        ToggleableMenuEntry* me =
            new ToggleableMenuEntry("  Ability - do what?                 "
                                    "Cost                          Failure",
                                    "  Ability - describe what?           "
                                    "Cost                          Failure",
                                    MEL_ITEM);
        me->colour = BLUE;
        abil_menu.add_entry(me);
    }
#else
    abil_menu.set_title(
        new ToggleableMenuEntry("  Ability - do what?                 "
                                "Cost                          Failure",
                                "  Ability - describe what?           "
                                "Cost                          Failure",
                                MEL_TITLE));
#endif
    abil_menu.set_tag("ability");
    abil_menu.add_toggle_key('!');
    abil_menu.add_toggle_key('?');
    abil_menu.menu_action = Menu::ACT_EXECUTE;

    if (crawl_state.game_is_hints())
    {
        // XXX: This could be buggy if you manage to pick up lots and
        // lots of abilities during hints mode.
        abil_menu.set_more(hints_abilities_info());
    }
    else
    {
        abil_menu.set_more(formatted_string::parse_string(
                           "Press '<w>!</w>' or '<w>?</w>' to toggle "
                           "between ability selection and description."));
    }

    int numbers[52];
    for (int i = 0; i < 52; ++i)
        numbers[i] = i;

    bool found_invocations = false;

    // First add all non-invocation abilities.
    for (unsigned int i = 0; i < talents.size(); ++i)
    {
        if (talents[i].is_invocation)
            found_invocations = true;
        else
        {
            ToggleableMenuEntry* me =
                new ToggleableMenuEntry(describe_talent(talents[i]),
                                        describe_talent(talents[i]),
                                        MEL_ITEM, 1, talents[i].hotkey);
            me->data = &numbers[i];
#ifdef USE_TILE
            me->add_tile(tile_def(tileidx_ability(talents[i].which), TEX_GUI));
#endif
            // Only check this here, since your god can't hate its own abilities
            if (god_hates_ability(talents[i].which, you.religion))
                me->colour = COL_FORBIDDEN;
            abil_menu.add_entry(me);
        }
    }

    if (found_invocations)
    {
#ifdef USE_TILE_LOCAL
        ToggleableMenuEntry* subtitle =
            new ToggleableMenuEntry("    Invocations - ",
                                    "    Invocations - ", MEL_ITEM);
        subtitle->colour = BLUE;
        abil_menu.add_entry(subtitle);
#else
        abil_menu.add_entry(
            new ToggleableMenuEntry("    Invocations - ",
                                    "    Invocations - ", MEL_SUBTITLE));
#endif
        for (unsigned int i = 0; i < talents.size(); ++i)
        {
            if (talents[i].is_invocation)
            {
                ToggleableMenuEntry* me =
                    new ToggleableMenuEntry(describe_talent(talents[i]),
                                            describe_talent(talents[i]),
                                            MEL_ITEM, 1, talents[i].hotkey);
                me->data = &numbers[i];
#ifdef USE_TILE
                me->add_tile(tile_def(tileidx_ability(talents[i].which),
                                      TEX_GUI));
#endif
                abil_menu.add_entry(me);
            }
        }
    }

    while (true)
    {
        vector<MenuEntry*> sel = abil_menu.show(false);
        if (!crawl_state.doing_prev_cmd_again)
            redraw_screen();
        if (sel.empty())
            return -1;

        ASSERT(sel.size() == 1);
        ASSERT(sel[0]->hotkeys.size() == 1);
        int selected = *(static_cast<int*>(sel[0]->data));

        if (abil_menu.menu_action == Menu::ACT_EXAMINE)
            _print_talent_description(talents[selected]);
        else
            return *(static_cast<int*>(sel[0]->data));
    }
}

string describe_talent(const talent& tal)
{
    ASSERT(tal.which != ABIL_NON_ABILITY);

    const string failure = failure_rate_to_string(tal.fail)
        + (testbits(get_ability_def(tal.which).flags, abflag::HOSTILE)
           ? " hostile" : "");

    ostringstream desc;
    desc << left
         << chop_string(ability_name(tal.which), 32)
         << chop_string(make_cost_description(tal.which), 30)
         << chop_string(failure, 12);
    return desc.str();
}

static void _add_talent(vector<talent>& vec, const ability_type ability,
                        bool check_confused)
{
    const talent t = get_talent(ability, check_confused);
    if (t.which != ABIL_NON_ABILITY)
        vec.push_back(t);
}

/**
 * Return all relevant talents that the player has.
 *
 * Currently the only abilities that are affected by include_unusable are god
 * abilities (affect by e.g. penance or silence).
 * @param check_confused If true, abilities that don't work when confused will
 *                       be excluded.
 * @param include_unusable If true, abilities that are currently unusable will
 *                         be excluded.
 * @return  A vector of talent structs.
 */
vector<talent> your_talents(bool check_confused, bool include_unusable)
{
    vector<talent> talents;

    // Species-based abilities.
    if (player_mutation_level(MUT_MUMMY_RESTORATION))
        _add_talent(talents, ABIL_MUMMY_RESTORATION, check_confused);

    if (you.species == SP_DEEP_DWARF)
        _add_talent(talents, ABIL_RECHARGING, check_confused);

    if (you.species == SP_FORMICID
        && (you.form != TRAN_TREE || include_unusable))
    {
        _add_talent(talents, ABIL_DIG, check_confused);
        if (!crawl_state.game_is_sprint() || brdepth[you.where_are_you] > 1)
            _add_talent(talents, ABIL_SHAFT_SELF, check_confused);
    }

    // Spit Poison, possibly upgraded to Breathe Poison.
    if (player_mutation_level(MUT_SPIT_POISON) == 3)
        _add_talent(talents, ABIL_BREATHE_POISON, check_confused);
    else if (player_mutation_level(MUT_SPIT_POISON))
        _add_talent(talents, ABIL_SPIT_POISON, check_confused);

    if (species_is_draconian(you.species)
        // Draconians don't maintain their original breath weapons
        // if shapechanged into a non-dragon form.
        && (!form_changed_physiology() || you.form == TRAN_DRAGON)
        && draconian_breath(you.species) != ABIL_NON_ABILITY)
    {
        _add_talent(talents, draconian_breath(you.species), check_confused);
    }

    if (you.species == SP_VAMPIRE && you.experience_level >= 3
        && you.hunger_state <= HS_SATIATED
        && you.form != TRAN_BAT)
    {
        _add_talent(talents, ABIL_TRAN_BAT, check_confused);
    }

    if (player_mutation_level(MUT_TENGU_FLIGHT) && !you.airborne()
        || you.racial_permanent_flight() && !you.attribute[ATTR_PERM_FLIGHT]
#if TAG_MAJOR_VERSION == 34
           && you.species != SP_DJINNI
#endif
           )
    {
        // Tengu can fly, but only from the ground
        // (until level 14, when it becomes permanent until revoked).
        // Black draconians and gargoyles get permaflight at XL 14, but they
        // don't get the tengu movement/evasion bonuses and they don't get
        // temporary flight before then.
        // Other dracs can mutate big wings whenever as well.
        _add_talent(talents, ABIL_FLY, check_confused);
    }

    if (you.attribute[ATTR_PERM_FLIGHT] && you.racial_permanent_flight())
        _add_talent(talents, ABIL_STOP_FLYING, check_confused);

    // Mutations
    if (player_mutation_level(MUT_HURL_HELLFIRE))
        _add_talent(talents, ABIL_HELLFIRE, check_confused);

    if (you.duration[DUR_TRANSFORMATION] && !you.transform_uncancellable)
        _add_talent(talents, ABIL_END_TRANSFORMATION, check_confused);

    if (player_mutation_level(MUT_BLINK))
        _add_talent(talents, ABIL_BLINK, check_confused);

    // Religious abilities.
    for (ability_type abil : get_god_abilities(include_unusable, false,
                                               include_unusable))
    {
        _add_talent(talents, abil, check_confused);
    }

    // And finally, the ability to opt-out of your faith {dlb}:
    if (!you_worship(GOD_NO_GOD))
        _add_talent(talents, ABIL_RENOUNCE_RELIGION, check_confused);

    if (env.level_state & LSTATE_BEOGH && can_convert_to_beogh())
        _add_talent(talents, ABIL_CONVERT_TO_BEOGH, check_confused);

    //jmf: Check for breath weapons - they're exclusive of each other, I hope!
    //     Make better ones come first.
    if (you.species != SP_RED_DRACONIAN && you.form == TRAN_DRAGON
         && dragon_form_dragon_type() == MONS_FIRE_DRAGON)
    {
        _add_talent(talents, ABIL_BREATHE_FIRE, check_confused);
    }

    // Checking for unreleased Delayed Fireball.
    if (you.attribute[ ATTR_DELAYED_FIREBALL ])
        _add_talent(talents, ABIL_DELAYED_FIREBALL, check_confused);

    if (you.duration[DUR_SONG_OF_SLAYING])
        _add_talent(talents, ABIL_STOP_SINGING, check_confused);

    // Evocations from items.
    if (you.scan_artefacts(ARTP_BLINK)
        && !player_mutation_level(MUT_NO_ARTIFICE))
    {
        _add_talent(talents, ABIL_EVOKE_BLINK, check_confused);
    }

    if (you.scan_artefacts(ARTP_FOG)
        && !player_mutation_level(MUT_NO_ARTIFICE))
    {
        _add_talent(talents, ABIL_EVOKE_FOG, check_confused);
    }

    if (you.evokable_berserk() && !player_mutation_level(MUT_NO_ARTIFICE))
        _add_talent(talents, ABIL_EVOKE_BERSERK, check_confused);

    if (you.evokable_invis() && !you.attribute[ATTR_INVIS_UNCANCELLABLE]
        && !player_mutation_level(MUT_NO_ARTIFICE))
    {
        // Now you can only turn invisibility off if you have an
        // activatable item. Wands and potions will have to time
        // out. -- bwr
        if (you.duration[DUR_INVIS])
            _add_talent(talents, ABIL_EVOKE_TURN_VISIBLE, check_confused);
        else
            _add_talent(talents, ABIL_EVOKE_TURN_INVISIBLE, check_confused);
    }

    if (you.evokable_flight() && !player_mutation_level(MUT_NO_ARTIFICE))
    {
        // Has no effect on permanently flying Tengu.
        if (!you.permanent_flight() || !you.racial_permanent_flight())
        {
            // You can still evoke perm flight if you have temporary one.
            if (!you.airborne()
                || !you.attribute[ATTR_PERM_FLIGHT]
                   && you.wearing_ego(EQ_ALL_ARMOUR, SPARM_FLYING))
            {
                _add_talent(talents, ABIL_EVOKE_FLIGHT, check_confused);
            }
            // Now you can only turn flight off if you have an
            // activatable item. Potions and spells will have to time
            // out.
            if (you.airborne() && !you.attribute[ATTR_FLIGHT_UNCANCELLABLE])
                _add_talent(talents, ABIL_STOP_FLYING, check_confused);
        }
    }

    // Find hotkeys for the non-hotkeyed talents.
    for (talent &tal : talents)
    {
        const int index = _lookup_ability_slot(tal.which);
        if (index > -1)
        {
            tal.hotkey = index_to_letter(index);
            continue;
        }

        // Try to find a free hotkey for i, starting from Z.
        for (int k = 51; k >= 0; ++k)
        {
            const int kkey = index_to_letter(k);
            bool good_key = true;

            // Check that it doesn't conflict with other hotkeys.
            for (const talent &other : talents)
                if (other.hotkey == kkey)
                {
                    good_key = false;
                    break;
                }

            if (good_key)
            {
                tal.hotkey = k;
                you.ability_letter_table[k] = tal.which;
                break;
            }
        }
        // In theory, we could be left with an unreachable ability
        // here (if you have 53 or more abilities simultaneously).
    }

    return talents;
}

/**
 * Maybe move an ability to the slot given by the ability_slot option.
 *
 * @param[in] slot current slot of the ability
 * @returns the new slot of the ability; may still be slot, if the ability
 *          was not reassigned.
 */
int auto_assign_ability_slot(int slot)
{
    const ability_type abil_type = you.ability_letter_table[slot];
    const string abilname = lowercase_string(ability_name(abil_type));
    bool overwrite = false;
    // check to see whether we've chosen an automatic label:
    for (auto& mapping : Options.auto_ability_letters)
    {
        if (!mapping.first.matches(abilname))
            continue;
        for (char i : mapping.second)
        {
            if (i == '+')
                overwrite = true;
            else if (i == '-')
                overwrite = false;
            else if (isaalpha(i))
            {
                const int index = letter_to_index(i);
                ability_type existing_ability = you.ability_letter_table[index];

                if (existing_ability == ABIL_NON_ABILITY
                    || existing_ability == abil_type)
                {
                    // Unassigned or already assigned to this ability.
                    you.ability_letter_table[index] = abil_type;
                    if (slot != index)
                        you.ability_letter_table[slot] = ABIL_NON_ABILITY;
                    return index;
                }
                else if (overwrite)
                {
                    const string str = lowercase_string(ability_name(existing_ability));
                    // Don't overwrite an ability matched by the same rule.
                    if (mapping.first.matches(str))
                        continue;
                    you.ability_letter_table[slot] = abil_type;
                    swap_ability_slots(slot, index, true);
                    return index;
                }
                // else occupied, continue to the next mapping.
            }
        }
    }
    return slot;
}

// Returns an index (0-51) if already assigned, -1 if not.
static int _lookup_ability_slot(const ability_type abil)
{
    // Placeholder handling, part 2: The ability we have might
    // correspond to a placeholder, in which case the ability letter
    // table will contain that placeholder. Convert the latter to
    // its corresponding ability before comparing the two, so that
    // we'll find the placeholder's index properly.
    for (int slot = 0; slot < 52; slot++)
        if (fixup_ability(you.ability_letter_table[slot]) == abil)
            return slot;
    return -1;
}

// Assign a new ability slot if necessary. Returns an index (0-51) if
// successful, -1 if you should just use the next one.
int find_ability_slot(const ability_type abil, char firstletter)
{
    // If we were already assigned a slot, use it.
    int had_slot = _lookup_ability_slot(abil);
    if (had_slot > -1)
        return had_slot;

    // No requested slot, find new one and make it preferred.

    // firstletter defaults to 'f', because a-e is for invocations
    int first_slot = letter_to_index(firstletter);

    // Reserve the first non-god ability slot (f) for Draconian breath
    if (you.species == SP_BASE_DRACONIAN && first_slot >= letter_to_index('f'))
        first_slot += 1;

    ASSERT(first_slot < 52);

    switch (abil)
    {
    case ABIL_ELYVILON_LIFESAVING:
        first_slot = letter_to_index('p');
        break;
    case ABIL_KIKU_GIFT_NECRONOMICON:
        first_slot = letter_to_index('N');
        break;
    case ABIL_ZIN_CURE_ALL_MUTATIONS:
    case ABIL_TSO_BLESS_WEAPON:
    case ABIL_KIKU_BLESS_WEAPON:
    case ABIL_LUGONU_BLESS_WEAPON:
    case ABIL_PAKELLAS_SUPERCHARGE:
        first_slot = letter_to_index('W');
        break;
    case ABIL_CONVERT_TO_BEOGH:
        first_slot = letter_to_index('Y');
        break;
    case ABIL_RU_SACRIFICE_PURITY:
    case ABIL_RU_SACRIFICE_WORDS:
    case ABIL_RU_SACRIFICE_DRINK:
    case ABIL_RU_SACRIFICE_ESSENCE:
    case ABIL_RU_SACRIFICE_HEALTH:
    case ABIL_RU_SACRIFICE_STEALTH:
    case ABIL_RU_SACRIFICE_ARTIFICE:
    case ABIL_RU_SACRIFICE_LOVE:
    case ABIL_RU_SACRIFICE_COURAGE:
    case ABIL_RU_SACRIFICE_ARCANA:
    case ABIL_RU_SACRIFICE_NIMBLENESS:
    case ABIL_RU_SACRIFICE_DURABILITY:
    case ABIL_RU_SACRIFICE_HAND:
    case ABIL_RU_SACRIFICE_EXPERIENCE:
    case ABIL_RU_SACRIFICE_SKILL:
    case ABIL_RU_SACRIFICE_EYE:
    case ABIL_RU_SACRIFICE_RESISTANCE:
    case ABIL_RU_REJECT_SACRIFICES:
        first_slot = letter_to_index('G');
        break;
    default:
        break;
    }

    for (int slot = first_slot; slot < 52; ++slot)
    {
        if (you.ability_letter_table[slot] == ABIL_NON_ABILITY)
        {
            you.ability_letter_table[slot] = abil;
            return auto_assign_ability_slot(slot);
        }
    }

    // If we can't find anything else, try a-e.
    for (int slot = first_slot - 1; slot >= 0; --slot)
    {
        if (you.ability_letter_table[slot] == ABIL_NON_ABILITY)
        {
            you.ability_letter_table[slot] = abil;
            return auto_assign_ability_slot(slot);
        }
    }

    // All letters are assigned.
    return -1;
}

vector<ability_type> get_god_abilities(bool ignore_silence, bool ignore_piety,
                                       bool ignore_penance)
{
    vector<ability_type> abilities;
    if (you_worship(GOD_RU))
    {
        ASSERT(you.props.exists(AVAILABLE_SAC_KEY));
        bool any_sacrifices = false;
        for (const auto& store : you.props[AVAILABLE_SAC_KEY].get_vector())
        {
            any_sacrifices = true;
            abilities.push_back(static_cast<ability_type>(store.get_int()));
        }
        if (any_sacrifices)
            abilities.push_back(ABIL_RU_REJECT_SACRIFICES);
    }
    if (you.transfer_skill_points > 0)
        abilities.push_back(ABIL_ASHENZARI_END_TRANSFER);
    if (!ignore_silence && silenced(you.pos()))
        return abilities;
    // Remaining abilities are unusable if silenced.

    for (const auto& power : get_god_powers(you.religion))
    {
        // not an activated power
        if (power.abil == ABIL_NON_ABILITY)
            continue;
        const ability_type abil = fixup_ability(power.abil);
        ASSERT(abil != ABIL_NON_ABILITY);
        if ((power.rank <= 0
             || power.rank == 7 && can_do_capstone_ability(you.religion)
             || piety_rank() >= power.rank
             || ignore_piety)
            && (!player_under_penance()
                || power.rank == -1
                || ignore_penance))
        {
            abilities.push_back(abil);
        }
    }

    return abilities;
}

void swap_ability_slots(int index1, int index2, bool silent)
{
    // Swap references in the letter table.
    ability_type tmp = you.ability_letter_table[index2];
    you.ability_letter_table[index2] = you.ability_letter_table[index1];
    you.ability_letter_table[index1] = tmp;

    if (!silent)
    {
        mprf_nocap("%c - %s", index_to_letter(index2),
                   ability_name(you.ability_letter_table[index2]));
    }

}


////////////////////////////////////////////////////////////////////////
// generic_cost

int generic_cost::cost() const
{
    return base + (add > 0 ? random2avg(add, rolls) : 0);
}

int scaling_cost::cost(int max) const
{
    return (value < 0) ? (-value) : ((value * max + 500) / 1000);
}
