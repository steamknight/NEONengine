#define ACE_TEST_RUNNER
#ifdef ACE_TEST_RUNNER

#include "neonengine.h"

#include <ace/managers/system.h>
#include <ace/utils/font.h>

#include <ace++/log.h>

#include "core/screen.h"
#include "tests/bstr_view_tests.h"
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
#include "tests/noir_vm_logic_tests.h"
#include "tests/noir_vm_reentry_tests.h"
#include "tests/noir_vm_scene_tests.h"
#include "tests/noir_vm_tests.h"
#include "tests/noir_vm_timeline_tests.h"
#include "tests/noir_vm_value_ops_tests.h"

namespace NEONengine::tests
{
    void runTestSuite(char const* szSuiteName, Test tests[], uint32_t testCount);

#define RUN_SUITE(suite) \
    runTestSuite(suite##_name, suite##_tests, sizeof(suite##_tests) / sizeof(Test));

    void testRunnerCreate()
    {
        ACE_LOG_BLOCK("testRunner");

        screenFadeFromBlack(g_mainScreen, 25, 0, NULL);
        screenClear(g_mainScreen, 0);

        RUN_SUITE(bstr_view);
        RUN_SUITE(mtl_array);
        RUN_SUITE(mtl_list);
        RUN_SUITE(mtl_vector);
        RUN_SUITE(mtl_expected);
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
        RUN_SUITE(noir_vm_reentry);
    }

    void runTestSuite(char const* szSuiteName, Test tests[], uint32_t testCount)
    {
        static uint32_t y  = 0;
        auto pTextRenderer = g_pEngine->default_text_renderer();

        char buffer[256];
        snprintf(buffer, sizeof(buffer), "Test: %s", szSuiteName);
        auto text = pTextRenderer->create_text(szSuiteName, 320, text_justify::LEFT);

        screenTextCopy(g_mainScreen, text.get(), 0, y, 24, FONT_COOKIE);

        uint32_t passedTests = 0;
        for (uint32_t i = 0; i < testCount; i++)
        {
            char const* szError = tests[i].test_fn();
            if (szError)
            {
                y += text->uwActualHeight;
                snprintf(buffer, sizeof(buffer), "%s: %s", tests[i].name, szError);
                text = pTextRenderer->create_text((char const*)buffer, 320, text_justify::RIGHT);
                screenTextCopy(g_mainScreen, text.get(), 0, y, 27, FONT_COOKIE);
                y += text->uwActualHeight;

                // return;
            }
            else
            {
                ++passedTests;
            }
        }

        snprintf(buffer, sizeof(buffer), "Passed %ld/%ld tests.", passedTests, testCount);
        text = pTextRenderer->create_text((char const*)buffer, 320, text_justify::RIGHT);
        screenTextCopy(g_mainScreen, text.get(), 0, y, 2, FONT_COOKIE);
        y += text->uwActualHeight;
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