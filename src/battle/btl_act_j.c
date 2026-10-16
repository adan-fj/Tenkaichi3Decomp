#include "common.h"
#include "battle/btl_act_j.h"

/*
 * End of the fighter action decision code: 0x203168..0x204E78.
 *
 * Requests that are not plain attacks or movement: the powered-up combo finisher, transformation, fusion,
 * member switch, techniques (blast 2 / ultimate, slots 2..4) and skills (blast 1, slots 0..1); then the table
 * that turns a pending hit reaction into a damage action, the story mode (mode 1) scripted actions, and small
 * selectors the handlers call to pick one of several action ids.
 *
 * The decision functions only fill the follow-up queue with BtlAct_SetQueue and return the number of slots
 * written; their callers (BtlDecide_Main, BtlAct_CheckForced, handlers) then request BtlAct_GetQueued(chr).
 */

extern s32 Battle_GetMode(void);
extern s32 BattleSide_IsCharaUsable(s32 side, s32 chara);
extern void BtlEvent_Raise(s32 player, s32 event);
extern s32 BtlStage_HasMoon(void); /* the stage's flag word has bit 0x10 */

extern f32 BtlUtil_WrapAngle(f32 a);
extern BtlActJPose *BtlChar_GetPos(BtlActJChr *chr);
extern s32 BtlChar_IsFree(BtlActJChr *chr);
extern s32 BtlChar_IsDead(BtlActJChr *chr);
extern s32 BtlChar_TestMemberUnk70(BtlActJChr *chr);
extern s32 BtlChar_TestFlag(BtlActJChr *chr, s32 bit);
extern void BtlChar_SetFlag(BtlActJChr *chr, s32 bit);
extern void BtlChar_ClearFlag(BtlActJChr *chr, s32 bit);
extern s32 BtlInput_IsHeld(BtlActJChr *chr, u32 mask);
extern s32 BtlInput_IsPressed(BtlActJChr *chr, u32 mask);
extern s32 BtlInput_TestAction(BtlActJChr *chr, s32 id, s32 want);
extern s32 BtlAnim_GetId(BtlActJChr *chr);
extern s32 BtlAnim_GetFlags(s32 anim);
extern s32 BtlOpp_GetPlayer(BtlActJChr *chr);
extern s32 BtlOpp_GetUnkD88(BtlActJChr *chr);

extern BtlActJMember *BtlMember_Get(BtlActJChr *chr, s32 member);
extern BtlActJMember *BtlMember_GetActive(BtlActJChr *chr);
extern s32 BtlMember_FindPresentIndex(BtlActJChr *chr, s32 chara);
extern BtlActJGauge *BtlMember_GetGauge(BtlActJChr *chr, s32 member);
extern BtlActJGauge *BtlMember_GetActiveGauge(BtlActJChr *chr);
extern s32 BtlMember_CountAlive(BtlActJChr *chr);
extern s32 BtlMember_HasKi(BtlActJChr *chr, s32 amount);
extern s32 BtlMember_HasBlast(BtlActJChr *chr, s32 amount);
extern s32 BtlMember_HasAbility(BtlActJChr *chr, s32 ability);

extern void BtlAct_SetQueue(BtlActJChr *chr, u32 slot, s32 id);
extern s32 BtlAct_GetCurrent(BtlActJChr *chr);
extern s32 BtlAct_GetQueued(BtlActJChr *chr);
extern s32 BtlAct_IsTechniqueId(s32 id);
extern f32 BtlAct_GetHeightRatio(BtlActJChr *chr);
extern void BtlAct_PrepareSwitch(BtlActJChr *chr);
extern s32 BtlAct_IsAttackId(s32 id);
extern void BtlAct_ResetAttack(BtlActJChr *chr);

/* Earlier parts of the decision code (not decompiled here). */
extern s32 BtlDecide_Common(BtlActJChr *chr, s32 mask);  /* status overrides: stunned 0xD3, flag 0xBE 0xEA, landing 0x11, dead 0xD2 */
extern s32 BtlDecide_Main(BtlActJChr *chr, u32 mask);  /* the input -> action decision for a free fighter */
extern s32 BtlDecide_QueueAttack(BtlActJChr *chr, s32 attack); /* builds the queue for an attack id */

/* Character parameter block (object + 0x91C) readers. */
extern s32 BtlParam_GetUnkAC(BtlActJChr *chr);                       /* s8 + 0xAC: default transformation index */
extern s32 BtlParam_GetSlotId(BtlActJChr *chr, s32 index);            /* u8 + 0x98[index]: transformation target character, 0xFF = none */
extern s32 BtlParam_GetSlotCost(BtlActJChr *chr, s32 index);            /* u8 + 0x9C[index] * 100000: transformation blast cost */
extern s32 BtlParam_GetSlotA0(BtlActJChr *chr, s32 index);            /* u8 + 0xA0[index]: transformation sequence 0..4 */
extern s32 BtlParam_GetSlotA4(BtlActJChr *chr, s32 index);            /* u8 + 0xA4[index]: transformation kind (3 needs the stage flag) */
extern s32 BtlParam_GetSlotA8(BtlActJChr *chr, s32 index);            /* u8 + 0xA8[index] */
extern s32 BtlParam_CountSlots(BtlActJChr *chr);                       /* number of transformation targets that are not 0xFF */
extern s32 BtlParam_TestUnkADBit(BtlActJChr *chr, s32 index);            /* u8 + 0xAD & (0x10 << index) */
extern s32 BtlParam_GetUnkB4(BtlActJChr *chr, s32 index);            /* u8 + 0xB4[index]: fusion result character, 0xFF = none */
extern s32 BtlParam_GetUnkB7(BtlActJChr *chr, s32 index);            /* u8 + 0xB7[index] */
extern s32 BtlParam_GetUnkBA(BtlActJChr *chr, s32 index, s32 n);     /* u8 + 0xBA[index][n]: fusion partner characters */
extern s32 BtlParam_GetCostAE(BtlActJChr *chr, s32 index);            /* u8 + 0xAE[index] * 100000: fusion blast cost */
extern s32 BtlParam_GetUnkB1(BtlActJChr *chr, s32 index);            /* u8 + 0xB1[index]: fusion sequence 1 / 2 */
extern s32 BtlParam_GetUnk8F(BtlActJChr *chr, s32 index);            /* u8 + 0x8F[index]: combo finisher mode 0..5 */
extern s32 BtlParam_CanFly(BtlActJChr *chr);                       /* parameter flag 0x1000 clear, or ability 0x36 */

/* Technique table (object + 0x92C) and skill table (object + 0x930) readers. */
extern s32 BtlSuper_GetFlags(BtlActJChr *chr, s32 slot);             /* technique flags */
extern s32 BtlSuper_GetType(BtlActJChr *chr, s32 slot);             /* technique kind (s8 + 0x13C[slot]) */
extern s32 BtlSuper_GetKiCost(BtlActJChr *chr, s32 slot);             /* technique ki cost (halved with ability 0x2B) */
extern s32 BtlSkill_GetFlags(BtlActJChr *chr, s32 slot);             /* skill flags */
extern s32 BtlSkill_GetId(BtlActJChr *chr, s32 slot);             /* skill id (s16) */
extern s32 BtlSkill_GetSequence(BtlActJChr *chr, s32 slot);             /* skill sequence 0..2 */
extern s32 BtlSkill_GetBlastCost(BtlActJChr *chr, s32 slot);             /* skill blast cost (one stock less with ability 0x15, minimum 1) */

/*
 * Powered-up mode (flag 6) only: input 80 (blast pressed) during a rush combo queues a technique as its finisher.
 * The last prepared attack id picks a column of the character's finisher table, whose value says which technique
 * (slot 2, slot 3, or the plain rush finisher 0x115) and whether a dash (0x2B, 0x2E) goes first.
 */
s32 BtlAct_QueueComboFinish(BtlActJChr *chr) {
    s32 n = 0;
    s32 col;
    s32 tech2;
    s32 tech3;

    if (!BtlChar_TestFlag(chr, 6)) {
        return 0;
    }
    if (!BtlInput_TestAction(chr, 0x50, 1)) {
        return 0;
    }
    switch (chr->attackId) {
        case 0xA6:
            col = 3;
            break;
        case 0xA7:
        case 0xA8:
            col = 6;
            break;
        case 0xA9:
            col = 5;
            break;
        case 0xAA:
            col = 4;
            break;
        case 0xAB:
            col = 7;
            break;
        case 0xAC:
            col = 8;
            break;
        case 0xAD:
            col = 2;
            break;
        case 0x5A:
        case 0x5B:
        case 0x5C:
        case 0x5D:
            col = 0;
            break;
        default:
            col = 1;
            break;
    }
    switch (BtlSuper_GetType(chr, 2)) {
        case 1:
            tech2 = 0x118;
            break;
        case 8:
            tech2 = 0x127;
            break;
        default:
            tech2 = 0x115;
            break;
    }
    switch (BtlSuper_GetType(chr, 3)) {
        case 1:
            tech3 = 0x119;
            break;
        case 8:
            tech3 = 0x128;
            break;
        default:
            tech3 = 0x116;
            break;
    }
    switch (BtlParam_GetUnk8F(chr, col)) {
        case 0:
            BtlAct_SetQueue(chr, n++, tech2);
            break;
        case 1:
            BtlAct_SetQueue(chr, n++, tech3);
            break;
        case 2:
            BtlAct_SetQueue(chr, n++, 0x115);
            break;
        case 3:
            BtlAct_SetQueue(chr, n++, 0x2B);
            BtlAct_SetQueue(chr, n++, 0x2E);
            BtlAct_SetQueue(chr, n++, tech2);
            break;
        case 4:
            BtlAct_SetQueue(chr, n++, 0x2B);
            BtlAct_SetQueue(chr, n++, 0x2E);
            BtlAct_SetQueue(chr, n++, tech3);
            break;
        case 5:
            BtlAct_SetQueue(chr, n++, 0x2B);
            BtlAct_SetQueue(chr, n++, 0x2E);
            BtlAct_SetQueue(chr, n++, 0x115);
            break;
    }
    BtlAct_ResetAttack(chr);
    return n;
}

