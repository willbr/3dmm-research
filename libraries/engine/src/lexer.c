/* Base lexer converted from Kauai lex.cpp by ShonK.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "mm/lexer.h"
#include "mm/wide.h"
#include <string.h>

#define LEX_NUMBER_LIMIT 0x7fffffffUL

static int is_space(unsigned ch)
{
    return ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r' || ch == 0x1a;
}

static int is_letter(unsigned ch)
{
    return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || ch == '_';
}

int mm_lexer_is_decimal(unsigned ch)
{
    return ch >= '0' && ch <= '9';
}

static int is_quote(unsigned ch)
{
    return ch == '"' || ch == '\'';
}

int mm_lexer_init(MMLexer *lexer, const MMReadStream *input, unsigned unit_bytes, int union_strings)
{
    if (!lexer || !input || !input->read_at || (unit_bytes != 1 && unit_bytes != 2))
        return 0;
    memset(lexer, 0, sizeof(*lexer));
    lexer->input = *input;
    lexer->unit_bytes = unit_bytes;
    lexer->union_strings = union_strings;
    lexer->line = 1;
    lexer->line_start = 1;
    return 1;
}

static int ensure_units(MMLexer *lexer, unsigned count)
{
    unsigned left = lexer->end - lexer->begin;
    unsigned available, read;
    unsigned unit = lexer->unit_bytes;
    unsigned char *bytes = (unsigned char *)&lexer->buffer;

    if (left >= count)
        return 1;
    available = (lexer->input.length - lexer->offset) / unit;
    if (available < count - left)
        return 0;
    if (left)
        memmove(bytes, bytes + lexer->begin * unit, left * unit);
    lexer->begin = 0;
    lexer->end = left;
    read = MM_LEX_BUFFER_UNITS - left;
    if (available < read)
        read = available;
    if (!mm_read_exact(&lexer->input, lexer->offset, bytes + left * unit, read * unit))
    {
        lexer->input.length = lexer->offset;
        return 0;
    }
    lexer->end += read;
    lexer->offset += read * unit;
    return 1;
}

static unsigned unit_at(const MMLexer *lexer, unsigned index)
{
    return lexer->unit_bytes == 1 ? lexer->buffer.narrow[lexer->begin + index]
                                 : lexer->buffer.wide[lexer->begin + index];
}

int mm_lexer_peek(MMLexer *lexer, unsigned *ch)
{
    if (!ensure_units(lexer, 1))
        return 0;
    *ch = unit_at(lexer, 0);
    return 1;
}

static void advance(MMLexer *lexer, unsigned count)
{
    lexer->begin += count;
    lexer->column += count;
}

void mm_lexer_advance(MMLexer *lexer)
{
    advance(lexer, 1);
}

static void append(MMLexText *text, unsigned unit_bytes, unsigned ch)
{
    if (text->length == MM_LEX_TEXT_CAPACITY)
        return;
    if (unit_bytes == 1)
        text->units.narrow[text->length] = (unsigned char)ch;
    else
        text->units.wide[text->length] = (unsigned short)ch;
    ++text->length;
}

MMI32 mm_lexer_number(MMLexer *lexer, unsigned first, unsigned base, MMU32 limit)
{
    unsigned ch;
    MMU32 value = first - '0';
    while (limit > 1 && mm_lexer_peek(lexer, &ch) && ch >= '0' && ch - '0' < base)
    {
        value = value * base + (ch - '0');
        mm_lexer_advance(lexer);
        --limit;
    }
    return mm_signed_word(value);
}

static int hex_digit(unsigned ch)
{
    if (mm_lexer_is_decimal(ch))
        return (int)(ch - '0');
    if (ch >= 'a' && ch <= 'f')
        return (int)(ch - 'a') + 10;
    if (ch >= 'A' && ch <= 'F')
        return (int)(ch - 'A') + 10;
    return -1;
}

static int read_hex(MMLexer *lexer, MMU32 *number)
{
    unsigned ch;
    int digit;
    MMU32 value = 0;
    if (!mm_lexer_peek(lexer, &ch) || (digit = hex_digit(ch)) < 0)
        return 0;
    do
    {
        value = value * 16 + (unsigned)digit;
        mm_lexer_advance(lexer);
    } while (mm_lexer_peek(lexer, &ch) && (digit = hex_digit(ch)) >= 0);
    *number = value;
    return 1;
}

static int read_escape(MMLexer *lexer, unsigned *out)
{
    unsigned ch;
    MMU32 number;
    if (!mm_lexer_peek(lexer, &ch))
        return 0;
    mm_lexer_advance(lexer);
    switch (ch)
    {
    case '\r':
        while (mm_lexer_peek(lexer, &ch) && ch == '\n')
            mm_lexer_advance(lexer);
        ch = 0;
        break;
    case 't':
        ch = '\t';
        break;
    case 'n':
        ch = '\r';
        break;
    case 'x':
    case 'X':
        if (!read_hex(lexer, &number))
            return 0;
        ch = (unsigned)(number & (lexer->unit_bytes == 1 ? 0xffU : 0xffffU));
        break;
    default:
        if (ch >= '0' && ch <= '7')
        {
            number = (MMU32)mm_lexer_number(lexer, ch, 8, 3);
            ch = (unsigned)(number & (lexer->unit_bytes == 1 ? 0xffU : 0xffffU));
        }
        break;
    }
    *out = ch;
    return 1;
}

static int line_directive(MMLexer *lexer)
{
    static const char prefix[] = "#line";
    static const unsigned short wide_prefix[] = {'#', 'l', 'i', 'n', 'e', 0};
    const unsigned char *bytes;
    const void *expected;
    unsigned ch;
    MMLexText filename = {0};
    MMU32 saved_line;
    int slash = 0;

    if (!ensure_units(lexer, sizeof(prefix)) || !is_space(unit_at(lexer, sizeof(prefix) - 1)))
        return 0;
    bytes = (const unsigned char *)&lexer->buffer + lexer->begin * lexer->unit_bytes;
    expected = lexer->unit_bytes == 1 ? (const void *)prefix : (const void *)wide_prefix;
    /* Kauai compares five bytes of this prefix even in a wide source. */
    if (memcmp(bytes, expected, sizeof(prefix) - 1) != 0)
        return 0;

    advance(lexer, sizeof(prefix) - 1);
    while (mm_lexer_peek(lexer, &ch) && is_space(ch) && ch != '\r')
        mm_lexer_advance(lexer);
    if (!mm_lexer_peek(lexer, &ch) || !mm_lexer_is_decimal(ch))
        return -1;
    mm_lexer_advance(lexer);
    saved_line = lexer->line;
    lexer->line = (MMU32)mm_lexer_number(lexer, ch, 10, LEX_NUMBER_LIMIT) - 1;
    if (!mm_lexer_peek(lexer, &ch))
        return 1;
    if (!is_space(ch))
        goto bad_directive;
    while (mm_lexer_peek(lexer, &ch) && is_space(ch) && ch != '\r')
        mm_lexer_advance(lexer);
    if (!mm_lexer_peek(lexer, &ch) || ch == '\r')
        return 1;
    if (ch != '"')
        goto bad_directive;

    mm_lexer_advance(lexer);
    for (;;)
    {
        if (!mm_lexer_peek(lexer, &ch) || ch == '\r')
            goto bad_directive;
        mm_lexer_advance(lexer);
        if (ch == '"')
            break;
        if (ch == '\\')
        {
            slash = !slash;
            if (!slash)
                continue;
        }
        else
            slash = 0;
        append(&filename, lexer->unit_bytes, ch);
    }
    if (mm_lexer_peek(lexer, &ch) && !is_space(ch))
        goto bad_directive;
    while (mm_lexer_peek(lexer, &ch) && is_space(ch) && ch != '\r')
        mm_lexer_advance(lexer);
    if (mm_lexer_peek(lexer, &ch) && ch != '\r')
        goto bad_directive;
    lexer->filename = filename;
    lexer->has_filename = 1;
    return 1;

