/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "kauai_core.h"
#include "scrcom.h"
#include "screxe.h"
#include "framedef.h"
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

class ScriptProbe : public ScriptInterpreter::Script
{
public:
    static bool Matches(ScriptInterpreter::PScript script, const LegacyLong *expected, String *literal)
    {
        if (!script)
            return false;
        const auto wordsMember = &ScriptProbe::_pgllw;
        const auto stringsMember = &ScriptProbe::_pgstLiterals;
        auto words = script->*wordsMember;
        auto strings = script->*stringsMember;
        if (words->IvMac() != 3 || !strings || strings->IstnMac() != 1)
            return false;
        for (LegacyLong i = 0; i < 3; ++i)
        {
            LegacyLong word;
            words->Get(i, &word);
            if (word != expected[i])
                return false;
        }
        String read;
        strings->GetStn(0, &read);
        return read.FEqual(literal);
    }
};

static bool CheckScriptResources()
{
    using namespace Chunky;
    using namespace Group;
    using ScriptInterpreter::Script;
    const LegacyLong expected[] = {0x11223344, -2, 0x01234567};
    LegacyLong foreign[] = {expected[0], expected[1], expected[2]};
    SwapBytesRglw(foreign, 3);
    auto file = ChunkyFile::PcflCreateTemp();
    auto words = DynamicArray::PglNew(sizeof(LegacyLong));
    auto strings = StringTable_GST::PgstNew();
    String literal(PszLit("Studio literal"));
    DataBlock block;
    ChunkNumber root, child;
    bool ok = file && words && strings && words->FInsert(0, foreign, 3) && strings->FAddStn(&literal) &&
              block.FSetTemp(words->CbOnFile()) && words->FWrite(&block, kboOther) &&
              file->FAddBlck(&block, kctgScript, &root) && block.FSetTemp(strings->CbOnFile()) &&
              strings->FWrite(&block, kboOther) && file->FAddBlck(&block, kctgScriptStrs, &child) &&
              file->FAdoptChild(kctgScript, root, kctgScriptStrs, child, 0);
    if (ok)
    {
        auto direct = Script::PscptRead(file, kctgScript, root);
        auto cache = ChunkyResourceFile::PcrfNew(file, 4096);
        auto cached = cache ? static_cast<ScriptInterpreter::PScript>(cache->PbacoFetch(kctgScript, root,
                                                                                     Script::FReadScript))
                            : nullptr;
        ok = ScriptProbe::Matches(direct, expected, &literal) && ScriptProbe::Matches(cached, expected, &literal);
        ReleasePpo(&cached);
        ReleasePpo(&cache);
        ReleasePpo(&direct);
    }
    ReleasePpo(&strings);
    ReleasePpo(&words);
    ReleasePpo(&file);
    return ok;
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
    if (!CheckScriptResources())
    {
        std::fputs("Resource reader changed script words or literals.\n", stderr);
        return 1;
    }
    std::puts("Script names and foreign-order script resources passed.");
    return 0;
}
