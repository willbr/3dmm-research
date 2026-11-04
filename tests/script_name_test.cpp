/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "kauai_core.h"
#include "scrcom.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
ASSERTNAME
void WarnProc(schar *file, LegacyLong line, schar *message)
{
    std::fprintf(stderr, "%s:%" LEGACY_PRId ": %s\n", file, line, message);
}
bool FAssertProc(schar *file, LegacyLong line, schar *message, void *, LegacyLong)
{
    WarnProc(file, line, message);
    std::exit(1);
}
int main()
{
    auto input = FileObject::PfilCreateTemp();
    const char numbers[] = "0x7fffffff 0x80000000 0xffffffff 2147483648 4294967295 'ABCD'\r\n";
    if (!input || !input->FWriteRgb(const_cast<char *>(numbers), sizeof(numbers) - 1, 0))
        return 1;
    {
        LexerBase lexer(input);
        const LegacyLong expected[] = {
            0x7fffffff, static_cast<LegacyLong>(0x80000000u), -1, static_cast<LegacyLong>(0x80000000u), -1, 0x41424344};
        for (const auto value : expected)
        {
            Token token;
            if (!lexer.FGetTok(&token) || token.tt != ttLong || token.lw != value)
                return 1;
        }
    }
    ReleasePpo(&input);
    struct Example
    {
        const char *text;
        LegacyUlong high, low;
    };
    // Independent 48-bit encodings from the identifier format: 0=padding,
    // 1..10=digits, 11..36=uppercase, 37..62=lowercase, 63=underscore.
    const Example examples[] = {{"_cparm", 0xfe7d, 0x25db1000},
                                {"_parm", 0xff49, 0x76c40000},
                                {"Ab0_zZ9", 0x2e60, 0x7ffa4280},
                                {"abcdefgh", 0x9669, 0xe8a6aaec},
                                {"", 0, 0}};
    for (const auto &example : examples)
    {
        String text;
        text.SetSzs(const_cast<char *>(example.text));
        ScriptCompiler::RuntimeVariableName name;
        name.SetFromStn(&text);
        if (name.lu1 != example.high || name.lu2 != example.low)
            return 1;
        String decoded;
        name.GetStn(&decoded);
        if (!decoded.FEqual(&text))
            return 1;
        name.lu1 |= 0xabcd0000;
        name.GetStn(&decoded);
        text.FAppendSz(PszLit("[43981]"));
        if (!decoded.FEqual(&text))
            return 1;
    }
    std::puts("Script names: original packed encodings, decoding and array-index suffixes passed.");
    return 0;
}
