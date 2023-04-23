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
korl_global_const f32 SNES_SECONDS_PER_FRAME = 60.0988062658451f;// derived from: http://nerdlypleasures.blogspot.com/2017/01/classic-systems-true-framerate.html
korl_global_const u8  SUPER_JUMP_FRAMES[]    = {61, 31, 27, 23, 18, 15, 12, 9, 7, 7, 6, 5, 4, 3};// derived from a pidgezero_one youtube video: https://www.youtube.com/watch?v=uSCIK5-EU8A
korl_global_const f32 SECONDS_PER_JUMP[]     = {1.3333f, 0.75f};// obtained experimentally
enum InputFlags
    {INPUT_FLAG_JUMP
};
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
} Memory;
korl_global_variable Memory* memory;
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
    korl_gui_setFontAsset(L"submodules/korl/test-assets/source-sans/SourceSans3-Semibold.otf");// KORL-ISSUE-000-000-086: gfx: default font path doesn't work, since this subdirectly is unlikely in the game project
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
    memory->windowSize = {KORL_C_CAST(f32, windowSizeX), KORL_C_CAST(f32, windowSizeY)};
    korl_logConsole_update(&memory->logConsole, deltaSeconds, korl_log_getBuffer, {windowSizeX, windowSizeY}, memory->allocatorStack);
    if(     memory->input.current  & (1 << INPUT_FLAG_JUMP)
       && !(memory->input.previous & (1 << INPUT_FLAG_JUMP)))
    {
        if(memory->jumping)
        {
        }
        else/* if we're not jumping, start a new jump */
        {
            memory->jumping            = true;
            memory->jumpInputSeconds   = -1;
            memory->currentJump        = 0;
            memory->currentJumpSeconds = 0;
        }
    }
    korl_gfx_useCamera(korl_gfx_createCameraOrtho(1));
    if(memory->jumping)
    {
        /* jump simulation logic */
        const f32 jumpSeconds = SECONDS_PER_JUMP[KORL_MATH_CLAMP(memory->currentJump, 0, korl_arraySize(SECONDS_PER_JUMP) - 1)];
        if(memory->currentJumpSeconds >= jumpSeconds)
        {
            memory->jumping = false;
        }
        /* draw the scene */
        const f32 jumpAnimationRatio = memory->currentJumpSeconds < (jumpSeconds/2) 
                                       ?         memory->currentJumpSeconds                    / (jumpSeconds/2)
                                       : 1.f - ((memory->currentJumpSeconds - (jumpSeconds/2)) / (jumpSeconds/2));
        const f32 jumpPositionY = -memory->windowSize.y/2 + jumpAnimationRatio * memory->windowSize.y;
        Korl_Gfx_Batch*const batchLine = korl_gfx_createBatchLines(memory->allocatorStack, 1);
        korl_gfx_batchSetLine(batchLine, 0, Korl_Math_V2f32{-memory->windowSize.x/2, jumpPositionY}.elements, Korl_Math_V2f32{memory->windowSize.x/2, jumpPositionY}.elements, 2, KORL_COLOR4U8_WHITE);
        korl_gfx_batch(batchLine, KORL_GFX_BATCH_FLAG_DISABLE_DEPTH_TEST);
        memory->currentJumpSeconds += deltaSeconds;
    }
    #if 0
    Korl_Gfx_Camera camera = korl_gfx_createCameraFov(90, 50, 1e16f, KORL_MATH_V3F32_ONE * 100, KORL_MATH_V3F32_ZERO);
    korl_gfx_useCamera(camera);
    Korl_Gfx_Drawable scene3d;
    korl_gfx_drawable_scene3d_initialize(&scene3d, korl_resource_fromFile(KORL_RAW_CONST_UTF16(L"data/cube.glb"), KORL_ASSETCACHE_GET_FLAG_LAZY));
    scene3d._model.scale = KORL_MATH_V3F32_ONE * 50;
    korl_gfx_draw(&scene3d);
    Korl_Gfx_Batch* batchAxis = korl_gfx_createBatchAxisLines(memory->allocatorStack);
    korl_gfx_batchSetScale(batchAxis, KORL_MATH_V3F32_ONE * 100);
    korl_gfx_batch(batchAxis, KORL_GFX_BATCH_FLAGS_NONE);
    #endif
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