/*
 * Can the fighter take transformation `index` (0..3)? Refused with ability 0x5E, flag 0xAF, the member's unk70
 * state, no target, a target the side may not use, kind 3 on a stage without flag 0x10, or (needBlast) not enough
 * blast gauge; needAllowed also requires form.allowed.
 */
s32 BtlAct_CanTransform(BtlActJChr *chr, u32 index, s32 needBlast, s32 needAllowed) {
    s32 chara;
    s32 cost;
    s32 kind;

    if (index >= 4) {
        return 0;
    }
    if (needAllowed && chr->form.allowed == 0) {
        return 0;
    }
    if (BtlMember_HasAbility(chr, 0x5E)) {
        return 0;
    }
    if (BtlChar_TestFlag(chr, 0xAF)) {
        return 0;
    }
    if (BtlChar_TestMemberUnk70(chr)) {
        return 0;
    }
    chara = BtlParam_GetSlotId(chr, index);
    cost = BtlParam_GetSlotCost(chr, index);
    kind = BtlParam_GetSlotA4(chr, index);
    if (chara == 0xFF) {
        return 0;
    }
    if (!BattleSide_IsCharaUsable(chr->player, chara)) {
        return 0;
    }
    if (kind == 3 && !BtlStage_HasMoon()) {
        return 0;
    }
    if (needBlast && !BtlMember_HasBlast(chr, cost)) {
        return 0;
    }
#ifdef PORT
    {
        extern s32 BtlMember_TransformAllowed(BtlActJChr *chr, s32 target);

        if (!BtlMember_TransformAllowed(chr, chara)) {  /* Mod "Ultra Instinct" v56: not back up past a form left */
            return 0;
        }
    }
#endif
    return 1;
}

#ifdef PORT
/* Mod "Ultra Instinct" v56: god forms (god_forms.csv): L2 + R3 + left / up / right / down takes Super Saiyan God, God Super
   Saiyan, Rose or Super Saiyan 4 Limit Breaker, with the game's own transformation (sequence 0) into the model the
   table gives (the base form's, the Super Saiyan form's or the Super Saiyan 4 form's), recoloured when it is loaded
   (btl_char_mgr.c). The cost is god_<form>_cost blast stocks. With L2
   held, R3 takes nothing else. */
extern s32 Port_ModGodModel(s32 chara, s32 form);
extern s32 Port_ModGodCost(s32 form);
extern s32 BtlAct_GodActive(BtlActJChr *chr);
extern void BtlAct_GodSetPending(BtlActJChr *chr, s32 god);
extern s32 BtlMember_GodAllowed(BtlActJChr *chr, s32 model, s32 god);

/* Out of stamina (BtlMember_StaminaTick): the transformation back to the line's base form (the model loaded again
   ends Kaioken, Ultra Instinct, Super Speed and the god form, BtlAct_EndFlag98), always with its animation. */
extern s32 Port_ModBaseOf(s32 chara);
extern void BtlAct_Request(BtlActJChr *chr, s32 id);

void BtlAct_StaminaOut(BtlActJChr *chr) {
    s32 chara = BtlMember_GetActive(chr)->chara;
    s32 base = Port_ModBaseOf(chara);

    if (!BattleSide_IsCharaUsable(chr->player, base)) {
        base = chara;
    }
    chr->form.index = 0;
    chr->form.chara = base;
    chr->form.costume = BtlMember_GetActive(chr)->costume;
    chr->form.cost = 0;
    chr->form.partner = -1;
    chr->form.kind = 1;
    chr->form.unk24 = 0xFF;
    chr->form.unk28 = 0;
    chr->form.variant = BtlMember_GetActiveGauge(chr)->variant;
    chr->form.animChara = base;
    chr->form.unk1C = base;
    chr->form.voiceChara = base;
    BtlAct_GodSetPending(chr, 0);
    BtlAct_Request(chr, 0xEC);
}

extern s32 Port_ModGodTransformKind(s32 form);

static s32 BtlAct_QueueGodForm(BtlActJChr *chr, s32 god) {
    s32 model = Port_ModGodModel(BtlMember_GetActive(chr)->chara, god);
    s32 cost;

    if (model < 0 || BtlAct_GodActive(chr) == god) {
        return 0;
    }
    if (!BtlChar_TestFlag(chr, 0x10F) && chr->form.allowed == 0) {
        return 0;
    }
    if (BtlMember_HasAbility(chr, 0x5E) || BtlChar_TestFlag(chr, 0xAF) || BtlChar_TestMemberUnk70(chr)) {
        return 0;
    }
    if (!BattleSide_IsCharaUsable(chr->player, model) || !BtlMember_GodAllowed(chr, model, god)) {
        return 0;
    }
    cost = Port_ModGodCost(god) * 100000;
    if (!BtlMember_HasBlast(chr, cost)) {
        extern void HudCombo_ShowModeText(s32 side, s32 kind, s32 value);

        HudCombo_ShowModeText(chr->player, 9, 0);  /* "Not enough stamina" */
        return 0;
    }
    chr->form.index = 0;
    chr->form.chara = model;
    chr->form.costume = BtlMember_GetActive(chr)->costume;
    chr->form.cost = cost;
    chr->form.partner = -1;
    chr->form.kind = Port_ModGodTransformKind(god);  /* its transformation effect (god_<form>_transform_kind) */
    chr->form.unk24 = 0xFF;
    chr->form.unk28 = 0;
    chr->form.variant = BtlMember_GetActiveGauge(chr)->variant;
    chr->form.animChara = model;
    chr->form.unk1C = model;
    chr->form.voiceChara = model;
    BtlAct_GodSetPending(chr, god);
    BtlAct_SetQueue(chr, 0, 0xEC);
    return 1;
}
#endif

#ifdef PORT
/* Mod "Ultra Instinct" v56: the CPU and the mod's skills (cpu_* in mod_values.txt; cpu_skills = 0 turns it all off).
   BtlAct_CpuThink runs every frame for each fighter the CPU plays (btl_ai_mgr.c: after the game's own AI has filled
   the pad, before the fighter reads it). It picks what is worth using now and leaves it as a request that the
   fighter's own input checks take (BtlAct_CheckSkillInput, BtlAct_CheckTransformInput), so it starts only where and
   when a player's input would:
     - forms: the strongest form it can take, ordinary or god, keeping cpu_stamina_reserve stamina, that lasts
       long enough on its stamina to finish the opponent (BtlAct_CpuBestForm: a hungry form like God Super Saiyan only
       when it can finish the job quickly, else a lighter one like Super Saiyan God). While a
       transformation up refills both fighters' health (transform_up_heals), it does not transform while it is more
       than cpu_heal_margin health percent ahead (the refill would help the opponent more), unless the opponent has
       cpu_heal_ignore_above percent or more or is cpu_outpowered times stronger. The game's own AI no longer picks
       its forms (it could step down from a god form).
     - Ultra Instinct and Super Speed: switched on once it has their stamina and some to spare (Ultra Instinct keeps
       cpu_ultra_instinct_reserve for its dodges).
     - Kaioken: one level at a time while it is less than cpu_kaioken_power_target times as strong as the opponent,
       as long as the health it costs leaves cpu_kaioken_keep_health of the maximum.
   Below CPU level cpu_transform_min_level (1: at level 0) it takes no form at all. Forms and Kaioken wait for a safe
   moment: the opponent further than cpu_safe_distance close ranges and not in a
   technique, or stunned. How often it takes its chance goes with its CPU level (cpu_skill_chance_low .. _high,
   rolled every cpu_think_frames), with cpu_skill_cooldown frames between two skills. It also changes the pad:
     - low on stamina (cpu_rest_below) with the opponent away (cpu_rest_distance close ranges), it stands and
       charges ki until cpu_rest_until (stamina comes back only when still). With full ki it only stands: charging
       on would fill the Max Power gauge and spend a blast stock, its stamina. Hit out of it, it waits
       cpu_rest_retry frames before it rests again;
     - in the homing dash, near the opponent (cpu_dash_strike_distance), it may strike: L2 + triangle with a
       direction (cpu_dash_strike_chance_*), holding triangle up to cpu_dash_strike_hold frames to charge it;
     - with a beam ready and the opponent far (cpu_beam_charge_distance) or stunned, it may hold triangle to charge
       it (cpu_beam_charge_chance_*), and lets it go once the opponent comes closer. */
extern f32 Port_ModValue(const char *key, f32 def);
extern f32 Port_ModStaminaDrain(s32 chara, s32 god);
extern u32 Rand_Range(u32 n);
extern s32 BtlCharApi_GetHp(s32 objId);
extern s32 BtlCharApi_GetHpMax(s32 objId);
extern s32 BtlCharApi_GetStunTimer(s32 objId);
extern s32 BtlCharApi_IsInTechnique(s32 objId);
extern s32 BtlCharApi_IsInputInjected(s32 objId);
extern f32 BtlCharApi_GetCloseRange(s32 objId);
extern BtlActJChr *BtlChar_Get(s32 side);
extern s32 BtlMember_StaminaPercent(BtlActJChr *chr);
extern s32 BtlMember_StaminaUnits(BtlActJChr *chr, s32 points);
extern s32 BtlMember_HasStamina(BtlActJChr *chr, s32 points);
extern f32 BtlMember_PowerLevel(BtlActJChr *chr);
extern f32 BtlMember_FormLevel(BtlActJChr *chr);
extern f32 BtlMember_FormPowerOf(s32 chara, s32 god);
extern s32 BtlMember_TransformHeals(s32 side);
extern s32 BtlMember_UltraInstinctStamina(BtlActJChr *chr, s32 activate);
extern s32 BtlAct_KaiokenNextHealth(BtlActJChr *chr);
extern s32 BtlAct_ModeOn(BtlActJChr *chr, s32 bit);
extern s32 BtlAct_HasModeAbility(BtlActJChr *chr, s32 which);
extern s32 BtlMember_IsKiFull(BtlActJChr *chr);
extern f32 powf(f32 x, f32 y);

