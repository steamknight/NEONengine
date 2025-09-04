#ifdef ACE_TEST_RUNNER

#include "neonengine.h"

#include <ace/managers/system.h>
#include <ace/utils/font.h>
#include <ace/utils/palette.h>

#include "core/screen.h"
#include "core/text.h"

#include "tests/array_tests.h"
#include "tests/bstring_tests.h"
#include "tests/lang_tests.h"

namespace NEONengine::tests
{
    void runTestSuite(char const* szSuiteName, Test tests[], ULONG ulTestCount);

    #define RUN_SUITE(suite) \
        runTestSuite(suite ## _name, suite ##_tests, sizeof(suite ## _tests) / sizeof(Test));

    void testRunnerCreate(void)
    {
        logBlockBegin("testRunner");

        RUN_SUITE(array);
        RUN_SUITE(bstring);
        RUN_SUITE(lang);

        logBlockEnd("testRunner");
    }

    void runTestSuite(char const* szSuiteName, Test tests[], ULONG ulTestCount)
    {
        logWrite("Running Test Suite: %s\n", szSuiteName);
        ULONG ulPassedTests = 0;
        for (ULONG i = 0; i < ulTestCount; i++)
        {
            char const* szError = tests[i].test_fn();
            if (szError)
            {
                logWrite("%s: %s - %s", "FAIL", tests[i].name, szError);
            }
            else
            {
                logWrite("%s: %s", "PASS", tests[i].name);
                ++ulPassedTests;
            }
        }

        logWrite("Passed %ld/%ld tests.", ulPassedTests, ulTestCount);
    }
    void testRunnerProcess(void)
    {
        statePop(g_gameStateManager);
    }

    tState g_stateTestRunner = {
        .cbCreate = testRunnerCreate,
        .cbLoop = testRunnerProcess,
        .cbDestroy = NULL,
    };
    }

#endif