bad_directive:
    lexer->line = saved_line;
    return -1;
}

static int skip_space(MMLexer *lexer)
{
    unsigned ch, next;
    int block = 0, star = 0, directive;
    while (mm_lexer_peek(lexer, &ch))
    {
        if (is_space(ch) || lexer->skip_line || block)
        {
            mm_lexer_advance(lexer);
            if (ch == '\r')
            {
                ++lexer->line;
                lexer->column = 0;
                lexer->line_start = 1;
                lexer->skip_line = 0;
            }
            else if (ch == '\n' && lexer->column == 1)
                lexer->column = 0;
            if (block)
            {
                if (star && ch == '/')
                    block = 0;
                star = ch == '*';
            }
            continue;
        }
        if (ch == '/' && ensure_units(lexer, 2))
        {
            next = unit_at(lexer, 1);
            if (next == '/' || next == '*')
            {
                advance(lexer, 2);
                lexer->skip_line = next == '/';
                block = next == '*';
                star = 0;
                continue;
            }
        }
        if (lexer->line_start && ch == '#')
        {
            directive = line_directive(lexer);
            if (directive < 0)
                return 0;
            if (directive)
                continue;
        }
        lexer->line_start = 0;
        break;
    }
    return !block;
}

static int quoted_string(MMLexer *lexer, MMLexText *text, int raw_path)
{
    unsigned ch;
    while (mm_lexer_peek(lexer, &ch))
    {
        mm_lexer_advance(lexer);
        if (ch == '\r')
            return 0;
        if (ch == '"')
        {
            if (raw_path || !lexer->union_strings)
                return 1;
            if (!skip_space(lexer))
            {
                lexer->skip_line = 1;
                return 0;
            }
            if (!mm_lexer_peek(lexer, &ch) || ch != '"')
                return 1;
            mm_lexer_advance(lexer);
            continue;
        }
        if (ch == '\\' && !raw_path)
        {
            if (!read_escape(lexer, &ch))
            {
                lexer->skip_line = 1;
                return 0;
            }
            if (!ch)
                continue;
        }
        append(text, lexer->unit_bytes, ch);
    }
    return 0;
}