#define BTL_CPU_KAIOKEN 1
#define BTL_CPU_UI 2
#define BTL_CPU_SPEED 3
#define BTL_CPU_GOD 4  /* + 0..3: Super Saiyan God, God Super Saiyan, Rose, Super Saiyan 4 Limit Breaker */
#define BTL_CPU_FORM 8 /* + the transformation slot 0..3 */

typedef struct BtlCpuSide {
    s32 want;       /* the request for the input checks (BTL_CPU_*), 0 none */
    s32 willing;    /* rolled every cpu_think_frames: whether it takes its chance now */
    s32 timer;      /* frames to the next roll */
    s32 cooldown;   /* frames before the next skill */
    s32 resting;    /* standing and charging ki to get stamina back */
    s32 restWait;   /* frames before it may rest again (it was hit out of it) */
    s32 dash;       /* the homing dash: 0 not decided, -1 no strike, 1 a strike, 2 triangle released (press next) */
    s32 strikeHold; /* frames triangle stays held after a dash strike */
    s32 beam;       /* a beam ready to fire: 0 not decided, 1 charge it, -1 not */
} BtlCpuSide;

static BtlCpuSide sBtlCpu[2];

/* A battle (re)starts the AI (btl_ai_mgr.c). */
void BtlAct_CpuReset(void) {
    BtlCpuSide zero = { 0 };

    sBtlCpu[0] = zero;
    sBtlCpu[1] = zero;
}

/* A CPU value between its _low (CPU level 0) and _high (the highest level, 29) settings. */
static f32 BtlAct_CpuByLevel(s32 level, const char *lo, f32 dlo, const char *hi, f32 dhi) {
    f32 a = Port_ModValue(lo, dlo);
    f32 t = level <= 0 ? 0.0f : level >= 29 ? 1.0f : (f32)level / 29.0f;

    return a + (Port_ModValue(hi, dhi) - a) * t;
}

/* A roll against a percent chance that goes with the CPU level. */
static s32 BtlAct_CpuChance(s32 level, const char *lo, f32 dlo, const char *hi, f32 dhi) {
    return (f32)Rand_Range(100) < BtlAct_CpuByLevel(level, lo, dlo, hi, dhi);
}

/* The model god form `god` shows for the fighter when it may take it now (what BtlAct_QueueGodForm checks, but the
   cost), else -1. */
static s32 BtlAct_CpuGodModel(BtlActJChr *chr, s32 god) {
    s32 model = Port_ModGodModel(BtlMember_GetActive(chr)->chara, god);

    if (model < 0 || BtlAct_GodActive(chr) == god) {
        return -1;
    }
    if (!BtlChar_TestFlag(chr, 0x10F) && chr->form.allowed == 0) {
        return -1;
    }
    if (BtlMember_HasAbility(chr, 0x5E) || BtlChar_TestFlag(chr, 0xAF) || BtlChar_TestMemberUnk70(chr)) {
        return -1;
    }
    if (!BattleSide_IsCharaUsable(chr->player, model) || !BtlMember_GodAllowed(chr, model, god)) {
        return -1;
    }
    return model;
}

/* Choosing a form (BtlAct_CpuBestForm): what the fighter knows of the fight. */
typedef struct BtlCpuForms {
    f32 now;      /* power level of the form it is in */
    f32 oppPower; /* the opponent's (Kaioken included) */
    f32 oppHp;    /* the opponent's health after the transformation, 0..1 (1 when it refills both) */
    f32 stamina;  /* its stamina, 0..100 */
    f32 full;     /* blast gauge units of 100 stamina */
    s32 reserve;  /* blast gauge units of cpu_stamina_reserve */
    f32 bestPower; /* the strongest form that lasts until the opponent is finished */
    f32 bestDrain;
    s32 best;
    f32 fallbackDamage; /* else: the most damage done before the stamina runs out */
    s32 fallback;
} BtlCpuForms;

/* Weighs one form: `pick` (BTL_CPU_*), power level `pl`, `drain` stamina a second, `cost` blast gauge units. It
   lasts (stamina left after paying) / drain seconds; finishing the opponent takes its health times cpu_ko_seconds
   (a whole health bar at equal power) divided by the damage multiplier its power gives (power_exponent, within
   damage_min / max_multiplier), times cpu_finish_margin. */
static void BtlAct_CpuWeighForm(BtlCpuForms *f, s32 pick, f32 pl, f32 drain, s32 cost) {
    f32 ko = Port_ModValue("cpu_ko_seconds", 90.0f);
    f32 left = f->stamina - (f32)cost * 100.0f / f->full;
    f32 dm = f->oppPower > 0.0f ? powf(pl / f->oppPower, Port_ModValue("power_exponent", 0.25f)) : 1.0f;
    f32 last;
    f32 need;

    if (pl <= f->now) {
        return;
    }
    if (dm < Port_ModValue("damage_min_multiplier", 0.1f)) {
        dm = Port_ModValue("damage_min_multiplier", 0.1f);
    }
    if (dm > Port_ModValue("damage_max_multiplier", 10.0f)) {
        dm = Port_ModValue("damage_max_multiplier", 10.0f);
    }
    last = drain > 0.0f ? left / drain : 1e30f;
    need = f->oppHp * ko / dm * Port_ModValue("cpu_finish_margin", 1.25f);
    if (ko <= 0.0f || last >= need) {
        if (pl > f->bestPower || (pl == f->bestPower && drain < f->bestDrain)) {
            f->bestPower = pl;
            f->bestDrain = drain;
            f->best = pick;
        }
    } else if (last >= Port_ModValue("cpu_form_min_seconds", 20.0f) && dm * last > f->fallbackDamage) {
        f->fallbackDamage = dm * last;
        f->fallback = pick;
    }
}

/* The form the fighter takes now, stronger than the one it is in and paid with cpu_stamina_reserve stamina to spare:
   the strongest that lasts on its stamina until the opponent is finished (of two as strong, the one that drains
   less); if none does, the one that does the most damage before its stamina runs out, when that lasts at least
   cpu_form_min_seconds. BTL_CPU_GOD + n or BTL_CPU_FORM + slot; 0 none. */
static s32 BtlAct_CpuBestForm(BtlActJChr *chr, s32 side) {
    BtlCpuForms f;
    s32 oppMax = BtlCharApi_GetHpMax(side ^ 1);
    s32 i;

    f.now = BtlMember_FormLevel(chr);
    f.oppPower = BtlMember_PowerLevel(BtlChar_Get(side ^ 1));
    f.oppHp = BtlMember_TransformHeals(side) || oppMax <= 0 ? 1.0f : (f32)BtlCharApi_GetHp(side ^ 1) / (f32)oppMax;
    f.stamina = (f32)BtlMember_StaminaPercent(chr);
    f.full = (f32)BtlMember_StaminaUnits(chr, 100);
    f.reserve = BtlMember_StaminaUnits(chr, (s32)Port_ModValue("cpu_stamina_reserve", 20.0f));
    f.bestPower = 0.0f;
    f.bestDrain = 0.0f;
    f.best = 0;
    f.fallbackDamage = 0.0f;
    f.fallback = 0;
    if (f.full <= 0.0f) {
        return 0;
    }
    for (i = 0; i < 4; i++) {
        s32 cost = BtlParam_GetSlotCost(chr, i);
        s32 chara = BtlParam_GetSlotId(chr, i);

        if (BtlAct_CanTransform(chr, i, 1, !BtlChar_TestFlag(chr, 0x10F)) && BtlMember_HasBlast(chr, cost + f.reserve)) {
            BtlAct_CpuWeighForm(&f, BTL_CPU_FORM + i, BtlMember_FormPowerOf(chara, 0), Port_ModStaminaDrain(chara, 0),
                                cost);
        }
    }
    for (i = 1; i <= 4; i++) {
        s32 model = BtlAct_CpuGodModel(chr, i);
        s32 cost = Port_ModGodCost(i) * 100000;

        if (model >= 0 && BtlMember_HasBlast(chr, cost + f.reserve)) {
            BtlAct_CpuWeighForm(&f, BTL_CPU_GOD + i - 1, BtlMember_FormPowerOf(model, i), Port_ModStaminaDrain(model, i),
                                cost);
        }
    }
    return f.best != 0 ? f.best : f.fallback;
}

/* Whether transforming up pays off now: always, unless it refills both fighters' health while the CPU is ahead on
   health (cpu_heal_margin), with an opponent that has health to gain (cpu_heal_ignore_above) and is not far stronger
   (cpu_outpowered). */
