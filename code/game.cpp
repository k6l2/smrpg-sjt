#include <stdlib.h>// needed for __FILEW__, etc...
#include "korl-interface-game.h"
#define _KORL_PLATFORM_API_MACRO_OPERATION(x) fnSig_##x *x;
    #include "korl-interface-platform-api.h"
    //KORL-ISSUE-000-000-120: interface-platform: remove KORL_DEFINED_INTERFACE_PLATFORM_API; this it just messy imo; if there is clear separation of code that should only exist in the platform layer, then we should probably just physically separate it out into separate source file(s)
    #define KORL_DEFINED_INTERFACE_PLATFORM_API// prevent certain KORL modules which define symbols that are required by the game module, but whose codes are confined to the platform layer, from re-defining them since we just declared the API
#undef _KORL_PLATFORM_API_MACRO_OPERATION
korl_internal void _game_getInterfacePlatformApi(KorlPlatformApi korlApi)
{
    #define _KORL_PLATFORM_API_MACRO_OPERATION(x) (x) = korlApi.x;
    #include "korl-interface-platform-api.h"
    #undef _KORL_PLATFORM_API_MACRO_OPERATION
}
#include "korl-string.h"
#include "korl-stringPool.h"
#include "korl-logConsole.h"
#include "korl-stb-ds.h"
#include "korl-checkCast.h"
korl_global_const u8      MAX_SUPER_JUMPS        = 100;// this is the goal, after all
korl_global_const f32     SNES_SECONDS_PER_FRAME = 1.f / 60.0988062658451f;// derived from: http://nerdlypleasures.blogspot.com/2017/01/classic-systems-true-framerate.html
korl_global_const u8      SUPER_JUMP_FRAMES[]    = {61, 31, 27, 23, 18, 15, 12, 9, 7, 7, 6, 5, 4, 3};// derived from a pidgezero_one youtube video: https://www.youtube.com/watch?v=uSCIK5-EU8A
korl_global_const f32     SECONDS_PER_JUMP[]     = {1.3333f, 0.75f};// obtained experimentally
korl_global_const wchar_t DEFAULT_FONT[]         = L"submodules/korl/test-assets/source-sans/SourceSans3-Semibold.otf";
enum InputFlags
    {INPUT_FLAG_JUMP
};
typedef struct HudLogLine
{
    Korl_StringPool_String text;
    f32                    seconds;// how long has the text been alive for
} HudLogLine;
typedef struct Memory
{
    Korl_Memory_AllocatorHandle allocatorHeap;
    Korl_Memory_AllocatorHandle allocatorStack;
    bool                        quit;
    Korl_StringPool             stringPool;// used by logConsole
    Korl_LogConsole             logConsole;
    struct
    {
        u32 previous;
        u32 current;
    } input;
    bool jumping;
    f32  jumpInputSeconds;// negative value indicates the user has not input a jump for the current jump
    u32  currentJump;
    f32  currentJumpSeconds;
    Korl_Math_V2f32 windowSize;
    bool drawSuperJumpMinimumThreshold;
    bool hudLogJumpInputs;
    HudLogLine* stbDaHudLog;
    f32 deltaSeconds;
} Memory;
korl_global_variable Memory* memory;
korl_internal void hudLog_add(Korl_StringPool_String string, bool onlyLog)
{
    korl_log(INFO, "%ws", korl_stringPool_getRawUtf16(&string));
    if(onlyLog)
        return;
    KORL_ZERO_STACK(HudLogLine, newLine);
    newLine.text = string;
    mcarrpush(KORL_STB_DS_MC_CAST(memory->allocatorHeap), memory->stbDaHudLog, newLine);
}
korl_internal void hudLog_remove(u$ index)
{
    HudLogLine*const hudLogLine = memory->stbDaHudLog + index;
    korl_stringPool_free(&hudLogLine->text);
    arrdel(memory->stbDaHudLog, index);
}
korl_internal void hudLog_step(void)
{
    korl_shared_const f32 HUD_LOG_LINE_KEYFRAME_SECONDS[] = {3, 2};
    enum HudLogLine_Keyframe
        {HUD_LOG_LINE_KEYFRAME_DISPLAY
        ,HUD_LOG_LINE_KEYFRAME_FADE
    };
    const Korl_Math_V2f32  hudLogOrigin = memory->windowSize * Korl_Math_V2f32{-0.5f, -0.5f};
    const HudLogLine*const hudLogEnd    = memory->stbDaHudLog + arrlen(memory->stbDaHudLog);
    f32 cursorY = 0;
    for(HudLogLine* hudLogLine = KORL_C_CAST(HudLogLine*, hudLogEnd) - 1; memory->stbDaHudLog && hudLogLine >= memory->stbDaHudLog; hudLogLine--)
    {
        Korl_Vulkan_Color4u8 color = KORL_COLOR4U8_WHITE;
        f32 currentTotalKeyFrameSeconds = 0;
        for(u$ i = 0; i < korl_arraySize(HUD_LOG_LINE_KEYFRAME_SECONDS); i++)
        {
            const f32 previousTotalKeyFrameSeconds = currentTotalKeyFrameSeconds;
            currentTotalKeyFrameSeconds += HUD_LOG_LINE_KEYFRAME_SECONDS[i];
            if(hudLogLine->seconds < currentTotalKeyFrameSeconds)
            {
                const f32 currentKeyFrameSeconds = hudLogLine->seconds - previousTotalKeyFrameSeconds;
                const f32 currentKeyFrameRatio   = currentKeyFrameSeconds / HUD_LOG_LINE_KEYFRAME_SECONDS[i];
                switch(i)
                {
                case HUD_LOG_LINE_KEYFRAME_DISPLAY:{break;}
                case HUD_LOG_LINE_KEYFRAME_FADE:{
                    color.a = korl_math_round_f32_to_u8((1.f - currentKeyFrameRatio) * KORL_U8_MAX);
                    break;}
                }
                i = korl_arraySize(HUD_LOG_LINE_KEYFRAME_SECONDS);
            }
        }
        if(hudLogLine->seconds >= currentTotalKeyFrameSeconds)
        {
            hudLog_remove(korl_checkCast_i$_to_u$(hudLogLine - memory->stbDaHudLog));
            continue;
        }
        Korl_Gfx_Batch*const batch = korl_gfx_createBatchText(memory->allocatorStack, DEFAULT_FONT, korl_stringPool_getRawUtf16(&hudLogLine->text), 24, color, 0.f, KORL_COLOR4U8_TRANSPARENT);
        batch->modelColor = color;// HACK: korl-gfx needs a _lot_ of work...
        korl_gfx_batchTextSetPositionAnchor(batch, KORL_MATH_V2F32_ZERO);
        korl_gfx_batchSetPosition2dV2f32(batch, hudLogOrigin + Korl_Math_V2f32{0, cursorY});
        korl_gfx_batch(batch, KORL_GFX_BATCH_FLAG_DISABLE_DEPTH_TEST);
        hudLogLine->seconds += memory->deltaSeconds;
        cursorY += 30;
    }
}
KORL_EXPORT KORL_FUNCTION_korl_command_callback(command_displayThreshold)
{
    memory->drawSuperJumpMinimumThreshold = !memory->drawSuperJumpMinimumThreshold;
    korl_log(INFO, "drawSuperJumpMinimumThreshold = %hs", memory->drawSuperJumpMinimumThreshold ? "true" : "false");
}
KORL_EXPORT KORL_FUNCTION_korl_command_callback(command_hudLogInputs)
{
    memory->hudLogJumpInputs = !memory->hudLogJumpInputs;
    korl_log(INFO, "hudLogJumpInputs = %hs", memory->hudLogJumpInputs ? "true" : "false");
}
KORL_EXPORT KORL_GAME_INITIALIZE(korl_game_initialize)
{
    _game_getInterfacePlatformApi(korlApi);
    KORL_ZERO_STACK(Korl_Heap_CreateInfo, heapCreateInfo);
    heapCreateInfo.initialHeapBytes = korl_math_megabytes(8);
    const Korl_Memory_AllocatorHandle allocatorHeap = korl_memory_allocator_create(KORL_MEMORY_ALLOCATOR_TYPE_LINEAR, L"game", KORL_MEMORY_ALLOCATOR_FLAG_SERIALIZE_SAVE_STATE, &heapCreateInfo);
    memory = KORL_C_CAST(Memory*, korl_allocate(allocatorHeap, sizeof(Memory)));
    memory->allocatorHeap  = allocatorHeap;
    memory->allocatorStack = korl_memory_allocator_create(KORL_MEMORY_ALLOCATOR_TYPE_LINEAR, L"game-stack", KORL_MEMORY_ALLOCATOR_FLAG_EMPTY_EVERY_FRAME, &heapCreateInfo);
    memory->stringPool     = korl_stringPool_create(allocatorHeap);
    memory->logConsole     = korl_logConsole_create(&memory->stringPool);
    mcarrsetcap(KORL_STB_DS_MC_CAST(memory->allocatorHeap), memory->stbDaHudLog, 32);
    korl_gui_setFontAsset(DEFAULT_FONT);// KORL-ISSUE-000-000-086: gfx: default font path doesn't work, since this subdirectly is unlikely in the game project
    korl_command_register(KORL_RAW_CONST_UTF8("display-threshold"), command_displayThreshold);
    korl_command_register(KORL_RAW_CONST_UTF8("hud-log-inputs"), command_hudLogInputs);
    return memory;
}
KORL_EXPORT KORL_GAME_ON_RELOAD(korl_game_onReload)
{
    _game_getInterfacePlatformApi(korlApi);
    memory = KORL_C_CAST(Memory*, context);
}
KORL_EXPORT KORL_GAME_ON_KEYBOARD_EVENT(korl_game_onKeyboardEvent)
{
    if(isDown && !isRepeat)
        switch(keyCode)
        {
        case KORL_KEY_SPACE:{memory->input.current |= 1 << INPUT_FLAG_JUMP; break;}
        case KORL_KEY_ESCAPE:{memory->quit = true; break;}
        case KORL_KEY_GRAVE:{korl_logConsole_toggle(&memory->logConsole); break;}
        default: break;
        }
    else if(!isDown)
        switch(keyCode)
        {
        case KORL_KEY_SPACE:{memory->input.current &= ~(1 << INPUT_FLAG_JUMP); break;}
        default: break;
        }
}
KORL_EXPORT KORL_GAME_UPDATE(korl_game_update)
{
    memory->deltaSeconds = deltaSeconds;
    memory->windowSize   = {KORL_C_CAST(f32, windowSizeX), KORL_C_CAST(f32, windowSizeY)};
    korl_logConsole_update(&memory->logConsole, deltaSeconds, korl_log_getBuffer, {windowSizeX, windowSizeY}, memory->allocatorStack);
    /* calculate the time duration the user is required to press the jump 
        button during the current jump in order to sustain the super jump */
    const f32 jumpSeconds              = SECONDS_PER_JUMP [KORL_MATH_CLAMP(memory->currentJump, 0, korl_arraySize(SECONDS_PER_JUMP)  - 1)];
    const u8  superJumpFrames          = SUPER_JUMP_FRAMES[KORL_MATH_CLAMP(memory->currentJump, 0, korl_arraySize(SUPER_JUMP_FRAMES) - 1)];
    const f32 superJumpInputMaxSeconds = SNES_SECONDS_PER_FRAME * superJumpFrames;
    /**/
    if(     memory->input.current  & (1 << INPUT_FLAG_JUMP)
       && !(memory->input.previous & (1 << INPUT_FLAG_JUMP)))// if the user pressed the JUMP input on _this_ frame
    {
        if(memory->jumping)/* if we're jumping, we need to accept the next super jump input */
        {
            if(memory->jumpInputSeconds < 0)/* only allow a jump input if the user hasn't provided one for the current jump */
            {
                memory->jumpInputSeconds = memory->currentJumpSeconds;
                const f32 secondsFromSuperJumpThreshold = memory->currentJumpSeconds - (jumpSeconds - superJumpInputMaxSeconds);// negative => we were too early, positive => we were successful
                // korl_log(INFO, "jumpInputSeconds = %f", memory->jumpInputSeconds);
                // korl_log(INFO, "secondsFromSuperJumpThreshold = %f (%i SNES frames)", secondsFromSuperJumpThreshold, korl_math_round_f32_to_i32(secondsFromSuperJumpThreshold / SNES_SECONDS_PER_FRAME));
                hudLog_add(korl_stringNewFormatUtf16(&memory->stringPool, L"secondsFromSuperJumpThreshold = %f (%i SNES frames)"
                                                    ,secondsFromSuperJumpThreshold
                                                    ,korl_math_round_f32_to_i32(secondsFromSuperJumpThreshold / SNES_SECONDS_PER_FRAME))
                          ,!memory->hudLogJumpInputs);
            }
        }
        else/* if we're not jumping, start a new jump */
        {
            memory->jumping            = true;
            memory->jumpInputSeconds   = KORL_F32_MIN;
            memory->currentJump        = 0;
            memory->currentJumpSeconds = 0;
        }
    }
    korl_gfx_useCamera(korl_gfx_createCameraOrtho(1));
    if(memory->jumping)
    {
        // korl_log(VERBOSE, "currentJumpSeconds = %f", memory->currentJumpSeconds);
        /* jump simulation logic */
        if(memory->currentJumpSeconds >= jumpSeconds)
        {
            // korl_log(INFO, "(%f - %f) == %f", jumpSeconds, superJumpInputMaxSeconds, jumpSeconds - superJumpInputMaxSeconds);
            /* if the user pressed the jump button at the right time, we can 
                advance to the next jump of the super jump */
            if(   memory->jumpInputSeconds >= 0 // the user has actually pressed the jump input
               && (   memory->jumpInputSeconds >= jumpSeconds - superJumpInputMaxSeconds 
                   || korl_math_isNearlyEqualEpsilon(memory->jumpInputSeconds, jumpSeconds - superJumpInputMaxSeconds, 1e-3f)) // user pressed the button within the frame window
               && memory->currentJump < MAX_SUPER_JUMPS + 1/* don't count the first jump, like in SMRPG */)// limit the total # of super jumps to some maximum
            {
                memory->currentJumpSeconds -= jumpSeconds;
                memory->jumpInputSeconds    = KORL_F32_MIN;
                memory->currentJump++;
            }
            else
            {
                memory->jumping = false;
                korl_log(INFO, "super jump complete; super jumps = %u", memory->currentJump > 0 ? memory->currentJump - 1 : 0);
            }
        }
        /* draw the scene */
        const f32 jumpAnimationRatio = memory->currentJumpSeconds < (jumpSeconds / 2) 
                                       ?         memory->currentJumpSeconds                      / (jumpSeconds / 2)
                                       : 1.f - ((memory->currentJumpSeconds - (jumpSeconds / 2)) / (jumpSeconds / 2));
        const f32 jumpPositionY = -memory->windowSize.y / 2 + jumpAnimationRatio * memory->windowSize.y;
        {/* draw the player */
            Korl_Gfx_Batch*const batchLine = korl_gfx_createBatchLines(memory->allocatorStack, 1);
            korl_gfx_batchSetLine(batchLine, 0, Korl_Math_V2f32{-memory->windowSize.x / 2, jumpPositionY}.elements, Korl_Math_V2f32{memory->windowSize.x / 2, jumpPositionY}.elements, 2, KORL_COLOR4U8_WHITE);
            korl_gfx_batch(batchLine, KORL_GFX_BATCH_FLAG_DISABLE_DEPTH_TEST);
        }
        if(memory->drawSuperJumpMinimumThreshold)
        {/* draw the visual indication of where the player needs to press the next jump input (below this line) */
            const f32 superJumpVisualRatio = KORL_MATH_CLAMP(superJumpInputMaxSeconds / (jumpSeconds / 2), 0, 1);// maps [0,1] => [bottom-of-jump, top-of-jump]
            const f32 y                    = -memory->windowSize.y / 2 + superJumpVisualRatio * memory->windowSize.y;
            Korl_Gfx_Batch*const batchLine = korl_gfx_createBatchLines(memory->allocatorStack, 1);
            korl_gfx_batchSetLine(batchLine, 0, Korl_Math_V2f32{-memory->windowSize.x / 2, y}.elements, Korl_Math_V2f32{memory->windowSize.x / 2, y}.elements, 2, KORL_COLOR4U8_GREEN);
            korl_gfx_batch(batchLine, KORL_GFX_BATCH_FLAG_DISABLE_DEPTH_TEST);
        }
        /* advance timers */
        memory->currentJumpSeconds += deltaSeconds;
    }
    else
    {
        {/* draw instructions */
            Korl_Gfx_Batch*const batch = korl_gfx_createBatchText(memory->allocatorStack, DEFAULT_FONT, L"Type [SPACE] key to begin super jump.", 24, KORL_COLOR4U8_WHITE, 0.f, KORL_COLOR4U8_TRANSPARENT);
            korl_gfx_batchTextSetPositionAnchor(batch, KORL_MATH_V2F32_ONE * 0.5f);
            korl_gfx_batchSetPosition2dV2f32(batch, KORL_MATH_V2F32_ZERO);
            korl_gfx_batch(batch, KORL_GFX_BATCH_FLAG_DISABLE_DEPTH_TEST);
        }
    }
    hudLog_step();
    memory->input.previous = memory->input.current;
    return !memory->quit;
}
#include "korl-math.c"
#include "korl-checkCast.c"
#include "korl-string.c"
#include "korl-stringPool.c"
#include "korl-logConsole.c"
#define STBDS_UNIT_TESTS // for the sake of detecting any other C++ warnings; we aren't going to actually run any of these tests
#include "korl-stb-ds.c"
