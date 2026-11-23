// The script machine (0x16EF4) that runs unit, structure and team scripts.
// A script: +0 position (0 stopped), +4 file (+4 code start, +0x10 the
// function table), +8 return value, +0xA frame byte, +0xB stack byte (15
// empty), +0xC five variables, +0x16 a stack of 15 words, +0x34 in a
// subroutine. An instruction word: bit 15 a jump to its low 15 bits; else
// bits 8-12 the opcode with a parameter in the low byte (bit 14, signed) or
// the next word (bit 13).
#include "game.hpp"

namespace game {

namespace {

uint32_t Slot(uint32_t s, int16_t i) { return s + 0x16 + uint32_t(int32_t(i) * 2); }

void Push(uint32_t s, uint16_t v) {
    U8{s + 0xB} -= 1;
    wr16(Slot(s, int8_t(rd8(s + 0xB))), v);
}

uint16_t Pop(uint32_t s) {
    int16_t i = int8_t(rd8(s + 0xB));
    U8{s + 0xB} += 1;
    return rd16(Slot(s, i));
}

int8_t Frame(uint32_t s) { return int8_t(rd8(s + 0xA)); }
uint32_t Code(uint32_t s) { return rd32(rd32(s + 4) + 4); }

uint16_t Stop(uint32_t s) {
    wr32(s, 0);
    return 0;
}

uint16_t Binary(uint16_t op, int16_t a, int16_t b) {
    switch (op) {
    case 0: return a && b;
    case 1: return a || b;
    case 2: return a == b;
    case 3: return a != b;
    case 4: return a < b;
    case 5: return a <= b;
    case 6: return a > b;
    case 7: return a >= b;
    case 8: return uint16_t(a + b);
    case 9: return uint16_t(a - b);
    case 10: return uint16_t(a * b);
    case 11: return b ? uint16_t(int32_t(a) / b) : uint16_t(a);
    case 12: {
        int n = b & 63;
        return uint16_t(n >= 16 ? (a < 0 ? -1 : 0) : a >> n);
    }
    case 13: {
        int n = b & 63;
        return uint16_t(n >= 16 ? 0 : uint16_t(a) << n);
    }
    case 14: return uint16_t(a & b);
    case 15: return uint16_t(a | b);
    case 16: return (b == 0 || (a == -32768 && b == -1)) ? 0 : uint16_t(int32_t(a) % b);
    case 17: return uint16_t(a ^ b);
    default: return 0xFFFF;
    }
}

} // namespace

// 0x16EF4: one instruction; 0 when the script stopped.
uint16_t ScriptStep(uint32_t s) {
    if (!s || !rd32(s)) return 0;
    uint32_t pc = rd32(s);
    wr32(s, pc + 2);
    uint16_t w = rd16(pc);
    uint16_t op = (w >> 8) & 0x1F;
    uint16_t p = 0;
    if (w & 0x8000) {
        op = 0;
        p = w & 0x7FFF;
    } else if (w & 0x4000) {
        p = uint16_t(int8_t(w));
    } else if (w & 0x2000) {
        p = rd16(rd32(s));
        wr32(s, rd32(s) + 2);
    }
    switch (op) {
    case 0: wr32(s, Code(s) + uint32_t(p) * 2); return 1;
    case 1: wr16(s + 8, p); return 1;
    case 2:
        if (p == 0) {
            Push(s, rd16(s + 8));
            return 1;
        }
        if (p == 1) {
            int32_t d = int32_t(rd32(s) - Code(s));
            Push(s, uint16_t(d / 2 + 1));
            Push(s, uint16_t(int16_t(Frame(s))));
            wr8(s + 0xA, uint8_t(rd8(s + 0xB) + 2));
            return 1;
        }
        return Stop(s);
    case 3:
    case 4: Push(s, p); return 1;
    case 5: Push(s, rd16(s + 0xC + uint32_t(int16_t(p) * 2))); return 1;
    case 6: Push(s, rd16(Slot(s, int16_t(Frame(s) - int16_t(p) - 2)))); return 1;
    case 7: Push(s, rd16(Slot(s, int16_t(Frame(s) + int16_t(p) - 1)))); return 1;
    case 8:
        if (p == 0) {
            wr16(s + 8, Pop(s));
            return 1;
        }
        if (p == 1) {
            if (rd8(s + 0xB) == 0xF) return Stop(s);
            wr8(s + 0xA, uint8_t(Pop(s)));
            int16_t loc = int16_t(Pop(s));
            wr32(s, Code(s) + uint32_t(int32_t(loc) * 2));
            return 1;
        }
        return Stop(s);
    case 9: {
        uint16_t v = Pop(s);
        wr16(s + 0xC + uint32_t(int16_t(p) * 2), v);
        return 1;
    }
    case 10: {
        uint16_t v = Pop(s);
        wr16(Slot(s, int16_t(Frame(s) - int16_t(p) - 2)), v);
        return 1;
    }
    case 11: {
        uint16_t v = Pop(s);
        wr16(Slot(s, int16_t(Frame(s) + int16_t(p) - 1)), v);
        return 1;
    }
    case 12: U8{s + 0xB} += uint8_t(p); return 1;
    case 13: U8{s + 0xB} -= uint8_t(p); return 1;
    case 14: {
        uint32_t fn = rd32(rd32(rd32(s + 4) + 0x10) + (p & 0xFF) * 4u);
        wr16(s + 8, ScriptFunction(fn, s));
        return 1;
    }
    case 15:
        if (Pop(s)) return 1;
        wr32(s, Code(s) + uint32_t(p & 0x7FFF) * 2);
        return 1;
    case 16: {
        uint16_t v = Pop(s);
        switch (p) {
        case 0: v = !v; break;
        case 1: v = uint16_t(-v); break;
        case 2: v = uint16_t(~v); break;
        default: return Stop(s);
        }
        Push(s, v);
        return 1;
    }
    case 17: {
        int16_t b = int16_t(Pop(s));
        int16_t a = int16_t(Pop(s));
        if ((p & 0x1F) > 17) return Stop(s);
        Push(s, Binary(p & 0x1F, a, b));
        return 1;
    }
    case 18: {
        if (rd8(s + 0xB) == 0xF) return Stop(s);
        wr16(s + 8, Pop(s));
        int16_t loc = int16_t(Pop(s));
        wr8(s + 0x34, 0);
        wr32(s, Code(s) + uint32_t(int32_t(loc) * 2));
        return 1;
    }
    default: return Stop(s);
    }
}

// Script functions by ROM address; each takes the script.
uint16_t ScriptFunction(uint32_t fn, uint32_t s) {
    switch (fn) {
    case 0xC652: return ScriptNothing(s);
    case 0xC656: return StructState(s);
    case 0xC662: return StructCheckLink(s);
    case 0xC6C4: return StructCallCarryall(s);
    case 0xC7D0: return StructSetState(s);
    case 0xC81C: return StructReleaseUnit(s);
    case 0xCA10: return StructFindTarget(s);
    case 0xCB96: return StructTurn(s);
    case 0xCD0A: return StructTargetDirection(s);
    case 0xCD72: return StructReleaseUnitTarget(s);
    case 0xCDD4: return StructFire(s);
    case 0xCFE6: return StructSound(s);
    case 0xD016: return StructReveal(s);
    case 0xD06C: return StructRefine(s);
    case 0xD1EE: return StructExplode(s);
    case 0xD4CC: return StructSurvivors(s);
    case 0x11018: return ScriptPrint(s);
    case 0x1108E: return ScriptDelay(s);
    case 0x110AA: return ScriptDelayRandom(s);
    case 0x110DC: return ScriptRandomRange(s);
    case 0x1110A: return ScriptNothing(s);
    case 0x11110: return ScriptDistanceToTarget(s);
    case 0x11156: return ScriptDistanceToObject(s);
    case 0x1118E: return ScriptIsNotObject(s);
    case 0x111EA: return ScriptTargetFacing(s);
    case 0x11216: return ScriptCountUnits(s);
    case 0x11264: return ScriptTargetCell(s);
    case 0x11296: return ScriptTargetKind(s);
    case 0x112C8: return ScriptLinkedType(s);
    case 0x112F2: return ScriptNothing(s);
    case 0x112F6: return ScriptFindSpice(s);
    case 0x1135C: return ScriptIsFriend(s);
    case 0x113B0: return ScriptIsEnemy(s);
    case 0x11442: return ScriptFreeStructure(s);
    case 0x2E508: return ScriptNothing(s);
    case 0x2E50C: return TeamNothing(s);
    case 0x2E528: return TeamMembers(s);
    case 0x2E534: return TeamMinimum(s);
    case 0x2E540: return TeamField1A(s);
    case 0x2E54C: return TeamRecruit(s);
    case 0x2E684: return TeamCentre(s);
    case 0x2E7C8: return TeamRally(s);
    case 0x2E90E: return TeamFindTarget(s);
    case 0x2E9CC: return TeamAttack(s);
    case 0x2EB6E: return TeamSetMode(s);
    case 0x2EBBA: return TeamRestoreMode(s);
    case 0x43CF4: return ScriptNothing(s);
    case 0x43CF8: return UnitTargetScore(s);
    case 0x43D68: return UnitFindTarget(s);
    case 0x43D88: return UnitDrop(s);
    case 0x43FE8: return UnitPickUp(s);
    case 0x44360: return UnitStop(s);
    case 0x44388: return UnitSetSpeed(s);
    case 0x44400: return UnitSetFrame(s);
    case 0x44432: return UnitFlyTo(s);
    case 0x445F6: return UnitDie(s);
    case 0x446CC: return ScriptNothing(s);
    case 0x446D0: return UnitExplode(s);
    case 0x447EA: return UnitFire(s);
    case 0x44C4C: return UnitFace(s);
    case 0x44C80: return UnitAim(s);
    case 0x44D36: return UnitDirectionTo(s);
    case 0x44D86: return UnitHeadFor(s);
    case 0x44E0C: return UnitSetTarget(s);
    case 0x44F36: return UnitSetActionFn(s);
    case 0x44F80: return UnitDefaultAction(s);
    case 0x44F9A: return UnitFaceTarget(s);
    case 0x45014: return UnitGetInfo(s);
    case 0x452CA: return UnitFollowPath(s);
    case 0x456C0: return UnitFindStructure(s);
    case 0x457CA: return UnitSpice(s);
    case 0x457F6: return UnitIsCarrying(s);
    case 0x45810: return UnitBlowUp(s);
    case 0x45884: return UnitSpawnSoldier(s);
    case 0x4591A: return UnitCallHelp(s);
    case 0x459E0: return UnitReleaseCarrier(s);
    case 0x45A1E: return UnitFreeStructure(s);
    case 0x45A90: return UnitNoResult(s);
    case 0x45A92: return UnitReveal(s);
    case 0x45AA4: return UnitHarvest(s);
    case 0x45B72: return UnitCanDropAt(s);
    case 0x45C50: return UnitRandomCell(s);
    case 0x45CE0: return UnitFidget(s);
    case 0x45D76: return UnitGoToStructure(s);
    case 0x45E34: return UnitDeploy(s);
    case 0x45ECE: return UnitNearbyTarget(s);
    case 0x45EFA: return UnitStillLinked(s);
    case 0x45F8A: return ScriptNothing(s);
    default: untranslated(fn);
    }
}

} // namespace game