static s32 BtlAct_CpuFormPaysOff(BtlActJChr *chr, s32 side) {
    s32 max = BtlCharApi_GetHpMax(side);
    s32 oppMax = BtlCharApi_GetHpMax(side ^ 1);
    f32 own;
    f32 opp;

    if (!BtlMember_TransformHeals(side) || max <= 0 || oppMax <= 0) {
        return 1;
    }
    own = 100.0f * (f32)BtlCharApi_GetHp(side) / (f32)max;
    opp = 100.0f * (f32)BtlCharApi_GetHp(side ^ 1) / (f32)oppMax;
    if (own - opp <= Port_ModValue("cpu_heal_margin", 15.0f)) {
        return 1;  /* not ahead on health: the refill helps it at least as much */
    }
    if (opp >= Port_ModValue("cpu_heal_ignore_above", 90.0f)) {
        return 1;  /* the opponent has little to gain */
    }
    return BtlMember_PowerLevel(BtlChar_Get(side ^ 1)) > BtlMember_PowerLevel(chr) * Port_ModValue("cpu_outpowered", 4.0f);
}

/* Whether one more Kaioken level pays off: the opponent still stronger than cpu_kaioken_power_target times its own
   power, the health it costs leaving cpu_kaioken_keep_health of the maximum, the stamina with cpu_stamina_reserve to
   spare. */
static s32 BtlAct_CpuKaiokenPaysOff(BtlActJChr *chr, s32 side) {
    s32 cost = BtlAct_KaiokenNextHealth(chr);
    s32 hp = BtlCharApi_GetHp(side);
    s32 max = BtlCharApi_GetHpMax(side);

    if (cost < 0 || max <= 0 || (f32)(hp - cost) < (f32)max * Port_ModValue("cpu_kaioken_keep_health", 0.5f)) {
        return 0;
    }
    if (!BtlMember_HasStamina(chr, (s32)(Port_ModValue("kaioken_stamina", 5.0f) +
                                         Port_ModValue("cpu_stamina_reserve", 20.0f)))) {
        return 0;
    }
    return BtlMember_PowerLevel(chr) <
           BtlMember_PowerLevel(BtlChar_Get(side ^ 1)) * Port_ModValue("cpu_kaioken_power_target", 2.0f);
}

/* One of the dash strikes that are on (dash_strike_*: 0x5E..0x66), at random: its direction bit (0 none); -1 when
   none is on. */
static s32 BtlAct_CpuStrikeDir(void) {
    static const char *const key[5] = { "dash_strike_neutral", "dash_strike_up", "dash_strike_down", "dash_strike_left",
                                        "dash_strike_right" };
    static const f32 def[5] = { 96.0f, 94.0f, 95.0f, 100.0f, 102.0f };
    static const s32 bit[5] = { 0, 0x10, 0x20, 0x40, 0x80 };
    s32 list[5];
    s32 n = 0;
    s32 i;

    for (i = 0; i < 5; i++) {
        s32 v = (s32)Port_ModValue(key[i], def[i]);

        if (v >= 0x5E && v <= 0x66) {
            list[n++] = bit[i];
        }
    }
    return n > 0 ? list[Rand_Range((u32)n)] : -1;
}

/* Every frame for the fighter of side `side` the CPU plays (CPU level `level`, `dist` from the opponent), with the
   pad the game's own AI has filled (battle button bits, stick). */
void BtlAct_CpuThink(s32 side, s32 level, f32 dist, u32 *buttons, f32 *x, f32 *y) {
    BtlCpuSide *c = &sBtlCpu[side & 1];
    BtlActJChr *chr = BtlChar_Get(side);
    BtlActJChr *opp = BtlChar_Get(side ^ 1);
    s32 reserve;
    s32 stamina;
    s32 oppDown;
    s32 oppBusy;
    s32 safe;
    f32 range;

    c->want = 0;
    if (chr == NULL || opp == NULL || Port_ModValue("cpu_skills", 1.0f) == 0.0f) {
        return;
    }
    range = BtlCharApi_GetCloseRange(side);
    reserve = (s32)Port_ModValue("cpu_stamina_reserve", 20.0f);
    stamina = BtlMember_StaminaPercent(chr);
    oppDown = BtlCharApi_GetStunTimer(side ^ 1) > 0;
    oppBusy = BtlCharApi_IsInTechnique(side ^ 1);
    safe = oppDown || (!oppBusy && dist > range * Port_ModValue("cpu_safe_distance", 3.0f));
    if (--c->timer <= 0) {
        c->timer = (s32)Port_ModValue("cpu_think_frames", 20.0f);
        c->willing = BtlAct_CpuChance(level, "cpu_skill_chance_low", 30.0f, "cpu_skill_chance_high", 100.0f);
    }
    if (c->cooldown > 0) {
        c->cooldown--;
    }

    /* a form or a skill, for the input checks */
    if (c->willing && c->cooldown == 0 && BtlChar_IsFree(chr)) {
        s32 form = level >= (s32)Port_ModValue("cpu_transform_min_level", 1.0f) ? BtlAct_CpuBestForm(chr, side) : 0;

        if (form != 0 && BtlAct_CpuFormPaysOff(chr, side)) {
            c->want = safe ? form : 0;  /* it waits for the moment rather than spend the stamina on something else */
        } else if (BtlAct_HasModeAbility(chr, 1) && !BtlAct_ModeOn(chr, 0x200) &&
                   BtlMember_HasStamina(chr, BtlMember_UltraInstinctStamina(chr, 1) +
                                             (s32)Port_ModValue("cpu_ultra_instinct_reserve", 30.0f))) {
            c->want = BTL_CPU_UI;
        } else if (BtlAct_HasModeAbility(chr, 2) && !BtlAct_ModeOn(chr, 0x400) &&
                   BtlMember_HasStamina(chr, (s32)Port_ModValue("super_speed_activate_stamina", 10.0f) + reserve)) {
            c->want = BTL_CPU_SPEED;
        } else if (safe && BtlAct_HasModeAbility(chr, 0) && BtlAct_CpuKaiokenPaysOff(chr, side)) {
            c->want = BTL_CPU_KAIOKEN;
        }
    }

    /* resting: stamina comes back only while still (stamina_recover_still) */
    if (c->restWait > 0) {
        c->restWait--;
    }
    if (c->resting) {
        if (BtlCharApi_GetStunTimer(side) > 0) {
            c->resting = 0;
            c->restWait = (s32)Port_ModValue("cpu_rest_retry", 90.0f);  /* hit out of it */
        } else if (stamina >= (s32)Port_ModValue("cpu_rest_until", 60.0f) || oppBusy ||
                   dist < range * Port_ModValue("cpu_rest_distance", 6.0f) * 0.75f) {
            c->resting = 0;
        }
    } else if (c->restWait == 0 && Port_ModValue("stamina_recover_still", 1.0f) != 0.0f &&
               stamina < (s32)Port_ModValue("cpu_rest_below", 25.0f) && !oppBusy && BtlChar_IsFree(chr) &&
               dist > range * Port_ModValue("cpu_rest_distance", 6.0f)) {
        c->resting = 1;
    }
    if (c->resting) {
        *buttons = BtlMember_IsKiFull(chr) ? 0 : 0x200;  /* L2 alone: it stands and charges ki; full: only stands */
        *x = 0.0f;
        *y = 0.0f;
    }

    /* a dash strike out of the homing dash (action 0x19, btl_act_d.c: L2 held, triangle pressed) */
    if (c->strikeHold > 0) {
        c->strikeHold--;
        *buttons |= 0x4;  /* triangle held: the strike charges */
    } else if (BtlAct_GetCurrent(chr) == 0x19) {
        if (c->dash == 0) {
            c->dash = BtlAct_CpuChance(level, "cpu_dash_strike_chance_low", 10.0f, "cpu_dash_strike_chance_high", 60.0f)
                          ? 1 : -1;
        }
        if (c->dash > 0 && dist < range * Port_ModValue("cpu_dash_strike_distance", 4.0f)) {
            if (c->dash == 1) {
                *buttons = (*buttons & ~0x4u) | 0x200;  /* triangle up first: the strike wants a fresh press */
                c->dash = 2;
            } else {
                s32 dir = BtlAct_CpuStrikeDir();

                if (dir >= 0) {
                    *buttons = (*buttons & ~0xF4u) | 0x204 | (u32)dir;
                    *x = 0.0f;
                    *y = 0.0f;
                    c->strikeHold = (s32)Rand_Range((u32)Port_ModValue("cpu_dash_strike_hold", 20.0f) + 1);
                }
                c->dash = -1;
            }
        }
    } else {
        c->dash = 0;
    }

    /* a beam ready to fire (flag 0xA7) charges while triangle stays held (btl_act_f.c: BtlSuper_HoldCharge) */
    if (BtlChar_TestFlag(chr, 0xA7) && BtlCharApi_IsInTechnique(side)) {
        if (c->beam == 0) {
            c->beam = BtlAct_CpuChance(level, "cpu_beam_charge_chance_low", 20.0f, "cpu_beam_charge_chance_high", 90.0f)
                          ? 1 : -1;
        }
        if (c->beam > 0 && (oppDown || dist > range * Port_ModValue("cpu_beam_charge_distance", 5.0f))) {
            *buttons |= 0x4;
        } else {
            c->beam = -1;  /* the opponent came closer: it fires */
        }
    } else {
        c->beam = 0;
    }
}

/* The CPU's request for the fighter, taken (cleared) when it is of the kind asked: `forms` 1 a form, 0 a skill
   (Kaioken, Ultra Instinct, Super Speed). 0 none. */
static s32 BtlAct_CpuTake(BtlActJChr *chr, s32 forms) {
    BtlCpuSide *c = &sBtlCpu[chr->player & 1];
    s32 want = c->want;

    if (want == 0 || (want >= BTL_CPU_GOD) != (forms != 0)) {
        return 0;
    }
    c->want = 0;
    c->cooldown = (s32)Port_ModValue("cpu_skill_cooldown", 60.0f);
    return want;
}

