// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#include "precomp.h"
#include "WexTestClass.h"

#include "CommonState.hpp"

#include "globals.h"
#include "srvinit.h"
#include "output.h"
#include "dbcs.h"

#include "../interactivity/inc/ServiceLocator.hpp"

using namespace WEX::Logging;
using namespace WEX::TestExecution;

class InitTests
{
    TEST_CLASS(InitTests);

    // https://msdn.microsoft.com/en-us/library/windows/desktop/dd317756(v=vs.85).aspx
    static const UINT s_uiOEMJapaneseCP = 932;
    static const UINT s_uiOEMSimplifiedChineseCP = 936;
    static const UINT s_uiOEMKoreanCP = 949;
    static const UINT s_uiOEMTraditionalChineseCP = 950;

    static const LANGID s_langIdJapanese = MAKELANGID(LANG_JAPANESE, SUBLANG_DEFAULT);
    static const LANGID s_langIdSimplifiedChinese = MAKELANGID(LANG_CHINESE, SUBLANG_CHINESE_SIMPLIFIED);
    static const LANGID s_langIdKorean = MAKELANGID(LANG_KOREAN, SUBLANG_KOREAN);
    static const LANGID s_langIdTraditionalChinese = MAKELANGID(LANG_CHINESE, SUBLANG_CHINESE_TRADITIONAL);
    static const LANGID s_langIdEnglish = MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US);

    // This test exists to ensure the continued behavior of the code in the Windows loader.
    // See the LOAD BEARING CODE comment inside GetConsoleLangId or the investigation results in MSFT: 9808579 for more detail.
    TEST_METHOD(TestGetConsoleLangId)
    {
        using Microsoft::Console::Interactivity::ServiceLocator;
        BEGIN_TEST_METHOD_PROPERTIES()
            // https://msdn.microsoft.com/en-us/library/windows/desktop/dd317756(v=vs.85).aspx
            // The interesting ones for us are:
            // Japanese Shift JIS = 932
            // Chinese Simplified GB2312 = 936
            // Korean Unified Hangul = 949
            // Chinese Traditional Big5 = 950
            TEST_METHOD_PROPERTY(L"Data:uiStartupCP", L"{437, 850, 932, 936, 949, 950}")
            TEST_METHOD_PROPERTY(L"Data:uiOutputCP", L"{437, 850, 932, 936, 949, 950}")
        END_TEST_METHOD_PROPERTIES()

        // if ServiceLocator::LocateGlobals().uiWindowsCP = a CJK one
        // we should get SUCCESS and a matching result to our input
        // for any other ServiceLocator::LocateGlobals().uiWindowsCP we should get STATUS_NOT_SUPPORTED and do nothing with the langid.

        VERIFY_SUCCEEDED_RETURN(TestData::TryGetValue(L"uiStartupCP", ServiceLocator::LocateGlobals().uiWindowsCP));

        UINT outputCP;
        VERIFY_SUCCEEDED_RETURN(TestData::TryGetValue(L"uiOutputCP", outputCP));

        LANGID langId = 0;
        const auto status = GetConsoleLangId(outputCP, &langId);

        if (s_uiOEMJapaneseCP == ServiceLocator::LocateGlobals().uiWindowsCP ||
            s_uiOEMSimplifiedChineseCP == ServiceLocator::LocateGlobals().uiWindowsCP ||
            s_uiOEMKoreanCP == ServiceLocator::LocateGlobals().uiWindowsCP ||
            s_uiOEMTraditionalChineseCP == ServiceLocator::LocateGlobals().uiWindowsCP)
        {
            VERIFY_ARE_EQUAL(STATUS_SUCCESS, status);

            LANGID langIdExpected;
            switch (outputCP)
            {
            case s_uiOEMJapaneseCP:
                langIdExpected = s_langIdJapanese;
                break;
            case s_uiOEMSimplifiedChineseCP:
                langIdExpected = s_langIdSimplifiedChinese;
                break;
            case s_uiOEMKoreanCP:
                langIdExpected = s_langIdKorean;
                break;
            case s_uiOEMTraditionalChineseCP:
                langIdExpected = s_langIdTraditionalChinese;
                break;
            default:
                langIdExpected = s_langIdEnglish;
                break;
            }

            VERIFY_ARE_EQUAL(langIdExpected, langId);
        }
        else
        {
            VERIFY_ARE_EQUAL(STATUS_NOT_SUPPORTED, status);
        }
    }

    static constexpr UINT s_uiUtf8CP = CP_UTF8;

    TEST_METHOD(TestInitializeConsoleCodePageRecomputesCPInfo)
    {
        using Microsoft::Console::Interactivity::ServiceLocator;
        auto& gci = ServiceLocator::LocateGlobals().getConsoleInformation();

        // Snapshot every field the initializer mutates so the shared global state is restored exactly.
        const auto originalCodePage = gci.GetCodePage();
        const auto originalCP = gci.CP;
        const auto originalOutputCP = gci.OutputCP;
        const auto originalDefaultCP = gci.DefaultCP;
        const auto originalDefaultOutputCP = gci.DefaultOutputCP;
        const auto originalCPInfo = gci.CPInfo;
        const auto originalOutputCPInfo = gci.OutputCPInfo;
        const auto restore = wil::scope_exit([&]() {
            gci.SetCodePage(originalCodePage);
            gci.CP = originalCP;
            gci.OutputCP = originalOutputCP;
            gci.DefaultCP = originalDefaultCP;
            gci.DefaultOutputCP = originalDefaultOutputCP;
            gci.CPInfo = originalCPInfo;
            gci.OutputCPInfo = originalOutputCPInfo;
        });

        // Seed a DBCS state first: 0xA4 is a Big5 lead byte.
        gci.SetCodePage(s_uiOEMTraditionalChineseCP);
        InitializeConsoleCodePage();

        VERIFY_ARE_EQUAL(s_uiOEMTraditionalChineseCP, gci.CP);
        VERIFY_ARE_EQUAL(s_uiOEMTraditionalChineseCP, gci.OutputCP);
        VERIFY_ARE_EQUAL(s_uiOEMTraditionalChineseCP, gci.DefaultCP);
        VERIFY_ARE_EQUAL(s_uiOEMTraditionalChineseCP, gci.DefaultOutputCP);
        VERIFY_IS_TRUE(IsDBCSLeadByteConsole(static_cast<char>(0xA4), &gci.CPInfo));
        VERIFY_IS_TRUE(IsDBCSLeadByteConsole(static_cast<char>(0xA4), &gci.OutputCPInfo));

        // Switching to UTF-8 must replace the stale Big5 lead byte table. This is
        // the regression: upstream computed CPInfo before assigning the code page.
        gci.SetCodePage(s_uiUtf8CP);
        InitializeConsoleCodePage();

        VERIFY_ARE_EQUAL(s_uiUtf8CP, gci.CP);
        VERIFY_ARE_EQUAL(s_uiUtf8CP, gci.OutputCP);
        VERIFY_ARE_EQUAL(s_uiUtf8CP, gci.DefaultCP);
        VERIFY_ARE_EQUAL(s_uiUtf8CP, gci.DefaultOutputCP);
        VERIFY_ARE_EQUAL(4u, gci.CPInfo.MaxCharSize);
        VERIFY_ARE_EQUAL(4u, gci.OutputCPInfo.MaxCharSize);
        VERIFY_IS_FALSE(IsDBCSLeadByteConsole(static_cast<char>(0xA4), &gci.CPInfo));
        VERIFY_IS_FALSE(IsDBCSLeadByteConsole(static_cast<char>(0xA4), &gci.OutputCPInfo));
    }
};