int mm_lexer_path(MMLexer *lexer, MMLexText *text)
{
    unsigned ch;
    text->length = 0;
    if (!skip_space(lexer))
    {
        lexer->skip_line = 1;
        return 0;
    }
    if (!mm_lexer_peek(lexer, &ch) || ch != '"')
        return 0;
    mm_lexer_advance(lexer);
    return quoted_string(lexer, text, 1);
}

static int quoted_number(MMLexer *lexer, MMLexToken *token)
{
    unsigned count, ch;
    MMU32 value = 0;
    for (count = 0; count < 5 && mm_lexer_peek(lexer, &ch); ++count)
    {
        mm_lexer_advance(lexer);
        if (ch == '\r')
            break;
        if (ch == '\'')
        {
            token->number = mm_signed_word(value);
            return 1;
        }
        if (ch == '\\' && !read_escape(lexer, &ch))
        {
            lexer->skip_line = 1;
            break;
        }
        value = (value << 8) + (ch & 0xffU);
    }
    return 0;
}

typedef struct LexOperator
{
    unsigned char ch, single, doubled, assign;
} LexOperator;

static const LexOperator operators[] = {
    {'!', ttLNot, ttNil, ttNe}, {'#', ttPound, ttNil, ttNil}, {'$', ttDollar, ttNil, ttNil},
    {'%', ttMod, ttNil, ttAMod}, {'&', ttBAnd, ttLAnd, ttABAnd}, {'(', ttOpenParen, ttNil, ttNil},
    {')', ttCloseParen, ttNil, ttNil}, {'*', ttMul, ttNil, ttAMul}, {'+', ttAdd, ttInc, ttAAdd},
    {',', ttComma, ttNil, ttNil}, {'-', ttSub, ttDec, ttASub}, {'.', ttDot, ttNil, ttNil},
    {'/', ttDiv, ttNil, ttADiv}, {':', ttColon, ttScope, ttNil}, {';', ttSemi, ttNil, ttNil},
    {'<', ttLt, ttShl, ttLe}, {'=', ttAssign, ttEq, ttNil}, {'>', ttGt, ttShr, ttGe},
    {'?', ttQuery, ttNil, ttNil}, {'@', ttAt, ttNil, ttNil}, {'[', ttOpenRef, ttNil, ttNil},
    {'\\', ttBackSlash, ttNil, ttNil}, {']', ttCloseRef, ttNil, ttNil}, {'^', ttBXor, ttLXor, ttABXor},
    {'`', ttAccent, ttNil, ttNil}, {'{', ttOpenBrace, ttNil, ttNil}, {'|', ttBOr, ttLOr, ttABOr},
    {'}', ttCloseBrace, ttNil, ttNil}, {'~', ttBNot, ttNil, ttNil}
};