/* Whether the CPU plays the fighter and picks its forms (BtlAct_CpuThink), not the game's own AI. */
static s32 BtlAct_CpuPlaysForms(BtlActJChr *chr) {
    return Port_ModValue("cpu_skills", 1.0f) != 0.0f && BtlCharApi_IsInputInjected(chr->player);
}
#endif

/*
 * Transformation inputs: 99 (R3 tapped, no direction) = the character's default index, 100..103 (R3 tapped with
 * left / up / right / down) = index 0..3. A character with a single transformation always uses its default.
 */
s32 BtlAct_CheckTransformInput(BtlActJChr *chr) {
    s32 index = -1;
    s32 def = BtlParam_GetUnkAC(chr);

#ifdef PORT
    {
        s32 cpu = BtlAct_CpuTake(chr, 1);  /* Mod "Ultra Instinct" v56: the CPU's form (BtlAct_CpuThink) */

        if (cpu >= BTL_CPU_FORM) {
            return BtlAct_CanTransform(chr, cpu - BTL_CPU_FORM, 1, !BtlChar_TestFlag(chr, 0x10F)) ?
                   BtlAct_QueueTransform(chr, cpu - BTL_CPU_FORM) : 0;
        }
        if (cpu >= BTL_CPU_GOD) {
            return BtlAct_QueueGodForm(chr, cpu - BTL_CPU_GOD + 1);
        }
        if (BtlAct_CpuPlaysForms(chr)) {
            return 0;  /* not the game's own AI's form */
        }
    }
#endif
#ifdef PORT
    if (BtlInput_IsHeld(chr, 0x200)) {  /* L2 held: the god forms */
        s32 god = 0;

        if (BtlInput_TestAction(chr, 0x64, 1)) {
            god = 1;
        }
        if (BtlInput_TestAction(chr, 0x65, 1)) {
            god = 2;
        }
        if (BtlInput_TestAction(chr, 0x66, 1)) {
            god = 3;
        }
        if (BtlInput_TestAction(chr, 0x67, 1)) {
            god = 4;  /* Super Saiyan 4 Limit Breaker */
        }
        return god ? BtlAct_QueueGodForm(chr, god) : 0;
    }
#endif
    if (BtlInput_TestAction(chr, 0x63, 1)) {
        index = def;
    }
    if (BtlInput_TestAction(chr, 0x64, 1)) {
        index = 0;
    }
    if (BtlInput_TestAction(chr, 0x65, 1)) {
        index = 1;
    }
    if (BtlInput_TestAction(chr, 0x66, 1)) {
        index = 2;
    }
    if (BtlInput_TestAction(chr, 0x67, 1)) {
        index = 3;
    }
    if (index < 0) {
        return 0;
    }
    if (BtlParam_CountSlots(chr) == 1 && def >= 0) {
        index = def;
    }
    if (BtlAct_CanTransform(chr, index, 1, !BtlChar_TestFlag(chr, 0x10F))) {
        return BtlAct_QueueTransform(chr, index);
    }
#ifdef PORT
    if (BtlAct_CanTransform(chr, index, 0, !BtlChar_TestFlag(chr, 0x10F))) {
        extern void HudCombo_ShowModeText(s32 side, s32 kind, s32 value);

        HudCombo_ShowModeText(chr->player, 9, 0);  /* Mod "Ultra Instinct" v56: only the stamina was missing */
    }
#endif
    return 0;
}

/* Fills the form-change request for transformation `index` and queues its action (0xEC..0xF0 by sequence). */
s32 BtlAct_QueueTransform(BtlActJChr *chr, s32 index) {
    s32 chara = BtlParam_GetSlotId(chr, index);
    s32 cost = BtlParam_GetSlotCost(chr, index);
    s32 seq = BtlParam_GetSlotA0(chr, index);
    s32 kind = BtlParam_GetSlotA4(chr, index);
    s32 unk24 = BtlParam_GetSlotA8(chr, index);
    s32 action;

#ifdef PORT
    BtlAct_GodSetPending(chr, 0);
#endif
    chr->form.index = index;
    chr->form.chara = chara;
    chr->form.costume = BtlMember_GetActive(chr)->costume;
    chr->form.cost = cost;
    chr->form.partner = -1;
    chr->form.kind = kind;
    chr->form.unk24 = unk24;
    chr->form.unk28 = 0;
    chr->form.variant = BtlMember_GetActiveGauge(chr)->variant;
    chr->form.animChara = chara;
    chr->form.unk1C = chara;
    chr->form.voiceChara = chara;
    if (BtlMember_GetActive(chr)->chara == 0x6A) {
        chr->form.unk28 = 2;
    }
    if (BtlParam_TestUnkADBit(chr, index)) {
        chr->form.variant = 0;
    }
    switch (seq) {
        case 1:
            action = 0xED;
            break;
        case 2:
            action = 0xEE;
            break;
        case 3:
            action = 0xEF;
            break;
        case 4:
            action = 0xF0;
            break;
        case 0:
            action = 0xEC;
            break;
        default:
            action = 0xEC;
            break;
    }
    BtlAct_SetQueue(chr, 0, action);
    return 1;
}

/*
 * Can the fighter perform fusion `index` (0..2)? Same refusals as a transformation, plus: one of the four partner
 * characters must be a present member that is alive and not excluded (gauge unk30). The partner's member index
 * goes to *partner.
 */
s32 BtlAct_CanFuse(BtlActJChr *chr, s32 index, s32 needBlast, s32 needAllowed, s32 *partner) {
    s32 chara;
    s32 cost;
    s32 member;
    s32 i;

    if (needAllowed && chr->form.allowed == 0) {
        return 0;
    }
    if (BtlMember_HasAbility(chr, 0x5E)) {
        return 0;
    }
    if (BtlChar_TestFlag(chr, 0xAF)) {
        return 0;
    }
    if (BtlChar_TestMemberUnk70(chr)) {
        return 0;
    }
    chara = BtlParam_GetUnkB4(chr, index);
    cost = BtlParam_GetCostAE(chr, index);
    if (chara == 0xFF) {
        return 0;
    }
    if (!BattleSide_IsCharaUsable(chr->player, chara)) {
        return 0;
    }
    if (needBlast && !BtlMember_HasBlast(chr, cost)) {
        return 0;
    }
    member = -1;
    i = 0;
    while (i < 4) {
        member = BtlMember_FindPresentIndex(chr, BtlParam_GetUnkBA(chr, index, i++));
        if (member >= 0) {
            break;
        }
    }
    if (member < 0) {
        return 0;
    }
    if (BtlMember_GetGauge(chr, member)->unk30 != 0) {
        return 0;
    }
    if (BtlMember_GetGauge(chr, member)->health <= 0) {
        return 0;
    }
    if (partner != NULL) {
        *partner = member;
    }
    return 1;
}

/* Fusion inputs: 104..106 (R3 held 12 frames with left / up / right) = fusion index 0..2. */
s32 BtlAct_CheckFusionInput(BtlActJChr *chr) {
    s32 partner;
    s32 index = -1;

    if (BtlInput_TestAction(chr, 0x68, 1)) {
        index = 0;
    }
    if (BtlInput_TestAction(chr, 0x69, 1)) {
        index = 1;
    }
    if (BtlInput_TestAction(chr, 0x6A, 1)) {
        index = 2;
    }
    if (index < 0) {
        return 0;
    }
    if (BtlAct_CanFuse(chr, index, 1, !BtlChar_TestFlag(chr, 0x10F), &partner)) {
        return BtlAct_QueueFusion(chr, index, partner);
    }
    return 0;
}

/*
 * Fills the form-change request for fusion `index` with member `partner` (searched for when negative) and queues
 * action 0xF1 or 0xF2 by the fusion's sequence.
 */
s32 BtlAct_QueueFusion(BtlActJChr *chr, s32 index, s32 partner) {
    s32 chara = BtlParam_GetUnkB4(chr, index);
    s32 cost = BtlParam_GetCostAE(chr, index);
    s32 seq = BtlParam_GetUnkB1(chr, index);
    s32 unk24 = BtlParam_GetUnkB7(chr, index);
    s32 i;
    s32 action;

    if (partner < 0) {
        i = 0;
        while (i < 4) {
            partner = BtlMember_FindPresentIndex(chr, BtlParam_GetUnkBA(chr, index, i++));
            if (partner >= 0) {
                break;
            }
        }
        if (partner < 0) {
            return 0;
        }
    }
    chr->form.index = index;
    chr->form.cost = cost;
    chr->form.kind = 5;
    chr->form.unk24 = unk24;
    chr->form.chara = chara;
    chr->form.costume = 0;
    chr->form.partner = partner;
    chr->form.unk28 = BtlMember_Get(chr, partner)->costume;
    chr->form.variant = BtlMember_GetActiveGauge(chr)->variant;
    chr->form.animChara = chara;
    chr->form.unk1C = chara;
    chr->form.voiceChara = chara;
    switch (seq) {
        case 1:
            action = 0xF1;
            break;
        case 2:
            action = 0xF2;
            break;
        default:
            return 0;
    }
    BtlAct_SetQueue(chr, 0, action);
    return 1;
}

/* Can the fighter switch member? Needs two living members, no flag 0xAF, and (needGauge) a full switch gauge. */
s32 BtlAct_CanSwitch(BtlActJChr *chr, s32 needGauge, s32 needAllowed) {
    if (needAllowed && chr->form.allowed == 0) {
        return 0;
    }
    if (BtlChar_TestFlag(chr, 0xAF)) {
        return 0;
    } else {
        if (needGauge && chr->switchGauge < 100000) {
            return 0;
        }
        if (BtlMember_CountAlive(chr) < 2) {
            return 0;
        }
        return 1;
    }
}

