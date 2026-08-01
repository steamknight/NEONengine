#ifdef ACE_TEST_RUNNER

#include "types.h"

#include "neonengine.h"

#include <ace/managers/system.h>
#include <ace/utils/font.h>

#include <ace++/log.h>

#include "core/logger.h"
#include "core/screen.h"
#include "tests/bstr_view_tests.h"
#include "tests/game_data_tests.h"
#include "tests/mtl_array_tests.h"
#include "tests/mtl_expected_tests.h"
#include "tests/mtl_list_tests.h"
#include "tests/mtl_vector_tests.h"
#include "tests/noir_vm_arithmetic_tests.h"
#include "tests/noir_vm_control_flow_tests.h"
#include "tests/noir_vm_data_move_tests.h"
#include "tests/noir_vm_dialogue_tests.h"
#include "tests/noir_vm_flag_ops_tests.h"
#include "tests/noir_vm_inventory_tests.h"
#include "tests/noir_vm_menu_tests.h"
#include "tests/noir_vm_debug_tests.h"
#include "tests/noir_vm_deferred_tests.h"
#include "tests/noir_vm_logic_tests.h"
#include "tests/noir_vm_scene_tests.h"
#include "tests/noir_vm_scheduler_tests.h"
#include "tests/noir_vm_tests.h"
#include "tests/noir_vm_timeline_tests.h"
#include "tests/noir_vm_value_ops_tests.h"
#include "tests/track_clock_tests.h"

NE_LOG_DOMAIN(Test);

namespace NEONengine::tests
{
    bool runTestSuite(char const* szSuiteName, Test tests[], u32 testCount);

#define RUN_SUITE(suite)                                                                       \
    do                                                                                         \
    {                                                                                          \
        ++suiteCount;                                                                          \
        if (runTestSuite(suite##_name, suite##_tests, sizeof(suite##_tests) / sizeof(Test)))    \
            ++suitesPassed;                                                                     \
    } while (0)

    namespace
    {
        /// Next free scanline for report text.
        u16 s_y = 0;

        // The hardware pointer owns palette entries 24..31, so report text has
        // to come out of the text range - and set its own colours, since what
        // the UI palette holds there is whatever the game happens to need.
        constexpr u8 COLOR_HEADING = 20;
        constexpr u8 COLOR_FAILURE = 21;
        constexpr u8 COLOR_PASSED  = 22;
        constexpr u8 COLOR_FAILED  = 23;

        void set_report_palette()
        {
            UWORD* palette         = screenGetPalette(g_mainScreen);
            palette[COLOR_HEADING] = 0x0FFF;  // white
            palette[COLOR_FAILURE] = 0x0F44;  // red
            palette[COLOR_PASSED]  = 0x04E4;  // green
            palette[COLOR_FAILED]  = 0x0FD2;  // amber
        }

        /**
         * @brief Draws one line of report text, if there is room above the footer.
         *
         * A failing pass can name more tests than the screen has lines. The log
         * has them all; the screen keeps the first that fit and reserves the
         * last line for the summary.
         */
        void draw_report_line(char const* str, u8 color)
        {
            auto text = g_pEngine->default_text_renderer()->create_text(str, 320, text_justify::LEFT);
            u16 const height = text->uwActualHeight;

            if (s_y + 2 * height > SCREEN_HEIGHT) return;

            screenTextCopy(g_mainScreen, text.get(), 0, s_y, color, FONT_COOKIE);
            s_y = to<u16>(s_y + height);
        }

        /// Draws the summary on the bottom line, whatever came before it.
        void draw_summary(char const* str, u8 color)
        {
            auto text = g_pEngine->default_text_renderer()->create_text(str, 320, text_justify::LEFT);
            screenTextCopy(g_mainScreen,
                text.get(),
                0,
                to<u16>(SCREEN_HEIGHT - text->uwActualHeight),
                color,
                FONT_COOKIE);
        }
    }  // namespace

    void testRunnerCreate()
    {
        ACE_LOG_BLOCK("testRunner");

        set_report_palette();
        screenFadeFromBlack(g_mainScreen, 25, 0, NULL);
        screenClear(g_mainScreen, 0);

        s_y              = 0;
        u32 suiteCount   = 0;
        u32 suitesPassed = 0;

        draw_report_line("Starting test pass...", COLOR_HEADING);

        RUN_SUITE(bstr_view);
        RUN_SUITE(mtl_array);
        RUN_SUITE(mtl_list);
        RUN_SUITE(mtl_vector);
        RUN_SUITE(mtl_expected);
        RUN_SUITE(game_data);
        RUN_SUITE(track_clock);
        RUN_SUITE(noir_vm);
        RUN_SUITE(noir_vm_data_move);
        RUN_SUITE(noir_vm_arithmetic);
        RUN_SUITE(noir_vm_control_flow);
        RUN_SUITE(noir_vm_logic);
        RUN_SUITE(noir_vm_flag_ops);
        RUN_SUITE(noir_vm_value_ops);
        RUN_SUITE(noir_vm_inventory);
        RUN_SUITE(noir_vm_timeline);
        RUN_SUITE(noir_vm_scene);
        RUN_SUITE(noir_vm_dialogue);
        RUN_SUITE(noir_vm_menu);
        RUN_SUITE(noir_vm_debug);
        RUN_SUITE(noir_vm_scheduler);
        RUN_SUITE(noir_vm_deferred);

        char summary[64];
        snprintf(summary,
            sizeof(summary),
            "%lu/%lu test suites passed.",
            to<ULONG>(suitesPassed),
            to<ULONG>(suiteCount));
        NE_LOG("%lu/%lu test suites passed", to<ULONG>(suitesPassed), to<ULONG>(suiteCount));
        draw_summary(summary, suitesPassed == suiteCount ? COLOR_PASSED : COLOR_FAILED);

        screenDuplicateBackToFront(g_mainScreen);
    }

    bool runTestSuite(char const* szSuiteName, Test tests[], u32 testCount)
    {
        char buffer[256];
        u32 passedTests = 0;

        for (u32 i = 0; i < testCount; i++)
        {
            char const* szError = tests[i].test_fn();
            if (!szError)
            {
                ++passedTests;
                continue;
            }

            NE_LOG("FAIL %s.%s: %s", szSuiteName, tests[i].name, szError);
            snprintf(buffer, sizeof(buffer), "%s.%s: %s", szSuiteName, tests[i].name, szError);
            draw_report_line(buffer, COLOR_FAILURE);
        }

        NE_LOG("suite %s: %lu/%lu passed",
            szSuiteName,
            to<ULONG>(passedTests),
            to<ULONG>(testCount));

        return passedTests == testCount;
    }

    void testRunnerProcess()
    {
        statePop(g_gameStateManager);
    }

    tState g_stateTestRunner = {
        .cbCreate  = testRunnerCreate,
        .cbLoop    = testRunnerProcess,
        .cbDestroy = NULL,
    };
}  // namespace NEONengine::tests

#endif