static int read_operator(MMLexer *lexer, unsigned ch)
{
    unsigned i, next;
    int kind;
    for (i = 0; i < sizeof(operators) / sizeof(operators[0]); ++i)
        if (operators[i].ch == ch)
        {
            kind = ttNil;
            if ((operators[i].doubled || operators[i].assign) && mm_lexer_peek(lexer, &next))
            {
                if (next == ch)
                    kind = operators[i].doubled;
                else if (next == '=')
                    kind = operators[i].assign;
                else if (ch == '-' && next == '>')
                    kind = ttArrow;
            }
            if (kind == ttNil)
                return operators[i].single;
            mm_lexer_advance(lexer);
            if ((kind == ttShr || kind == ttShl) && mm_lexer_peek(lexer, &next) && next == '=')
            {
                mm_lexer_advance(lexer);
                kind = kind == ttShr ? ttAShr : ttAShl;
            }
            return kind;
        }
    return ttError;
}

static int number_token(MMLexer *lexer, MMLexToken *token, unsigned ch,
                        MMLexNumberReader number_reader, void *context)
{
    unsigned next, base = 10;
    MMU32 hex;
    if (ch == '0')
    {
        if (!mm_lexer_peek(lexer, &next))
        {
            token->number = 0;
            return 1;
        }
        if (next == 'x' || next == 'X')
        {
            mm_lexer_advance(lexer);
            if (!read_hex(lexer, &hex))
                return 0;
            token->number = mm_signed_word(hex);
            return 1;
        }
        base = 8;
    }
    if (number_reader)
        number_reader(context, token, ch, base);
    else
        token->number = mm_lexer_number(lexer, ch, base, LEX_NUMBER_LIMIT);
    return 1;
}

int mm_lexer_next(MMLexer *lexer, MMLexToken *token, MMLexNumberReader number_reader, void *context)
{
    unsigned ch;
    token->text.length = 0;
    token->number = 0;
    if (!skip_space(lexer))
    {
        lexer->skip_line = 1;
        goto error;
    }
    if (!mm_lexer_peek(lexer, &ch))
    {
        token->kind = ttNil;
        return 0;
    }
    mm_lexer_advance(lexer);
    if (mm_lexer_is_decimal(ch))
    {
        token->kind = ttLong;
        if (!number_token(lexer, token, ch, number_reader, context) ||
            (mm_lexer_peek(lexer, &ch) && (mm_lexer_is_decimal(ch) || is_letter(ch) || is_quote(ch))))
            goto error;
    }
    else if (ch == '"')
    {
        token->kind = ttString;
        if (!quoted_string(lexer, &token->text, 0))
            goto error;
    }
    else if (ch == '\'')
    {
        token->kind = ttLong;
        if (!quoted_number(lexer, token))
            goto error;
    }
    else if (is_letter(ch))
    {
        token->kind = ttName;
        append(&token->text, lexer->unit_bytes, ch);
        while (mm_lexer_peek(lexer, &ch) && (is_letter(ch) || mm_lexer_is_decimal(ch)))
        {
            append(&token->text, lexer->unit_bytes, ch);
            mm_lexer_advance(lexer);
        }
    }
    else
        token->kind = read_operator(lexer, ch);
    return 1;

error:
    token->kind = ttError;
    token->text.length = 0;
    return 1;
}