/* Member switch input: 107 (L3 + R3 pressed). */
s32 BtlAct_CheckSwitchInput(BtlActJChr *chr) {
    if (!BtlInput_TestAction(chr, 0x6B, 1)) {
        return 0;
    }
    if (!BtlAct_CanSwitch(chr, 1, !BtlChar_TestFlag(chr, 0x10F))) {
        return 0;
    }
    return BtlAct_QueueSwitch(chr);
}

/* Picks the member to switch to and queues the switch action 0xF3. */
s32 BtlAct_QueueSwitch(BtlActJChr *chr) {
    BtlAct_PrepareSwitch(chr);
    BtlAct_SetQueue(chr, 0, 0xF3);
    return 1;
}

/* Action id of technique slot 2..4 from the technique's kind: a block of three ids per kind. */
s32 BtlAct_GetTechniqueAction(BtlActJChr *chr, s32 slot) {
    s32 n = slot - 2;

    switch (BtlSuper_GetType(chr, slot)) {
        case 0:
        case 7:
            return n + 0x106;
        case 1:
            return n + 0x10C;
        case 2:
            return n + 0x10F;
        case 3:
            return n + 0x112;
        case 6:
            return n + 0x11E;
        case 4:
        case 5:
            return n + 0x11B;
        case 9:
            return n + 0x109;
        case 8:
            return n + 0x124;
        case 10:
            return n + 0x121;
        default:
            return slot + 0x104;
    }
}

/*
 * Can technique slot 2..4 be used? Needs lock-on (flag 5), the powered-up mode (flag 6) for slot 4, and the ki
 * cost. A technique with flag 0x2000 is refused while its lock counter is positive, except in modes 5 and 6 and
 * in story mode under flag 0x11F.
 */
s32 BtlAct_CanUseTechnique(BtlActJChr *chr, s32 slot) {
    s32 check;

    if ((u32)(slot - 2) >= 3) {
        return 0;
    }
    if (BtlChar_TestMemberUnk70(chr)) {
        return 0;
    }
    if (!BtlChar_TestFlag(chr, 5)) {
        return 0;
    }
    if (slot == 4 && !BtlChar_TestFlag(chr, 6)) {
        return 0;
    }
    if (!BtlMember_HasKi(chr, BtlSuper_GetKiCost(chr, slot))) {
        return 0;
    }
    if (BtlSuper_GetFlags(chr, slot) & 0x2000) {
        if (Battle_GetMode() == 5) {
            return 1;
        }
        if (Battle_GetMode() == 6) {
            return 1;
        }
        check = 1;
        if (Battle_GetMode() == 1) {
            check = !BtlChar_TestFlag(chr, 0x11F);
        }
        if (check) {
            if (BtlMember_GetActiveGauge(chr)->techLock[slot] > 0) {
                return 0;
            }
        }
    }
    return 1;
}

/*
 * Technique inputs: 108 / 109 / 110 (charge + blast, neutral / up / down, or flags 0x122..0x124) = slot 2 / 3 / 4.
 * Queues [0x1B first when flag 0x13 is set and the technique allows it], [0x105 for slot 4], the technique action.
 */
s32 BtlAct_CheckTechniqueInput(BtlActJChr *chr) {
    s32 slot = -1;
    s32 n = 0;
    s32 pre;
    s32 flags;
    s32 kind;

    if (chr->techDelay > 0) {
        return 0;
    }
    if (BtlInput_TestAction(chr, 0x6C, 1)) {
        slot = 2;
    }
    if (BtlInput_TestAction(chr, 0x6D, 1)) {
        slot = 3;
    }
    if (BtlInput_TestAction(chr, 0x6E, 1)) {
        slot = 4;
    }
    if (slot < 0) {
        return 0;
    }
    if (!BtlAct_CanUseTechnique(chr, slot)) {
        return 0;
    }
    if (!BtlChar_IsFree(chr)) {
        return 0;
    }
    if (BtlChar_TestFlag(chr, 0x13)) {
        flags = BtlSuper_GetFlags(chr, slot) & 0x200;
        kind = BtlSuper_GetType(chr, slot);
        pre = flags == 0;
        if (kind == 10) {
            pre = 0;
        }
        if (pre) {
            BtlAct_SetQueue(chr, 0, 0x1B);
            n = 1;
        }
    }
    if (slot == 4) {
        BtlAct_SetQueue(chr, n++, 0x105);
    }
    BtlAct_SetQueue(chr, n++, BtlAct_GetTechniqueAction(chr, slot));
    return n;
}

/* Action id of skill slot 0..1 from the skill's sequence: 0xFD.., 0xFF.. or 0x101... */
s32 BtlAct_GetSkillAction(BtlActJChr *chr, s32 slot) {
    switch (BtlSkill_GetSequence(chr, slot)) {
        case 1:
            return slot + 0xFF;
        case 0:
            return slot + 0xFD;
        case 2:
            return slot + 0x101;
        default:
            return slot + 0xFD;
    }
}

/*
 * Can skill slot 0..1 be used? Needs lock-on when the skill has flag 1, the blast cost, no lock counter when it
 * has flag 0x10, and room for what it gives: skills 0xC / 0x32 refill the evasions (not while any are left, or
 * with flag 0x100 while 3 are left), 0x37 the second kind, 0x41 the counter at + 0xE5C.
 */
s32 BtlAct_CanUseSkill(BtlActJChr *chr, u32 slot) {
    s32 flags;
    s32 stack;

    if (slot >= 2) {
        return 0;
    }
    flags = BtlSkill_GetFlags(chr, slot);
    stack = (flags >> 8) & 1;
    if (BtlChar_TestMemberUnk70(chr)) {
        return 0;
    }
    if (!BtlChar_TestFlag(chr, 5) && (flags & 1)) {
        return 0;
    }
    if (!BtlMember_HasBlast(chr, BtlSkill_GetBlastCost(chr, slot))) {
        return 0;
    }
    if ((flags & 0x10) && BtlMember_GetActiveGauge(chr)->skillLock[slot] > 0) {
        return 0;
    }
    switch (BtlSkill_GetId(chr, slot)) {
        case 0xC:
        case 0x32:
            if (stack) {
                if (chr->dodges >= 3) {
                    return 0;
                }
            } else {
                if (chr->dodges > 0) {
                    return 0;
                }
            }
            break;
        case 0x37:
            if (stack) {
                if (chr->dodgesB >= 3) {
                    return 0;
                }
            } else {
                if (chr->dodgesB > 0) {
                    return 0;
                }
            }
            break;
        case 0x41:
            if (chr->unkE5C >= 3) {
                return 0;
            }
            break;
    }
    return 1;
}

#ifdef PORT
/* Mod "Ultra Instinct" v56: Kaioken for every adult Goku on L2 + down + circle (btl_act_h_b.c). */
extern s32 BtlAct_IsGoku(BtlActJChr *chr);
extern s32 BtlAct_HasModeAbility(BtlActJChr *chr, s32 which);
extern void BtlAct_ToggleMode(BtlActJChr *chr, s32 bit);
extern void BtlAct_KaiokenQueue(BtlActJChr *chr);
extern void BtlAct_KaiokenUnqueue(BtlActJChr *chr);
extern s32 BtlMember_HasStamina(BtlActJChr *chr, s32 points);
extern f32 Port_ModValue(const char *key, f32 def);
extern s32 BtlSuper_GetType(BtlActJChr *chr, s32 slot);
#endif

/*
 * Skill inputs: 113 / 114 (charge + guard, neutral / up, or flags 0x120 / 0x121) = slot 0 / 1. During an
 * animation with flag 0x10 only skills with flag 8 are accepted.
 */
s32 BtlAct_CheckSkillInput(BtlActJChr *chr) {
    s32 slot = -1;

#ifdef PORT
    {
        s32 cpu = BtlAct_CpuTake(chr, 0);  /* Mod "Ultra Instinct" v56: the CPU's skill (BtlAct_CpuThink) */

        if (cpu == BTL_CPU_KAIOKEN && BtlAct_HasModeAbility(chr, 0) && BtlChar_IsFree(chr) &&
            BtlMember_HasStamina(chr, (s32)Port_ModValue("kaioken_stamina", 5.0f))) {
            BtlAct_KaiokenQueue(chr);
            BtlAct_SetQueue(chr, 0, 0xFE);  /* as L2 + down + circle */
            return 1;
        }
        if ((cpu == BTL_CPU_UI || cpu == BTL_CPU_SPEED) && BtlChar_IsFree(chr) &&
            !BtlAct_ModeOn(chr, cpu == BTL_CPU_UI ? 0x200 : 0x400)) {
            BtlAct_ToggleMode(chr, cpu == BTL_CPU_UI ? 0x200 : 0x400);
        }
    }
#endif
#ifdef PORT
    if (BtlInput_IsHeld(chr, 0x200) && BtlInput_IsPressed(chr, 0x4000) && BtlAct_HasModeAbility(chr, 0)) {
        if (!BtlChar_IsFree(chr) || !BtlMember_HasStamina(chr, (s32)Port_ModValue("kaioken_stamina", 5.0f))) {
            return 0;
        }
        BtlAct_KaiokenQueue(chr);
        BtlAct_SetQueue(chr, 0, 0xFE);  /* the next Kaioken level */
        return 1;
    }
    if (BtlInput_TestAction(chr, 0x71, 1) && BtlInput_IsHeld(chr, 0x20) && BtlAct_HasModeAbility(chr, 0)) {
        if (!BtlChar_IsFree(chr) || !BtlMember_HasStamina(chr, (s32)Port_ModValue("kaioken_stamina", 5.0f))) {
            return 0;
        }
        BtlAct_KaiokenQueue(chr);
        BtlAct_SetQueue(chr, 0, 0xFE);  /* the plain skill action of slot 1 */
        return 1;
    }
    if (BtlInput_TestAction(chr, 0x71, 1) && BtlInput_IsHeld(chr, 0xC0)) {
        s32 ui = BtlInput_IsHeld(chr, 0x80);  /* right: Ultra Instinct; left: Super Speed */

        if (BtlAct_HasModeAbility(chr, ui ? 1 : 2)) {
            if (BtlChar_IsFree(chr)) {
                BtlAct_ToggleMode(chr, ui ? 0x200 : 0x400);
            }
            return 0;
        }
    }
#endif
    if (BtlInput_TestAction(chr, 0x71, 1)) {
        slot = 0;
    }
    if (BtlInput_TestAction(chr, 0x72, 1)) {
        slot = 1;
    }
    if (slot < 0) {
        return 0;
    }
    if (!BtlAct_CanUseSkill(chr, slot)) {
        return 0;
    }
    if (!BtlChar_IsFree(chr)) {
        return 0;
    }
    if ((BtlAnim_GetFlags(BtlAnim_GetId(chr)) & 0x10) && !(BtlSkill_GetFlags(chr, slot) & 8)) {
        return 0;
    }
#ifdef PORT
    BtlAct_KaiokenUnqueue(chr);
#endif
    BtlAct_SetQueue(chr, 0, BtlAct_GetSkillAction(chr, slot));
    return 1;
}

