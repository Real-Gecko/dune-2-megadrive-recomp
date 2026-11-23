// Dune - The Battle for Arrakis game code translated from the 68000 ROM.
// Functions are named by what they do; the ROM address is in a comment at
// each definition. Register and stack arguments become parameters; pointers
// into the 68000 address space stay uint32_t addresses (md::rd*/wr*).
#pragma once

#include "../display.hpp"
#include "../host.hpp"
#include "../md/mem.hpp"

#include <cstdint>
#include <initializer_list>

namespace game {

using md::S16;
using md::S32;
using md::S8;
using md::U16;
using md::U32;
using md::U8;
using md::rd16;
using md::rd32;
using md::rd8;
using md::wr16;
using md::wr32;
using md::wr8;

// Names an untranslated routine and exits (status 2).
[[noreturn]] void untranslated(uint32_t addr);
// Calls the translated routine at a 68000 address taken from a code pointer.
uint32_t call(uint32_t addr);

// --- Library RAM (asm library at 0x200-0x1900) ---
inline constexpr U32 vblank_hook{0xFFE002};  // called from VBlank when set
inline constexpr U16 vblank_count{0xFFE006};
inline constexpr U16 screen_width{0xFFE00A}; // 0x140 or 0x100
inline constexpr U16 vdp_mode2{0xFFE00C};    // VDP register 1 word last set
inline constexpr U16 vdp_autoinc{0xFFE00E};  // VDP register 15 word last set
inline constexpr U16 fade_delay{0xFFE010};   // frames per palette fade step
inline constexpr U32 fade_hook{0xFFE012};    // called every fade frame
inline constexpr U32 random_seed{0xFFE016};
inline constexpr U32 frame_counter{0xFFE01A};
inline constexpr U16 window_base{0xFFE01E};
inline constexpr U16 sprite_base{0xFFE020};
inline constexpr U16 plane_a_base{0xFFE022};
inline constexpr U16 plane_b_base{0xFFE024};
inline constexpr U16 hscroll_base{0xFFE026};
constexpr uint32_t kDmaQueue = 0xFFE02A; // 64 entries of 10 bytes
inline constexpr U16 dma_read{0xFFE2AA};
inline constexpr U16 dma_write{0xFFE2AC};
inline constexpr U8 dma_enable{0xFFE2B2};
constexpr uint32_t kPalette = 0xFFE2B4; // 64 colour words shown via DMA
inline constexpr U32 vscroll_ab{0xFFE3B6};
inline constexpr U32 vscroll_ab2{0xFFE3BA};
inline constexpr U8 pad1{0xFFE41A};      // buttons newly pressed
inline constexpr U8 pad1_held{0xFFE41B};
inline constexpr U8 pad2{0xFFE41C};
inline constexpr U8 pad2_held{0xFFE41D};
inline constexpr U8 pad_repeat_mask{0xFFE41E};
inline constexpr U8 pad_hooked{0xFFE41F};
inline constexpr U32 pad_hook{0xFFE422};
inline constexpr U8 vblank_timer{0xFFE426};
constexpr uint32_t kSpriteTable = 0xFFE428; // sprite attribute table copy
constexpr uint32_t kSoundQueue = 0xFFF800;

// --- Library (lib.cpp) ---
void VBlank();
void QueueDma(uint16_t cmd, uint32_t src, uint32_t dest_len);
void RunDmaQueue();
void WaitVBlank();
void FlushDma();
void WaitFrames(uint16_t n);
void ClearCram();
void ClearVram();
void ClearVsram();
void ClearRam();
void UploadPalette(uint32_t pal);
void ClearPaletteRange(uint16_t first, uint16_t count);
void FadeTo(uint32_t pal, uint16_t first, uint16_t count);
void FadeOut(uint16_t first, uint16_t count);
bool FadeToward(uint32_t pal, uint16_t first, uint16_t count);
void FadeInAll(uint32_t pal);
void FadeOutAll();
void UploadTiles(uint32_t src, uint16_t vram, uint16_t count);
void BlitMap(uint32_t src, uint16_t nt, uint32_t pos, uint32_t size);
uint16_t Decompress(uint32_t src, uint32_t dest);
void LoadTileList(uint32_t list, uint16_t vram);
void UnpackTiles(uint32_t src, uint16_t vram, uint16_t bytes);
void InitHardware();
uint8_t Random();
uint8_t RandomRange(uint8_t lo, uint8_t hi);
uint32_t ReadPads();
void InitSpritePool(uint32_t pool, uint32_t list);
void BootLogo();

// --- Sprite objects (sprites.cpp) ---
constexpr uint32_t kSpriteList = 0xFFF3A8; // list header; active head at +4
void StopBlink(uint32_t node);
void StartBlink(uint32_t node, uint8_t rate);
void SortSprite(uint32_t node, uint32_t list);
uint32_t AllocSprite(uint32_t pos, uint32_t attr, uint32_t frame, uint32_t list);
void FreeSprite(uint32_t node, uint32_t list);
void BuildSpriteTable(uint16_t first);
uint32_t CreateSprite(uint32_t obj, uint16_t cls, uint16_t orient);

// --- C library wrappers (api.cpp) ---
bool FadeColor(uint16_t line, uint16_t index, uint16_t target);
uint32_t NearPoint(int16_t x, int16_t y, int16_t px, int16_t py);
void SetPlaneSize(uint16_t vcode, uint16_t hcode);
uint16_t SetPlaneA(uint16_t a);
uint16_t SetPlaneB(uint16_t a);
uint16_t SetHScrollTable(uint16_t a);
uint16_t SetSpriteTable(uint16_t a);
void WriteCram(uint16_t line, uint16_t index, uint16_t count, uint32_t src);
void SpriteTableHook();
uint32_t InstallSpriteTableHook();
uint32_t SetVBlankHook(uint32_t hook);
void ClearMapRam();
void ClearUnitLists();
void UpdateFrameDelta();
void ResetFrameDelta();
void SyncFrameDelta();
uint16_t FrameDelta();
uint16_t ReadButtons();
void ReadShownPalette();
void SetSpritePalette(uint16_t node, uint16_t n);
void HideSprite(uint16_t node);
void ShowSprite(uint16_t node);
void SetScreenWidth(uint16_t wide);
uint16_t SetFadeDelay(uint16_t n);
void DisplayOff();
void DisplayOn();
void FadeIn(uint32_t pal);
void FadeOutScreen();
uint32_t FindNamed(uint32_t name);
uint32_t FindChunk(int16_t file, uint32_t id);
uint32_t RandomCorner(uint32_t pos);
void WaitLong(uint16_t n);
uint8_t TakeFlag(uint16_t i);
uint8_t TestAndSetFlag(uint16_t i);
void SetPadRepeatMask(uint16_t m);
uint32_t FrameCounter();
void BuildGameSprites();
void FadeAll(uint32_t pal);

// --- Screens and text (screen.cpp) ---
void SetWindowPos(uint16_t h, uint16_t v);
void SetWindowBase(uint16_t a);
void ResetWindowBase();
uint32_t LoadScreen(uint16_t n);
void LoadFont();
void PrintWindow(uint32_t s, uint16_t x, uint16_t y);
void PrintWindowHi(uint32_t s, uint16_t x, uint16_t y);
void PrintPlaneA(uint32_t s, uint16_t x, uint16_t y, uint16_t line);
void SetColor(uint16_t line, uint16_t index, uint16_t c);
void ClearWindowRows(uint16_t rows, uint16_t width);
void ClearPlaneARows(uint16_t rows, uint16_t width);
void ClearWindowPlane();

// --- Title (title.cpp) ---
void TitleScrollHook();
uint16_t GetKey();
uint16_t TitleScreen(uint16_t intro);
void SetPlaneBScroll(uint16_t v);
void SetPlaneAScroll(uint16_t v);

// --- Tutorial (tutorial.cpp) ---
uint16_t Tutorial();

// --- Sound (sound.cpp): commands to the GEMS driver ---
void GemsInit(uint32_t patches, uint32_t envelopes, uint32_t sequences, uint32_t samples);
void GemsStartSeq(uint32_t seq);
void GemsStopSeq(uint32_t seq);
void GemsStopAll();
void GemsSeqCommand3(uint32_t seq, uint32_t a, uint32_t b);
void LoadTitleSoundBanks();
void LoadGameSoundBanks();
void PlayMusic(uint32_t id);
void PlaySfx(uint32_t id);
void PlayMusicTrack(uint32_t track);
void PlaySoundEffect(uint32_t id);
void StartSeq29();
void StopSeq29();

// --- Mentat screens (mentat.cpp) ---
uint16_t ConfirmHouse(uint16_t house);
void MentatHook();
void MentatFadeHook();
void Briefing(uint16_t house, uint16_t scenario);

// --- House selection and scenario start (house.cpp) ---
void HouseSelectInit(uint16_t cls);
uint16_t HouseSelectPad();
void HouseSelectAnim();
uint16_t ChooseHouse();
void StartScenario(uint16_t house, uint16_t campaign);
uint16_t SetGameMode(uint16_t mode);

// --- C runtime (libc.cpp) ---
// printf arguments: varargs are pushed as longs (ints sign-extended).
struct PArg {
    uint32_t v;
};
inline PArg W(uint32_t v) { return {uint32_t(int32_t(int16_t(v)))}; }
inline PArg L(uint32_t v) { return {v}; }
uint32_t StrLen(uint32_t s);
uint32_t StrNCpy(uint32_t dst, uint32_t src, int32_t n);
uint32_t StrCat(uint32_t dst, uint32_t src);
int32_t StrCmp(uint32_t a, uint32_t b);
uint32_t MemSet(uint32_t p, uint8_t v, uint32_t n);
uint32_t MemCpy(uint32_t dst, uint32_t src, int32_t n);
uint32_t MemMove(uint32_t dst, uint32_t src, uint32_t n);
uint16_t SprintfAt(uint32_t dst, uint32_t fmt, uint32_t args);
uint16_t Sprintf(uint32_t dst, uint32_t fmt, std::initializer_list<PArg> args);

// --- World (scenario.cpp, stubs.cpp) ---
// Object nodes are word addresses read with movea.w: sign-extended RAM.
inline uint32_t Node(uint16_t w) { return uint32_t(int32_t(int16_t(w))); }
uint32_t InitUnitArray(uint32_t a);
uint32_t InitTeamArray(uint32_t a);
uint32_t InitHouseArray(uint32_t a);
uint32_t InitStructureArray(uint32_t a);
uint32_t HouseGet(uint16_t n);
uint32_t FirstHouse(uint32_t it);
uint32_t NextHouse(uint32_t it);
void ClearUnitLists();
void PlayVoice(uint16_t id);
void SetCursorCell(uint16_t cell);
void SetCursorShape(int16_t a, int16_t b);
void SetCursorLimits(uint32_t lo, uint32_t hi);
void RadarColumn(uint32_t src);
void ClearRadar();
void SetRadarMode(uint16_t full);
void UpdateRadarFrame();
void UpdateRadar();
void RefreshRadar();
void EndRadarStatic();
void PlaceRadarFrame();
bool CellInMap(uint16_t cell);
void Assert(uint32_t obj);
void UnitShadowFlag(uint32_t u);
uint16_t ScaleHealth(uint16_t max, uint16_t f);
uint32_t StructureAt(uint16_t cell);
uint32_t UnitAt(uint16_t cell);
uint16_t LoadScenario(uint16_t step, uint16_t house);
void PlayMission(uint16_t house, uint16_t step);
// untranslated so far (stubs.cpp)
void GenerateMap(uint32_t seed);
void SetMapBounds(uint32_t rect);
void UploadScroll();
void ScrollView(int16_t dx, int16_t dy);
void FreeCursorHook();
void PlacementCursorHook();
uint32_t CreateTeam(uint16_t house, uint16_t ai, uint16_t move, uint16_t min, uint16_t max);
uint32_t CreateUnit(uint16_t index, uint16_t type, uint16_t house);
uint32_t CreateUnitAt(uint16_t index, uint16_t type, uint16_t house, uint32_t target, uint16_t flag);
void SetUnitAction(uint32_t u, int16_t action);
void UnitSeenBy(uint32_t u, int16_t house);
void UnitSetFacing(uint32_t u, uint16_t facing, uint16_t immediate, uint16_t turret);
bool Allied(uint16_t a, uint16_t b);
uint32_t TeamGet(uint16_t i);
uint32_t TargetStructure(uint16_t t);
uint32_t TargetUnit(uint16_t t);
uint32_t TargetObject(uint16_t t);
void ScriptStart(uint32_t s, int16_t type);
void ScriptReset(uint32_t s, uint32_t info);
uint32_t CellPos(uint16_t cell);
uint16_t PlaceStructure(uint32_t s, uint16_t cell);
int16_t CanPlace(uint16_t cell, uint16_t type);
uint32_t UnitOnCell(uint16_t cell);
int16_t CanPlaceAI(uint16_t cell, uint16_t type);
void RevealAround(uint32_t pos, uint16_t radius);
uint16_t RevealCell(uint16_t cell, uint16_t house);
void UpdateFog(uint16_t cell);
uint16_t DistanceFine(uint32_t a, uint32_t b);
uint16_t Distance(uint32_t a, uint32_t b);
uint16_t IsSpiceCell(uint16_t cell);
void MarkCell(uint32_t cell);
void RedrawUnitsAt(uint32_t pos);
uint16_t ConnectWalls(uint16_t cell, uint16_t neighbours);
void FreeStructure(uint32_t s);
void FreeStructureBlip(uint16_t b);
void ClearFlagCell(uint32_t s);
void FreeObjectSprite(uint16_t n);
void FreeEffect(uint32_t n);
void UpdateCellSprite(uint16_t cell, uint16_t mode, uint16_t b);
uint16_t PartOf(uint16_t total, uint16_t part);
void CrushUnit(uint32_t u);
void FreeUnit(uint32_t u);
void FreeUnitBlip(uint16_t b);
uint16_t AddBlip(uint32_t u);
uint32_t LinkedUnit(uint32_t u);
void ReleaseTarget(uint32_t u);
uint16_t SetTarget(uint32_t o, uint16_t t);
uint16_t LinkTargets(uint16_t a, uint16_t b);
uint16_t TargetValid(uint16_t t);
uint16_t TargetCell(uint16_t t);
uint16_t TargetCellOf(uint16_t t);
using PathCostFn = uint16_t (*)(uint16_t cell, uint16_t dir);
uint32_t FindPath(uint16_t from, uint16_t to, uint32_t buf, uint16_t max, PathCostFn cost, uint16_t limit);
uint16_t MoveCost(uint32_t u, uint16_t cell, uint16_t facing);
uint32_t NearestUnit(uint16_t a, uint16_t house, uint16_t target, uint16_t b);
uint16_t CellDistance(uint16_t a, uint16_t b);
uint16_t CellDirection(uint16_t a, uint16_t b);
uint32_t MovePos(uint32_t pos, uint16_t dir, uint16_t dist);
uint16_t TeamWaypoint(uint16_t from, uint16_t target);
uint16_t UnitStartStep(uint32_t u);
uint16_t FindReturnTarget(uint32_t u);
uint32_t ExplosionAt(uint16_t kind, uint32_t pos, uint16_t pal);
uint16_t Deviate(uint32_t u, uint16_t house);
uint16_t LeaveTeamOf(uint32_t u);
uint32_t ScatterPos(uint32_t pos, uint16_t radius, uint16_t snap);
void RevealUnit(uint32_t u);
void ChangeSpice(uint16_t cell, int16_t delta);
void SpreadSpice(uint16_t cell, uint16_t radius);
uint16_t TickBlink(uint32_t pos);
uint32_t TargetPos(uint16_t t);
uint16_t Direction(uint32_t from, uint32_t to);
uint16_t ChooseTarget(uint32_t u, uint16_t mode);
uint16_t MoveUnit(uint32_t u, uint16_t speed);
uint16_t DamageUnit(uint32_t u, uint16_t damage, uint16_t effect);
void SpiceBloom(uint16_t cell, uint16_t house);
void Explosion(uint16_t kind, uint32_t pos, uint16_t house);
uint32_t FindStructureTarget(uint32_t u, uint16_t mode);
uint32_t FindUnitTarget(uint32_t u, uint16_t mode);
uint16_t StructureScore(uint32_t u, uint32_t s);
uint16_t UnitScore(uint32_t u, uint32_t v);
uint16_t FindSpice(uint16_t cell, uint16_t radius, uint16_t unit);
void EnterStructure(uint32_t u, uint32_t s);
uint16_t PlaceUnit(uint32_t u, uint32_t pos);
void SpawnEffect(uint16_t kind, uint32_t pos, uint16_t a, uint16_t b);
void Scorch(uint32_t pos);
void ShakeScreen(uint16_t n);
uint16_t UnitEaten(uint32_t u);
uint32_t FireProjectile(uint32_t pos, uint16_t weapon, uint16_t house, uint16_t damage, uint16_t target);
uint16_t UnitBlocked(uint32_t u);
uint32_t NearbyUnit(uint32_t u);
uint16_t Undeviate(uint32_t u, uint16_t n);
uint16_t CanEnter(uint32_t u, uint32_t s);
void SetTankFrame(uint32_t table, uint16_t n, uint16_t body, uint16_t turret, uint16_t house);
uint16_t ScriptStep(uint32_t s);
uint16_t ScriptFunction(uint32_t fn, uint32_t s);
// script functions (unitfn.cpp, structfn.cpp)
uint16_t ScriptCountUnits(uint32_t s);
uint16_t ScriptDelay(uint32_t s);
uint16_t ScriptDelayRandom(uint32_t s);
uint16_t ScriptDistanceToObject(uint32_t s);
uint16_t ScriptDistanceToTarget(uint32_t s);
uint16_t ScriptFindSpice(uint32_t s);
uint16_t ScriptFreeStructure(uint32_t s);
uint16_t ScriptIsEnemy(uint32_t s);
uint16_t ScriptIsFriend(uint32_t s);
uint16_t ScriptIsNotObject(uint32_t s);
uint16_t ScriptLinkedType(uint32_t s);
uint16_t ScriptNothing(uint32_t s);
uint16_t ScriptPrint(uint32_t s);
uint16_t ScriptRandomRange(uint32_t s);
uint16_t ScriptTargetCell(uint32_t s);
uint16_t ScriptTargetFacing(uint32_t s);
uint16_t ScriptTargetKind(uint32_t s);
uint16_t StructCallCarryall(uint32_t s);
uint16_t StructCheckLink(uint32_t s);
uint16_t StructExplode(uint32_t s);
uint16_t StructFindTarget(uint32_t s);
uint16_t StructFire(uint32_t s);
uint16_t StructRefine(uint32_t s);
uint16_t StructReleaseUnit(uint32_t s);
uint16_t StructReleaseUnitTarget(uint32_t s);
uint16_t StructReveal(uint32_t s);
uint16_t StructSetState(uint32_t s);
uint16_t StructSound(uint32_t s);
uint16_t StructState(uint32_t s);
uint16_t StructSurvivors(uint32_t s);
uint16_t StructTargetDirection(uint32_t s);
uint16_t StructTurn(uint32_t s);
uint16_t TeamAttack(uint32_t s);
uint16_t TeamCentre(uint32_t s);
uint16_t TeamField1A(uint32_t s);
uint16_t TeamFindTarget(uint32_t s);
uint16_t TeamMembers(uint32_t s);
uint16_t TeamMinimum(uint32_t s);
uint16_t TeamNothing(uint32_t s);
uint16_t TeamRally(uint32_t s);
uint16_t TeamRecruit(uint32_t s);
uint16_t TeamRestoreMode(uint32_t s);
uint16_t TeamSetMode(uint32_t s);
uint16_t UnitAim(uint32_t s);
uint16_t UnitBlowUp(uint32_t s);
uint16_t UnitCallHelp(uint32_t s);
uint16_t UnitCanDropAt(uint32_t s);
uint16_t UnitDefaultAction(uint32_t s);
uint16_t UnitDeploy(uint32_t s);
uint16_t UnitDie(uint32_t s);
uint16_t UnitDirectionTo(uint32_t s);
uint16_t UnitDrop(uint32_t s);
uint16_t UnitExplode(uint32_t s);
uint16_t UnitFace(uint32_t s);
uint16_t UnitFaceTarget(uint32_t s);
uint16_t UnitFidget(uint32_t s);
uint16_t UnitFindStructure(uint32_t s);
uint16_t UnitFindTarget(uint32_t s);
uint16_t UnitFire(uint32_t s);
uint16_t UnitFlyTo(uint32_t s);
uint16_t UnitFollowPath(uint32_t s);
uint16_t UnitFreeStructure(uint32_t s);
uint16_t UnitGetInfo(uint32_t s);
uint16_t UnitGoToStructure(uint32_t s);
uint16_t UnitHarvest(uint32_t s);
uint16_t UnitHeadFor(uint32_t s);
uint16_t UnitIsCarrying(uint32_t s);
uint16_t UnitNearbyTarget(uint32_t s);
uint16_t UnitNoResult(uint32_t s);
uint16_t UnitPickUp(uint32_t s);
uint16_t UnitRandomCell(uint32_t s);
uint16_t UnitReleaseCarrier(uint32_t s);
uint16_t UnitReveal(uint32_t s);
uint16_t UnitSetActionFn(uint32_t s);
uint16_t UnitSetFrame(uint32_t s);
uint16_t UnitSetSpeed(uint32_t s);
uint16_t UnitSetTarget(uint32_t s);
uint16_t UnitSpawnSoldier(uint32_t s);
uint16_t UnitSpice(uint32_t s);
uint16_t UnitStillLinked(uint32_t s);
uint16_t UnitStop(uint32_t s);
uint16_t UnitTargetScore(uint32_t s);
void UnitUnseen(uint32_t u, int16_t house);
void LiftUnit(uint32_t u);
void DropSelection();
void CancelOrder(uint16_t a, uint32_t u);
void ForgetObject(uint32_t o);
void UpdatePower(int16_t house);
void DrawStructureTiles(uint16_t tiles, uint16_t a, uint16_t cell, uint16_t layout, uint16_t house);
void RevealStructure(uint32_t s);
void RemoveCellEffect(uint16_t c);
void DrawEffect(uint32_t n);
void UpdateEffects();
void UpdateCellEffects();
uint16_t HasUpgrade(uint32_t s);
uint16_t StructureBuild(uint32_t s, int16_t item);
uint32_t BuildableMask(uint32_t s);
uint16_t UpgradeItem(uint32_t s);
void CancelBuild(uint32_t s);
void AiStructureSetup(uint32_t s);
void NumberStructures(int16_t house);
uint16_t PosCell(uint32_t pos);
void OccupyCells(uint32_t s);
void SetStructureState(uint32_t s, uint16_t state);
void SetUnitSpeed(uint32_t u, uint16_t speed);
void UpdateUnitSprite(uint32_t u);
uint16_t TerrainAt(uint16_t cell);
void SetSpriteClass(uint16_t n, int16_t cls, uint16_t flip, uint16_t house);
void SetTankFrame(uint32_t table, uint16_t n, uint16_t body, uint16_t turret, uint16_t house);
void SetDirFrame(uint16_t n, uint16_t facing, int16_t layer);
void SetFrame4(uint16_t n, uint16_t i, uint16_t flip);
void UploadFrame4(uint16_t n, uint16_t i, uint16_t flip);
uint32_t CreateStructure(uint16_t index, uint16_t type, uint16_t house, uint16_t cell);
uint32_t StructureGet(uint16_t i);
uint32_t UnitGet(uint16_t i);
uint16_t RedrawCell(uint16_t cell);
void RunMission();
uint16_t MakeTarget(uint16_t index, uint16_t kind);
uint16_t ObjectTarget(uint32_t o);
uint16_t SnapToUnit(uint16_t cell);
uint16_t HandleInput();
uint16_t SideMenu();
void StructureMenu();
void StarportMenu(uint32_t s);
int16_t BuildMenu();
void UsePlacementCursor();
void LoadGameTiles();
void ShowIcon(int16_t cls);
void UseFreeCursor();
uint16_t LaunchSpecial(uint32_t s);
uint16_t OptionsScreen(uint16_t ingame);
uint16_t PasswordScreen();
void OrderSpecial(uint16_t cell);
void UnitMoveTo(uint32_t u, uint16_t target);
void UnitAttack(uint32_t u, uint16_t target);
void StructurePlaced();
uint32_t DeliverUnit(uint16_t house, uint16_t type, uint16_t dest);
uint16_t UpdateRadarStatus(uint16_t house);
void UpdateStorage(uint16_t house);
uint32_t OwnedTypes(uint16_t house);
void UpdateWorld(uint32_t a);
void UpdatePanel();
void HideIcon();
void HideProgressBar();
void HidePanelIcon();
void HideBuildMarker();
void ShowBuildMarker();
void ShowCursorShadow();
uint32_t FirstUnit(uint32_t it, uint16_t house, uint16_t type);
uint32_t NextUnit(uint32_t it);
uint32_t FirstStructure(uint32_t it, uint16_t house, uint16_t type);
uint32_t NextStructure(uint32_t it);
uint32_t FirstTeam(uint32_t it, uint16_t house);
uint32_t NextTeam(uint32_t it);
void TeamTick();
void HouseTick();
uint16_t PickCell(uint16_t mode, uint16_t house);
uint16_t HasUnit(uint16_t house, uint16_t type);
bool OnScreen(uint32_t pos);
uint16_t EnsureHarvester(uint16_t house);
void ApplyPower(uint16_t house);
void StructureTick();
void StructureUpdate();
uint16_t DamageStructure(uint32_t s, uint16_t damage, uint16_t a);
void StructureDestroyed(uint32_t s);
void ForgetStructure(uint32_t s);
uint32_t BuiltStructure(uint32_t s);
uint16_t AiChooseBuild(uint32_t s);
void ClearMark(uint32_t s, uint8_t bit);
void SetReadyMark(uint32_t s, uint16_t k);
void SetPowerMark(uint32_t s);
void SetRepairMark(uint32_t s, uint16_t k);
uint32_t FirstListedStructure(uint16_t enemy);
uint32_t NextListedStructure();
uint32_t FirstListedUnit(uint16_t enemy);
uint32_t NextListedUnit();
void ClearMarks(uint32_t s);
uint16_t FreeCellBeside(uint32_t s, uint16_t harvester);
void StructureLost(uint32_t s);
uint16_t CheckGameEnd();
void WinScreen();
void EndSceneHook();
void WinBriefing();
void LoseScreen();
void LoseBriefing();
void WinMap(uint16_t house, uint16_t scen);
void LoseMap(uint16_t house, uint16_t scen);
void ScoreScreen(uint16_t scen, uint16_t score, uint32_t time, uint16_t a, uint16_t b, uint16_t kills, uint16_t losses,
                 uint16_t c, uint16_t d, uint16_t won);
void Finale(uint16_t house);
void AttractDemo(uint32_t);
void UpdateHouse(uint16_t house, uint16_t a);
void SetCursorAt(uint16_t cell);

// --- Front end (frontend.cpp) ---
void FrontEnd();

// --- Game main (cmain.cpp) ---
void CMain();
void GameTick();

// --- Boot and main (boot.cpp) ---
[[noreturn]] void Reset();

} // namespace game
