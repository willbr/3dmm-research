/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "kauai_core.h"
#include "scrcom.h"
#include "screxe.h"
#include "framedef.h"
#include "mssio.h"
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

static bool Matches(ScriptInterpreter::PScript script, const LegacyLong *expected, String *literal)
{
    if (!script || mm_script_word_count(script) != 3 || mm_script_literal_count(script) != 1)
        return false;
    for (MMU32 i = 0; i < 3; ++i)
        if (mm_script_word(script, i) != expected[i])
            return false;
    String read;
    return ScriptInterpreter::ReadScriptLiteral(script, 0, &read) && read.FEqual(literal);
}

static bool CheckScriptResources()
{
    using namespace Chunky;
    using namespace Group;
    using namespace ScriptInterpreter;
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
        auto direct = ReadScript(file, kctgScript, root);
        auto cache = ChunkyResourceFile::PcrfNew(file, 4096);
        auto cached = cache ? FetchScript(cache->Resources(), kctgScript, root) : nullptr;
        ok = Matches(direct, expected, &literal) && cached && mm_script_binding(cached)->cache == cache->CoreCache();
        ReleasePpo(&cache);
        const ChunkNumber saved = root + 1;
        ok = ok && Matches(cached, expected, &literal) && !mm_script_binding(cached)->cache &&
             SaveScript(cached, file, kctgScript, saved, true);
        auto reopened = ok ? ReadScript(file, kctgScript, saved) : nullptr;
        ok = Matches(reopened, expected, &literal);
        ReleaseScript(&reopened);
        ReleaseScript(&cached);
        ReleaseScript(&direct);
    }
    ReleasePpo(&strings);
    ReleasePpo(&words);
    ReleasePpo(&file);
    return ok;
}
static bool CheckInterpreterResume(bool infix)
{
    using namespace ScriptCompiler;
    using namespace ScriptInterpreter;

    char infixSource[] = "value = _parm[0] + 7; Pause(); value = value * 2; Return(value);\r\n";
    char postfixSource[] = "0 <&_parm 7 Add >value Pause $resume Go 99 Return @resume <value 2 Mul Return\r\n";
    auto source = infix ? infixSource : postfixSource;
    auto input = FileObject::PfilCreateTemp();
    CompilerBase compiler;
    MessageSinkIO errors(stderr);
    auto script = input && input->FWriteRgb(source, std::strlen(source), 0)
                      ? compiler.PscptCompileFil(input, infix, &errors)
                      : nullptr;
    ReleasePpo(&input);
    if (!script)
        return false;

    Interpreter interpreter;
    LegacyLong argument = -3;
    LegacyLong result = 0;
    bool paused = false;
    bool ran = interpreter.FRunScript(script, &argument, 1, &result, &paused) && paused;
    ReleaseScript(&script);
    return ran && interpreter.FResume(&result, &paused) && !paused && result == 8;
}

static bool CheckCustomNumberReader()
{
    class ModelLexer : public LexerBase
    {
      protected:
        void _ReadNumTok(PToken token, achar ch, LegacyLong base, LegacyLong limit) override
        {
            LexerBase::_ReadNumTok(token, ch, base, limit);
            if (_FFetchRgch(&ch) && ch == '.')
            {
                _Advance();
                if (_FFetchRgch(&ch) && mm_lexer_is_decimal(static_cast<uchar>(ch)))
                {
                    _Advance();
                    token->tt = ttLimBase;
                    token->lw = token->lw * 10 + ch - '0';
                }
            }
        }

      public:
        explicit ModelLexer(PFileObject input) : LexerBase(input) {}
    };

    char source[] = "12.5 + 0x1F";
    auto input = FileObject::PfilCreateTemp();
    if (!input)
        return false;
    if (!input->FWriteRgb(source, sizeof(source) - 1, 0))
    {
        ReleasePpo(&input);
        return false;
    }
    ModelLexer lexer(input);
    ReleasePpo(&input);
    Token token;
    return lexer.FGetTok(&token) && token.tt == ttLimBase && token.lw == 125 &&
           lexer.FGetTok(&token) && token.tt == ttAdd &&
           lexer.FGetTok(&token) && token.tt == ttLong && token.lw == 31 &&
           !lexer.FGetTok(&token);
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
    if (!CheckCustomNumberReader())
        return 1;
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
    if (!CheckInterpreterResume(true) || !CheckInterpreterResume(false))
    {
        std::fputs("Script interpreter lost state across pause and resume.\n", stderr);
        return 1;
    }
    std::puts("Script names, foreign-order resources and interpreter resume passed.");
    return 0;
}