/*
 * Queues the damage action for a pending hit reaction (HitReact.reaction, fighter + 0xFB0); called every frame by
 * BtlAct_CheckForced and by the handler of actions 0xB8 / 0xBA.
 *   - reactions 0 and 1: nothing (returns 0). Reaction 2 (no flinch): nothing unless the fighter is dead.
 *   - reactCount[reaction] is incremented (saturating at 100); 0x23 and 0x24 also increment each other's count.
 *   - reactions 5..13, 35, 36, 42, 45..47 taken for the second time or more (count >= 2) become reaction 0x10,
 *     with flag 0x94 and the fighter turned along the hit (hitYaw, or hitYaw + pi when not hit from behind).
 *   - on a dead fighter, reactions 2..9, 23..25, 42..48 become reaction 0xF the same way.
 *   - reactTimer[reaction] = 0, then reaction -> action:
 *        4 0xC1    5 0xC2    6 0xC3    7 0xC4    8 0xC5    9 0xC6   10 0xCE   11 0xCD   12 0xCB   13 0xCC
 *       14 0xDA   15 0xCF   16 0xD0   17 0xD5   18 0xD6   19 0xD4   20 0xB8   22 0xBA
 *       23 / 24 / 25  0xD3, and stunTime = 15 / 30 / 45 frames unless it is already running
 *       26 / 27 / 28  0xE0, and stunTime = 15 / 30 / 45 the same way
 *       29, 30  0x131 + unkE9C      31 0xDB   32 0xDC   33 0xDD   34 0x137 + unkE9C
 *       35 0xC7   36 0xC8   37 0xBC   38 0xD1   39 0xCB   40 0xDF   41 0x134 + unkFF4
 *       42 0xC6   43 0xC4   44 0xC5   45 0xC6   46 0xC4   47 0xC6   48 0xC6   49 0xBF
 *       2, 3, 21 and anything else  0xC0
 *   - event 0x55 is raised for the opponent's player when the current animation has any flag; flag 0x129 is set.
 */
s32 BtlAct_QueueReaction(BtlActJChr *chr, u32 reaction) {
    s32 action;

    if (reaction < 2) {
        return 0;
    }
    if (reaction == 2 && !BtlChar_IsDead(chr)) {
        return 0;
    }
    if (chr->reactCount[reaction] < 100) {
        chr->reactCount[reaction]++;
    }
    switch (reaction) {
        case 0x23:
            chr->reactCount[0x24]++;
            break;
        case 0x24:
            chr->reactCount[0x23]++;
            break;
    }
    switch (reaction) {
        case 5:
        case 6:
        case 7:
        case 8:
        case 9:
        case 10:
        case 11:
        case 12:
        case 13:
        case 35:
        case 36:
        case 42:
        case 45:
        case 46:
        case 47:
            if (chr->reactCount[reaction] >= 2) {
                BtlChar_SetFlag(chr, 0x94);
                reaction = 0x10;
                if (chr->hitBack != 0) {
                    chr->turnYaw = chr->hitYaw;
                    chr->turnPitch = 0;
                } else {
                    chr->turnYaw = BtlUtil_WrapAngle(chr->hitYaw + 3.14159265f);
                    chr->turnPitch = 0;
                }
            }
            break;
    }
    if (BtlChar_IsDead(chr)) {
        switch (reaction) {
            case 2:
            case 3:
            case 4:
            case 5:
            case 6:
            case 7:
            case 8:
            case 9:
            case 23:
            case 24:
            case 25:
            case 42:
            case 43:
            case 44:
            case 45:
            case 46:
            case 47:
            case 48:
                BtlChar_SetFlag(chr, 0x94);
                reaction = 0xF;
                if (chr->hitBack != 0) {
                    chr->turnYaw = chr->hitYaw;
                    chr->turnPitch = 0;
                } else {
                    chr->turnYaw = BtlUtil_WrapAngle(chr->hitYaw + 3.14159265f);
                    chr->turnPitch = 0;
                }
                break;
        }
    }
    chr->reactTimer[reaction] = 0;
    switch (reaction) {
        case 4:
            action = 0xC1;
            break;
        case 5:
            action = 0xC2;
            break;
        case 6:
            action = 0xC3;
            break;
        case 7:
        case 43:
        case 46:
            action = 0xC4;
            break;
        case 8:
        case 44:
            action = 0xC5;
            break;
        case 9:
        case 42:
        case 45:
        case 47:
        case 48:
            action = 0xC6;
            break;
        case 10:
            action = 0xCE;
            break;
        case 11:
            action = 0xCD;
            break;
        case 14:
            action = 0xDA;
            break;
        case 15:
            action = 0xCF;
            break;
        case 16:
            action = 0xD0;
            break;
        case 38:
            action = 0xD1;
            break;
        case 17:
            action = 0xD5;
            break;
        case 18:
            action = 0xD6;
            break;
        case 19:
            action = 0xD4;
            break;
        case 20:
            action = 0xB8;
            break;
        case 37:
            action = 0xBC;
            break;
        case 22:
            action = 0xBA;
            break;
        case 49:
            action = 0xBF;
            break;
        case 12:
        case 39:
            action = 0xCB;
            break;
        case 13:
            action = 0xCC;
            break;
        case 31:
            action = 0xDB;
            break;
        case 32:
            action = 0xDC;
            break;
        case 33:
            action = 0xDD;
            break;
        case 35:
            action = 0xC7;
            break;
        case 36:
            action = 0xC8;
            break;
        case 40:
            action = 0xDF;
            break;
        case 23:
            action = 0xD3;
            if (chr->stunTime <= 0) {
                chr->stunTime = 15;
            }
            break;
        case 24:
            action = 0xD3;
            if (chr->stunTime <= 0) {
                chr->stunTime = 30;
            }
            break;
        case 25:
            action = 0xD3;
            if (chr->stunTime <= 0) {
                chr->stunTime = 45;
            }
            break;
        case 26:
            action = 0xE0;
            if (chr->stunTime <= 0) {
                chr->stunTime = 15;
            }
            break;
        case 27:
            action = 0xE0;
            if (chr->stunTime <= 0) {
                chr->stunTime = 30;
            }
            break;
        case 28:
            action = 0xE0;
            if (chr->stunTime <= 0) {
                chr->stunTime = 45;
            }
            break;
        case 34:
            action = chr->unkE9C + 0x137;
            break;
        case 29:
        case 30:
            action = chr->unkE9C + 0x131;
            break;
        case 41:
            action = chr->unkFF4 + 0x134;
            break;
        case 3:
        default:
            action = 0xC0;
            break;
    }
    if (BtlAnim_GetFlags(BtlAnim_GetId(chr))) {
        BtlEvent_Raise(BtlOpp_GetPlayer(chr), 0x55);
    }
    BtlChar_SetFlag(chr, 0x129);
    BtlAct_SetQueue(chr, 0, action);
    return 1;
}

/* Action that follows actions 0x12D..0x12F, 0x133..0x135 and 0x139..0x13B, by `kind` 0..7 (5 and others: 0xCF). */
s32 BtlAct_GetLandingAction(BtlActJChr *chr, s32 arg1, u32 kind) {
    s32 action = 0xCF;

    switch (kind) {
        case 0:
            action = 0xD8;
            break;
        case 1:
            action = 0xD9;
            break;
        case 2:
            action = 0xCF;
            break;
        case 3:
            action = 0xD0;
            break;
        case 4:
            action = 0xD5;
            break;
        case 6:
            action = 0xD4;
            break;
        case 7:
            action = 0xDB;
            break;
    }
    return action;
}

/* Flag 0x21, or flag 0x22: queues 0x25 / 0x27 (by BtlParam_CanFly), or 0x28 for flag 0x22 under lock-on. */
s32 BtlAct_QueueFlag21Action(BtlActJChr *chr) {
    if (BtlChar_TestFlag(chr, 0x21)) {
        if (!BtlParam_CanFly(chr)) {
            BtlAct_SetQueue(chr, 0, 0x25);
            return 1;
        } else {
            BtlAct_SetQueue(chr, 0, 0x27);
            return 1;
        }
    }
    if (BtlChar_TestFlag(chr, 0x22)) {
        if (!BtlChar_TestFlag(chr, 5)) {
            if (!BtlParam_CanFly(chr)) {
                BtlAct_SetQueue(chr, 0, 0x25);
                return 1;
            } else {
                BtlAct_SetQueue(chr, 0, 0x27);
                return 1;
            }
        } else {
            BtlAct_SetQueue(chr, 0, 0x28);
            return 1;
        }
    }
    return 0;
}

/* Flag 0x21: queues 0x29. Flag 0x22: queues 0x2A under lock-on, else 0x29. */
s32 BtlAct_QueueFlag21ActionB(BtlActJChr *chr) {
    if (BtlChar_TestFlag(chr, 0x21)) {
        BtlAct_SetQueue(chr, 0, 0x29);
        return 1;
    }
    if (BtlChar_TestFlag(chr, 0x22)) {
        if (!BtlChar_TestFlag(chr, 5)) {
            BtlAct_SetQueue(chr, 0, 0x29);
            return 1;
        } else {
            BtlAct_SetQueue(chr, 0, 0x2A);
            return 1;
        }
    }
    return 0;
}

/*
 * Recovery input of a fighter that is down or tumbling (actions 0xD8, 0xDA..0xDE). With flag 0x11: any direction
 * or any face button queues 0xE6. Otherwise a direction queues 0xE2 / 0xE3 (left / right) or 0xE5 / 0xE4 (up /
 * down); each pair is swapped when the model does not face the same way as the fighter camera (model yaw 90
 * degrees or more off the camera yaw). A face button alone queues 0xE1. `held` tests held directions instead of
 * pressed ones; a face button press always makes the directions "held".
 */
s32 BtlAct_CheckRecoveryInput(BtlActJChr *chr, s32 held) {
    BtlActJPose *pose = BtlChar_GetPos(chr);
    s32 (*test)(BtlActJChr *chr, u32 mask);
    s32 button;
    s32 front;

    if (held) {
        test = BtlInput_IsHeld;
    } else {
        test = BtlInput_IsPressed;
    }
    if (!BtlChar_IsFree(chr)) {
        return 0;
    }
    if (BtlChar_TestFlag(chr, 0x11)) {
        if (test(chr, 0xF0)) {
            BtlAct_SetQueue(chr, 0, 0xE6);
            return 1;
        }
        if (test(chr, 0x100000)) {
            BtlAct_SetQueue(chr, 0, 0xE6);
            return 1;
        }
    } else {
        button = BtlInput_IsPressed(chr, 0x100000);
        front = 0;
        if (button) {
            test = BtlInput_IsHeld;
        }
        if (__builtin_fabsf(BtlUtil_WrapAngle(pose->rot.y - chr->camYaw)) < 1.5707963f) {
            front = 1;
        }
        if (test(chr, 0x40)) {
            BtlAct_SetQueue(chr, 0, front ? 0xE2 : 0xE3);
            return 1;
        }
        if (test(chr, 0x80)) {
            BtlAct_SetQueue(chr, 0, front ? 0xE3 : 0xE2);
            return 1;
        }
        if (test(chr, 0x10)) {
            BtlAct_SetQueue(chr, 0, front ? 0xE5 : 0xE4);
            return 1;
        }
        if (test(chr, 0x20)) {
            BtlAct_SetQueue(chr, 0, front ? 0xE4 : 0xE5);
            return 1;
        }
        if (button) {
            BtlAct_SetQueue(chr, 0, 0xE1);
            return 1;
        }
    }
    return 0;
}

/*
 * Story mode (mode 1) only: actions forced by the battle script through fighter flags. Flag 0xFA queues action 4.
 * Outside a damage action: 0x116..0x119 transformation 0..3, 0x11A..0x11C fusion 0..2, 0x11D member switch (no
 * cost or availability check). Always: 0x110..0x115 queue actions 5..10. The flag is consumed (except 0xFA).
 */
s32 BtlAct_CheckStoryForced(BtlActJChr *chr) {
    if (Battle_GetMode() != 1) {
        return 0;
    }
    if (BtlChar_TestFlag(chr, 0xFA)) {
        BtlAct_SetQueue(chr, 0, 4);
        return 1;
    }
    if (!BtlAct_IsTechniqueId(BtlAct_GetCurrent(chr))) {
        if (BtlChar_TestFlag(chr, 0x116)) {
            BtlChar_ClearFlag(chr, 0x116);
            BtlAct_QueueTransform(chr, 0);
            return 1;
        }
        if (BtlChar_TestFlag(chr, 0x117)) {
            BtlChar_ClearFlag(chr, 0x117);
            BtlAct_QueueTransform(chr, 1);
            return 1;
        }
        if (BtlChar_TestFlag(chr, 0x118)) {
            BtlChar_ClearFlag(chr, 0x118);
            BtlAct_QueueTransform(chr, 2);
            return 1;
        }
        if (BtlChar_TestFlag(chr, 0x119)) {
            BtlChar_ClearFlag(chr, 0x119);
            BtlAct_QueueTransform(chr, 3);
            return 1;
        }
        if (BtlChar_TestFlag(chr, 0x11A)) {
            BtlChar_ClearFlag(chr, 0x11A);
            BtlAct_QueueFusion(chr, 0, -1);
            return 1;
        }
        if (BtlChar_TestFlag(chr, 0x11B)) {
            BtlChar_ClearFlag(chr, 0x11B);
            BtlAct_QueueFusion(chr, 1, -1);
            return 1;
        }
        if (BtlChar_TestFlag(chr, 0x11C)) {
            BtlChar_ClearFlag(chr, 0x11C);
            BtlAct_QueueFusion(chr, 2, -1);
            return 1;
        }
        if (BtlChar_TestFlag(chr, 0x11D)) {
            BtlChar_ClearFlag(chr, 0x11D);
            BtlAct_QueueSwitch(chr);
            return 1;
        }
    }
    if (BtlChar_TestFlag(chr, 0x110)) {
        BtlChar_ClearFlag(chr, 0x110);
        BtlAct_SetQueue(chr, 0, 5);
        return 1;
    }
    if (BtlChar_TestFlag(chr, 0x111)) {
        BtlChar_ClearFlag(chr, 0x111);
        BtlAct_SetQueue(chr, 0, 6);
        return 1;
    }
    if (BtlChar_TestFlag(chr, 0x112)) {
        BtlChar_ClearFlag(chr, 0x112);
        BtlAct_SetQueue(chr, 0, 7);
        return 1;
    }
    if (BtlChar_TestFlag(chr, 0x113)) {
        BtlChar_ClearFlag(chr, 0x113);
        BtlAct_SetQueue(chr, 0, 8);
        return 1;
    }
    if (BtlChar_TestFlag(chr, 0x114)) {
        BtlChar_ClearFlag(chr, 0x114);
        BtlAct_SetQueue(chr, 0, 9);
        return 1;
    }
    if (BtlChar_TestFlag(chr, 0x115)) {
        BtlChar_ClearFlag(chr, 0x115);
        BtlAct_SetQueue(chr, 0, 10);
        return 1;
    }
    return 0;
}

/* Action a fighter on the ground goes to: 0xE0 while the stun countdown runs, else 0xD8. */
s32 BtlAct_GetDownAction(BtlActJChr *chr) {
    return chr->stunTime > 0 ? 0xE0 : 0xD8;
}

/*
 * Attack id of a paid evasion (flag 0x7C) and of action 0x39: (own + opponent's counter at + 0xD88) % 3 picks
 * 0x89, 0x8A, or by height 0x8B (low) / 0x8C (above half of the height range).
 */
s32 BtlAct_GetEvasionAttack(BtlActJChr *chr) {
    s32 n = chr->unkD88;

    n += BtlOpp_GetUnkD88(chr);
    switch (n % 3) {
        case 1:
            return 0x89;
        case 0:
            return BtlAct_GetHeightRatio(chr) > 0.5f ? 0x8C : 0x8B;
        case 2:
            return 0x8A;
    }
    return 0x8C;
}

/*
 * Follow-up of actions 0x67..0x69 by `kind`: 0 = 0x6A, 1..4 = 0x9E..0xA1 (attack ids: the queue is built with
 * BtlDecide_QueueAttack and its first action returned), anything else 0x6A.
 */
s32 BtlAct_GetChainAction(BtlActJChr *chr, u32 kind) {
    s32 id;

    switch (kind) {
        case 0:
            id = 0x6A;
            break;
        case 1:
            id = 0x9E;
            break;
        case 2:
            id = 0x9F;
            break;
        case 3:
            id = 0xA0;
            break;
        case 4:
            id = 0xA1;
            break;
        case 5:
        default:
            return 0x6A;
    }
    if (!BtlAct_IsAttackId(id)) {
        return id;
    }
    if (BtlDecide_QueueAttack(chr, id)) {
        return BtlAct_GetQueued(chr);
    }
    return 0x6A;
}

/*
 * What a fighter does when a damage action ends: idle (0xB), unless an input (BtlDecide_Main with mask 0x80000)
 * or a status override (BtlDecide_Common with mask 0xF) queues something else.
 */
s32 BtlAct_GetIdleFollowUp(BtlActJChr *chr) {
    BtlAct_SetQueue(chr, 0, 0xB);
    BtlDecide_Main(chr, 0x80000);
    BtlDecide_Common(chr, 0xF);
    return BtlAct_GetQueued(chr);
}

/* 0x14 when BtlParam_CanFly holds for the character, else 0x16. */
s32 BtlAct_GetAction14or16(BtlActJChr *chr) {
    return !BtlParam_CanFly(chr) ? 0x16 : 0x14;
}

/* 0x15 when BtlParam_CanFly holds or flag 0x11 is set, else 0x11. */
s32 BtlAct_GetAction11or15(BtlActJChr *chr) {
    if (!BtlParam_CanFly(chr)) {
        if (!BtlChar_TestFlag(chr, 0x11)) {
            return 0x11;
        }
    }
    return 0x15;
}
