/* Infix compilation converted from Kauai scrcom.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "script_compile_internal.h"
#include "mm/collections.h"
#include "mm/lex_token.h"
#include "mm/script_opcodes.h"
#include "mm/script_runtime.h"
#include "mm/wide.h"
#include <string.h>

enum { NO_NODE = -1, fsccNil = 0, fsccWantVoid = 1, fsccTop = 2 };

typedef struct CompileArray
{
    MMCollectionStorage storage;
    MMArray array;
} CompileArray;

/* operator flags - for in-fix compilation */
enum
{
    fopNil = 0,
    fopOp = 1,         /* an operator */
    fopAssign = 2,     /* an assignment operator */
    fopPostAssign = 4, /* unary operator acts after using the value */
    fopFunction = 8,   /* a function */
    fopArray = 16      /* an array reference */
};

/* expression tree node - for in-fix compilation */
typedef struct Expression
{
    short tt;     /* the token type */
    short op;     /* operator (or function) to generate */
    short opl;    /* operator precedence level */
    short grfop;  /* flags */
    MMI32 lwValue; /* value if a ttLong; an istn if a ttName */
    MMI32 ietn1;   /* indices into _pgletnTree for the operands */
    MMI32 ietn2;
    MMI32 ietn3;
    MMI32 cetnDeep; /* depth of the etn tree to here */
} Expression;

/* control structure types - these are the keywords that are followed */
/* by a condition (which is why Else is not here). */
enum
{
    cstNil,
    cstIf,
    cstElif,
    cstWhile
};

/* control structure descriptor */
typedef struct ControlFrame
{
    MMI32 cst;
    MMI32 lwLabel1;  /* use depends on cst */
    MMI32 lwLabel2;  /* use depends on cst */
    MMI32 lwLabel3;  /* use depends on cst */
    CompileArray *condition; /* Saved expression tree for a conditional loop. */
    MMI32 ietnTop;   /* the top of the expression tree */
} ControlFrame;


typedef struct CompileName
{
    const void *text;
    MMU32 length;
} CompileName;

typedef struct VariableName
{
    MMU32 high, low;
} VariableName;

enum { KEY_NONE = -1, KEY_IF, KEY_ELIF, KEY_ELSE, KEY_END, KEY_WHILE, KEY_BREAK, KEY_CONTINUE };

typedef struct InfixCompiler
{
    MMScriptEmitter *emitter;
    const MMScriptInput *input;
    CompileArray expressions, operators, controls;
    CompileArray *tree;
    MMCollectionStorage name_storage;
    MMStringTable names;
    MMI32 next_label;
    int eof;
} InfixCompiler;

static int add_expression(InfixCompiler *, Expression *, MMI32 *);
static void set_depth(InfixCompiler *, Expression *, int);
static int constant_at(InfixCompiler *, MMI32, MMI32 *);
static int fold_binary(InfixCompiler *, MMI32, MMI32, MMI32, MMI32 *);
static void emit_expression(InfixCompiler *, MMI32, MMU32, MMI32 *);
static MMI32 conditional_kind(InfixCompiler *, MMI32);
static void begin_control(InfixCompiler *, MMI32, MMI32);
static int emit_control_keyword(InfixCompiler *, MMI32, MMU32);
static void emit_variable_access(InfixCompiler *, MMI32, VariableName *, MMI32 *, MMI32 *, MMI32 *);
static void emit_function(InfixCompiler *, MMI32, MMU32, MMI32);

static void report(InfixCompiler *compiler, int error)
{
    mm_script_emitter_error(compiler->emitter, error);
}

static int array_init(CompileArray *array, const MMStorageAllocator *allocator, MMU32 width, MMU32 grow)
{
    if (mm_storage_init(&array->storage, allocator) != MM_STORAGE_OK)
        return 0;
    array->storage.min_grow[0] = width * grow;
    array->array.storage = &array->storage;
    array->array.width = width;
    array->array.free_count = -1;
    return 1;
}

static int array_read(const CompileArray *array, MMI32 index, void *out)
{
    if (index < 0 || (MMU32)index >= array->array.count)
    {
        memset(out, 0, array->array.width);
        return 0;
    }
    memcpy(out, array->storage.buffers[0].data + index * array->array.width, array->array.width);
    return 1;
}

static void array_write(CompileArray *array, MMI32 index, const void *record)
{
    if (index >= 0 && (MMU32)index < array->array.count)
        memcpy(array->storage.buffers[0].data + index * array->array.width, record, array->array.width);
}

static int array_push(CompileArray *array, const void *record)
{
    return mm_array_add(&array->array, record, NULL) == MM_STORAGE_OK;
}

static int array_pop(CompileArray *array, void *record)
{
    return mm_array_pop(&array->array, record) == MM_STORAGE_OK;
}

static int append_expression(InfixCompiler *compiler, const Expression *expression, MMI32 *index)
{
    MMU32 slot;
    if (mm_array_add(&compiler->tree->array, expression, &slot) != MM_STORAGE_OK)
        return 0;
    *index = (MMI32)slot;
    return 1;
}

static void destroy_tree(CompileArray *tree)
{
    MMStorageAllocator allocator;
    if (tree)
    {
        allocator = tree->storage.allocator;
        mm_storage_dispose(&tree->storage);
        allocator.release(allocator.context, tree, sizeof(*tree));
    }
}

static CompileArray *clone_tree(const CompileArray *tree)
{
    const MMStorageAllocator *allocator = &tree->storage.allocator;
    CompileArray *copy = allocator->resize(allocator->context, NULL, 0, sizeof(*copy), MM_STORAGE_NORMAL);
    if (!copy)
        return NULL;
    memset(copy, 0, sizeof(*copy));
    array_init(copy, allocator, sizeof(Expression), 0);
    if (mm_storage_copy(&copy->storage, &tree->storage, tree->array.count * sizeof(Expression), 0) != MM_STORAGE_OK)
    {
        destroy_tree(copy);
        return NULL;
    }
    copy->array.count = tree->array.count;
    return copy;
}

static int reserve_names(void *context, MMStringTable *table, MMU32 bytes, MMU32 entries)
{
    MMCollectionStorage *storage = context;
    int status = mm_storage_ensure(storage, bytes, entries, 0);
    table->strings = storage->buffers[0].data;
    table->entries = storage->buffers[1].data;
    table->string_capacity = storage->buffers[0].capacity;
    table->entry_capacity = storage->buffers[1].capacity;
    return status == MM_STORAGE_OK;
}

static MMI32 intern_name(InfixCompiler *compiler, const void *text, MMU32 length)
{
    MMU32 index = compiler->names.count;
    if (!mm_strings_find(&compiler->names, text, length, NULL, NULL, &index) &&
        !mm_strings_insert(&compiler->names, index, text, length, NULL))
        report(compiler, MM_SCRIPT_COMPILE_MEMORY);
    return (MMI32)index;
}

static CompileName interned_name(InfixCompiler *compiler, MMI32 index)
{
    CompileName name = {NULL, 0};
    mm_strings_get(&compiler->names, (MMU32)index, &name.text, &name.length);
    return name;
}

static CompileName node_name(InfixCompiler *compiler, MMI32 index)
{
    Expression expression;
    CompileName empty = {NULL, 0};
    if (!array_read(compiler->tree, index, &expression) || expression.tt != ttName)
    {
        report(compiler, MM_SCRIPT_COMPILE_SYNTAX);
        return empty;
    }
    return interned_name(compiler, expression.lwValue);
}

static int keyword(InfixCompiler *compiler, CompileName name)
{
    static const char *const names[] = {"If", "Elif", "Else", "End", "While", "Break", "Continue"};
    unsigned i;
    for (i = 0; i < sizeof(names) / sizeof(*names); ++i)
        if (mm_script_name_matches(names[i], name.text, name.length, compiler->names.unit_bytes))
            return (int)i;
    return KEY_NONE;
}

static void variable_name(InfixCompiler *compiler, CompileName name, VariableName *variable)
{
    if (keyword(compiler, name) != KEY_NONE ||
        mm_script_find_function(compiler->input->dialect, name.text, name.length, compiler->names.unit_bytes))
        report(compiler, MM_SCRIPT_COMPILE_KEYWORD_VARIABLE);
    if (!mm_script_variable_name(name.text, name.length, compiler->names.unit_bytes, &variable->high, &variable->low))
        report(compiler, MM_SCRIPT_COMPILE_VARIABLE);
}

static void emit_named_label(InfixCompiler *compiler, MMI32 node, int define)
{
    CompileName name = node_name(compiler, node);
    if (define)
        mm_script_emit_label(compiler->emitter, name.text, name.length);
    else
        mm_script_emit_label_reference(compiler->emitter, name.text, name.length);
}

static void emit_string(InfixCompiler *compiler, MMI32 index)
{
    CompileName name = interned_name(compiler, index);
    mm_script_emit_literal(compiler->emitter, name.text, name.length);
}

static void internal_label(InfixCompiler *compiler, MMI32 label, int define)
{
    static const char digits[] = "0123456789ABCDEF";
    unsigned char narrow[9];
    unsigned short wide[9];
    MMU32 value = (MMU32)label, first = sizeof(narrow), i;
    const void *text;
    /* User identifiers cannot begin with the numeric prefix. */
    do
    {
        narrow[--first] = (unsigned char)digits[value & 15];
        value >>= 4;
    } while (value);
    narrow[--first] = '0';
    text = narrow + first;
    if (compiler->names.unit_bytes == 2)
    {
        for (i = first; i < sizeof(narrow); ++i)
            wide[i] = narrow[i];
        text = wide + first;
    }
    if (define)
        mm_script_emit_label(compiler->emitter, text, sizeof(narrow) - first);
    else
        mm_script_emit_label_reference(compiler->emitter, text, sizeof(narrow) - first);
}

static MMI32 max_word(MMI32 left, MMI32 right)
{
    return left > right ? left : right;
}

static void swap_words(MMI32 *left, MMI32 *right)
{
    MMI32 saved = *left;
    *left = *right;
    *right = saved;
}

static void swap_flags(int *left, int *right)
{
    int saved = *left;
    *left = *right;
    *right = saved;
}

static void swap_expressions(Expression *left, Expression *right)
{
    Expression saved = *left;
    *left = *right;
    *right = saved;
}

static MMI32 divide_constants(MMI32 opcode, MMI32 left, MMI32 right)
{
    MMU32 a = left < 0 ? 0U - (MMU32)left : (MMU32)left;
    MMU32 b = right < 0 ? 0U - (MMU32)right : (MMU32)right;
    MMU32 result = opcode == kopDiv ? a / b : a % b;
    int negative = opcode == kopDiv ? (left < 0) != (right < 0) : left < 0;
    return mm_signed_word(negative ? 0U - result : result);
}
/* operator precedence levels */
enum
{
    oplNil = 0,
    koplComma,
    koplAssign,
    koplColon,
    koplLOr,
    koplLXor,
    koplLAnd,
    koplBOr,
    koplBXor,
    koplBAnd,
    koplEq,
    koplRel,
    koplShift,
    koplAdd,
    koplMul,
    koplPreUn,
    koplPostUn,
    koplGroup,
    koplNumber,
    koplRemoteName,
    koplThisName,
    koplGlobalName,
    koplArrayName,
    koplName,
    koplMax
};

/* token-operator map entry */
typedef struct OperatorToken
{
    short tt;
    short grfop;
    short op;
    short oplResolve;
    short oplMinRes;
    short ttPop;
    short oplPush;
} OperatorToken;

#define _TomeCore(tt, grfop, op, oplRes, oplMin, ttPop, oplPush)                                                       \
    {                                                                                                                  \
        tt, grfop, op, oplRes, oplMin, ttPop, oplPush                                                                  \
    }
#define _TomeRes(tt, op, oplRes)                                                                                       \
    {                                                                                                                  \
        tt, fopOp, op, oplRes, oplRes, ttNil, oplRes                                                                   \
    }
#define _TomePush(tt, op, oplPush)                                                                                     \
    {                                                                                                                  \
        tt, fopOp, op, oplNil, oplNil, ttNil, oplPush                                                                  \
    }

/* following a non-operator */
static const OperatorToken operators_after_operand[] = {
    _TomeCore(ttOpenParen, fopOp | fopFunction, opNil, koplName, koplName, ttNil, oplNil), /* function call */
    _TomeCore(ttCloseParen, fopNil, opNil, koplComma, koplComma, ttOpenParen, koplGroup),
    _TomeCore(ttOpenRef, fopOp | fopArray, opNil, koplName, koplName, ttNil, oplNil), /* array reference */
    _TomeCore(ttCloseRef, fopNil, opNil, koplComma, koplComma, ttOpenRef, koplArrayName),

    _TomeRes(ttAdd, kopAdd, koplAdd), _TomeRes(ttSub, kopSub, koplAdd), _TomeRes(ttMul, kopMul, koplMul),
    _TomeRes(ttDiv, kopDiv, koplMul), _TomeRes(ttMod, kopMod, koplMul),
    _TomeCore(ttInc, fopAssign | fopPostAssign, kopInc, koplRemoteName, koplRemoteName, ttNil, koplPostUn),
    _TomeCore(ttDec, fopAssign | fopPostAssign, kopDec, koplRemoteName, koplRemoteName, ttNil, koplPostUn),
    _TomeRes(ttBOr, kopBOr, koplBOr), _TomeRes(ttBAnd, kopBAnd, koplBAnd), _TomeRes(ttBXor, kopBXor, koplBXor),
    _TomeRes(ttShr, kopShr, koplShift), _TomeRes(ttShl, kopShl, koplShift), _TomeRes(ttLOr, opNil, koplLOr),
    _TomeRes(ttLAnd, opNil, koplLAnd), _TomeRes(ttLXor, kopLXor, koplLXor), _TomeRes(ttEq, kopEq, koplEq),
    _TomeRes(ttNe, kopNe, koplEq), _TomeRes(ttGt, kopGt, koplRel), _TomeRes(ttGe, kopGe, koplRel),
    _TomeRes(ttLt, kopLt, koplRel), _TomeRes(ttLe, kopLe, koplRel),

    _TomeCore(ttAssign, fopAssign | fopOp, opNil, koplAssign + 1, koplRemoteName, ttNil, koplAssign),
    _TomeCore(ttAAdd, fopAssign | fopOp, kopAdd, koplAssign + 1, koplRemoteName, ttNil, koplAssign),
    _TomeCore(ttASub, fopAssign | fopOp, kopSub, koplAssign + 1, koplRemoteName, ttNil, koplAssign),
    _TomeCore(ttAMul, fopAssign | fopOp, kopMul, koplAssign + 1, koplRemoteName, ttNil, koplAssign),
    _TomeCore(ttADiv, fopAssign | fopOp, kopDiv, koplAssign + 1, koplRemoteName, ttNil, koplAssign),
    _TomeCore(ttAMod, fopAssign | fopOp, kopMod, koplAssign + 1, koplRemoteName, ttNil, koplAssign),
    _TomeCore(ttABOr, fopAssign | fopOp, kopBOr, koplAssign + 1, koplRemoteName, ttNil, koplAssign),
    _TomeCore(ttABAnd, fopAssign | fopOp, kopBAnd, koplAssign + 1, koplRemoteName, ttNil, koplAssign),
    _TomeCore(ttABXor, fopAssign | fopOp, kopBXor, koplAssign + 1, koplRemoteName, ttNil, koplAssign),
    _TomeCore(ttAShr, fopAssign | fopOp, kopShr, koplAssign + 1, koplRemoteName, ttNil, koplAssign),
    _TomeCore(ttAShl, fopAssign | fopOp, kopShl, koplAssign + 1, koplRemoteName, ttNil, koplAssign),

    _TomeCore(ttQuery, fopOp, opNil, koplLOr, koplLOr, ttNil, oplNil),
    _TomeCore(ttColon, fopOp, opNil, koplComma, koplComma, ttQuery, koplColon), _TomeRes(ttComma, opNil, koplComma),
    _TomeCore(ttArrow, fopOp, opNil, koplGroup, koplGroup, ttNil, koplRemoteName),
    _TomeCore(ttDollar, fopNil, opNil, koplName, koplName, ttNil, koplNumber),
    _TomeCore(ttAt, fopNil, opNil, koplComma, koplName, ttNil, koplComma),

    /* end of list marker */
    _TomeRes(ttNil, opNil, oplNil)};

/* following an operator */
static const OperatorToken operators_before_operand[] = {
    _TomeCore(ttName, fopNil, opNil, oplNil, oplNil, ttNil, koplName),
    _TomeCore(ttLong, fopNil, opNil, oplNil, oplNil, ttNil, koplNumber),
    _TomeCore(ttString, fopNil, opNil, oplNil, oplNil, ttNil, koplNumber),
    _TomeCore(ttOpenParen, fopOp, opNil, oplNil, oplNil, ttNil, oplNil),            /* grouping */
    _TomeCore(ttCloseParen, fopNil, opNil, oplNil, oplNil, ttOpenParen, koplGroup), /* empty group */

    _TomePush(ttSub, kopNeg, koplPreUn), _TomePush(ttLNot, kopLNot, koplPreUn), _TomePush(ttBNot, kopBNot, koplPreUn),
    _TomeCore(ttInc, fopOp | fopAssign, kopInc, oplNil, oplNil, ttNil, koplPreUn),
    _TomeCore(ttDec, fopOp | fopAssign, kopDec, oplNil, oplNil, ttNil, koplPreUn), _TomePush(ttAdd, opNil, koplPreUn),

    _TomePush(ttDot, opNil, koplThisName), _TomePush(ttScope, opNil, koplGlobalName),

    /* end of list marker */
    _TomeRes(ttNil, opNil, oplNil)};

/***************************************************************************
    Find the TOME corresponding to this token and fOp.
***************************************************************************/
static const OperatorToken *find_operator(MMI32 token, int needs_operand)
{
    const OperatorToken *entry;

    for (entry = needs_operand ? operators_before_operand : operators_after_operand; ttNil != entry->tt; ++entry)
    {
        if (token == entry->tt)
            return entry;
    }
    return NULL;
}

/***************************************************************************
    Resolve the Expression stack to something at the given opl.
***************************************************************************/
static int resolve_precedence(InfixCompiler *compiler, MMI32 opl, MMI32 oplMin, MMI32 *pietn)
{
    Expression etn, etnT;
    MMI32 ietn, cetn;

    array_read(&compiler->operators, compiler->operators.array.count - 1, &etn);
    if (compiler->operators.array.count <= 1 || (etn.grfop & fopOp))
    {
        report(compiler, MM_SCRIPT_COMPILE_SYNTAX);
        return 0;
    }

    for (;;)
    {
        cetn = compiler->operators.array.count;
        array_read(&compiler->operators, cetn - 2, &etn);
        if (etn.opl < opl)
            break;

        array_pop(&compiler->operators, &etnT);
        if (!add_expression(compiler, &etnT, &ietn))
        {
            report(compiler, MM_SCRIPT_COMPILE_MEMORY);
            return 0;
        }
        if (ietn == NO_NODE)
            return 0;

        if (etn.ietn1 == NO_NODE)
            etn.ietn1 = ietn;
        else if (etn.ietn2 == NO_NODE)
            etn.ietn2 = ietn;
        else
        {
            etn.ietn3 = ietn;
        }
        etn.grfop &= ~fopOp;
        array_write(&compiler->operators, cetn - 2, &etn);
    }

    array_pop(&compiler->operators, &etnT);
    if (etnT.opl < oplMin)
    {
        report(compiler, MM_SCRIPT_COMPILE_SYNTAX);
        return 0;
    }
    if (!add_expression(compiler, &etnT, pietn))
    {
        report(compiler, MM_SCRIPT_COMPILE_MEMORY);
        return 0;
    }

    return *pietn != NO_NODE;
}

/***************************************************************************
    See if we can simplify the expression because of constant operands and
    other optimizations, then add it to the tree.  Most of this routine
    is not necessary for non-optimized compilation.  This routine could
    be simplified to nuke () groupings and unary plus operators and just
    add the rest to the expression tree.
***************************************************************************/
static int add_expression(InfixCompiler *compiler, Expression *petn, MMI32 *pietn)
{
    MMI32 lw1, lw2, lw;
    int fConst1, fConst2;
    Expression etnT1, etnT2, etn;
    MMI32 ietnConst;
    int fCommute = 0;


    /* get rid of non-function call groupings - they aren't necessary in */
    /* the parse tree. */
    if (petn->op == opNil && petn->tt == ttCloseParen)
    {
        if (petn->grfop & fopFunction)
            goto LAdd;
        if (petn->ietn1 == NO_NODE)
        {
            report(compiler, MM_SCRIPT_COMPILE_SYNTAX);
            *pietn = NO_NODE;
            return 1;
        }
        *pietn = petn->ietn1;
        return 1;
    }

    if (petn->ietn3 != NO_NODE || petn->ietn1 == NO_NODE)
        goto LAdd;

    /* one or two operands */
    fConst1 = constant_at(compiler, petn->ietn1, &lw1);
    if (petn->ietn2 == NO_NODE)
    {
        /* unary operators */

        /* nuke unary plus operator */
        if (ttAdd == petn->tt && opNil == petn->op)
        {
            *pietn = petn->ietn1;
            return 1;
        }

        if (!fConst1)
            goto LAdd;

        /* optimize unary operators with constant operand */
        switch (petn->op)
        {
        default:
            goto LAdd;

        case kopNeg:
            lw = mm_signed_word(0U - (MMU32)lw1);
            break;
        case kopBNot:
            lw = mm_signed_word(~(MMU32)lw1);
            break;
        case kopLNot:
            lw = !lw1;
            break;
        }
        *pietn = ietnConst = petn->ietn1;
        goto LSetConst;
    }

    /* two operands - determine if they are constants */
    fConst2 = constant_at(compiler, petn->ietn2, &lw2);
    if (fConst1 && fConst2)
    {
        /* optimize binary operators with both operands constants */
        if (petn->op == opNil)
        {
            switch (petn->tt)
            {
            default:
                goto LAdd;
            case ttLOr:
                lw = lw1 || lw2;
                break;
            case ttLAnd:
                lw = lw1 && lw2;
                break;
            }
        }
        else if (!fold_binary(compiler, petn->op, lw1, lw2, &lw))
            goto LAdd;

        *pietn = ietnConst = petn->ietn1;
        goto LSetConst;
    }

    /* try to combine operand subtrees for the binary operators that are */
    /* commutative and associative */
    switch (petn->op)
    {
    default:
        break;

    case kopSub:
        if (!fConst2)
            break;

        /* the second argument is constant, but the first is not, so negate the */
        /* constant and change the operator to kopAdd */
        array_read(compiler->tree, petn->ietn2, &etnT2);
        etnT2.lwValue = mm_signed_word(0U - (MMU32)etnT2.lwValue);
        array_write(compiler->tree, petn->ietn2, &etnT2);
        petn->op = kopAdd;
        /* fall through */
    case kopAdd:
    case kopMul:
    case kopBOr:
    case kopBAnd:
    case kopBXor:
    case kopLXor:
        /* get the two etn's */
        fCommute = 1;
        array_read(compiler->tree, petn->ietn1, &etnT1);
        array_read(compiler->tree, petn->ietn2, &etnT2);

        if (fConst1)
        {
            /* first operand is constant, so swap them */
            swap_words(&petn->ietn1, &petn->ietn2);
            swap_flags(&fConst1, &fConst2);
            swap_expressions(&etnT1, &etnT2);
        }

        if (fConst2)
        {
            /* the second is constant */
            /* see if the first has the same op as petn */
            if (etnT1.op != petn->op)
                break;

            /* see if the first one has a constant second operand */
            if (!constant_at(compiler, etnT1.ietn2, &lw1))
                break;

            /* all we have to do is modify the first one's second operand */
            fold_binary(compiler, petn->op, lw1, etnT2.lwValue, &lw);
            *pietn = petn->ietn1;
            ietnConst = etnT1.ietn2;
            goto LSetConst;
        }

        /* neither operand is constant */

        /* see if the right operands of the operands are constant */
        fConst1 = (etnT1.op == petn->op) && constant_at(compiler, etnT1.ietn2, &lw1);
        fConst2 = (etnT2.op == petn->op) && constant_at(compiler, etnT2.ietn2, &lw2);

        if (fConst1 && fConst2)
        {
            /* both are constant */
            fold_binary(compiler, petn->op, lw1, lw2, &lw);
            *pietn = petn->ietn2;
            etnT1.ietn2 = etnT2.ietn1;
            etnT2.ietn1 = petn->ietn1;
            set_depth(compiler, &etnT1, 1);
            array_write(compiler->tree, petn->ietn1, &etnT1);
            ietnConst = etnT2.ietn2;
            set_depth(compiler, &etnT2, 0);
            array_write(compiler->tree, petn->ietn2, &etnT2);
        LSetConst:
            array_read(compiler->tree, ietnConst, &etn);
            etn.lwValue = lw;
            array_write(compiler->tree, ietnConst, &etn);
            return 1;
        }

        if (fConst1)
        {
            /* only the first is constant */
            swap_words(&etnT1.ietn2, &petn->ietn2);
            set_depth(compiler, &etnT1, 1);
            array_write(compiler->tree, petn->ietn1, &etnT1);
            break;
        }

        if (fConst2)
        {
            /* only the second is constant */
            goto LPivot;
        }

        /* neither is constant */
        if ((etnT1.op == petn->op) == (etnT2.op == petn->op))
        {
            /* both the same as petn->op or both different, let _SetDepth */
            /* do its thing */
            break;
        }

        if (etnT1.op == petn->op)
        {
            if (etnT2.ietn1 == NO_NODE)
                break;
            /* swap them */
            swap_words(&petn->ietn1, &petn->ietn2);
            swap_expressions(&etnT1, &etnT2);
        }

        /* determine if we want to pivot or swap */
        array_read(compiler->tree, etnT2.ietn2, &etn);
        if (etn.cetnDeep > etnT1.cetnDeep)
        {
            /* swap them */
            swap_words(&petn->ietn1, &petn->ietn2);
        }
        else
        {
        LPivot:
            /* pivot */
            etn = *petn;
            etn.ietn2 = etnT2.ietn2;
            etn.ietn1 = petn->ietn2;
            etnT2.ietn2 = etnT2.ietn1;
            etnT2.ietn1 = petn->ietn1;
            set_depth(compiler, &etnT2, 1);
            array_write(compiler->tree, petn->ietn2, &etnT2);
            petn = &etn;
        }
        break;
    }

LAdd:
    set_depth(compiler, petn, fCommute);
    return append_expression(compiler, petn, pietn);
}

/***************************************************************************
    Set the depth of the Expression from its children.  If fCommute is true and
    the first two children are non-nil, puts the deepest child first.
***************************************************************************/
static void set_depth(InfixCompiler *compiler, Expression *petn, int fCommute)
{
    Expression etn1, etn2;

    if (fCommute && petn->ietn1 != NO_NODE && petn->ietn2 != NO_NODE)
    {
        /* put the deeper guy on the left */
        array_read(compiler->tree, petn->ietn1, &etn1);
        array_read(compiler->tree, petn->ietn2, &etn2);
        if (etn1.cetnDeep < etn2.cetnDeep ||
            (etn1.cetnDeep == etn2.cetnDeep && etn1.op == petn->op && etn2.op != petn->op))
        {
            /* swap them */
            swap_words(&petn->ietn1, &petn->ietn2);
            swap_expressions(&etn1, &etn2);
        }
        /* put as much stuff on the left as we can */
        while (etn2.op == petn->op)
        {
            etn1 = etn2;
            etn1.ietn1 = petn->ietn1;
            etn1.ietn2 = etn2.ietn1;
            petn->ietn1 = petn->ietn2;
            petn->ietn2 = etn2.ietn2;
            set_depth(compiler, &etn1, 1);
            array_write(compiler->tree, petn->ietn1, &etn1);
            array_read(compiler->tree, petn->ietn2, &etn2);
        }
    }

    petn->cetnDeep = 1;
    if (petn->ietn1 != NO_NODE)
    {
        array_read(compiler->tree, petn->ietn1, &etn1);
        petn->cetnDeep = max_word(petn->cetnDeep, etn1.cetnDeep + 1);
        if (petn->ietn2 != NO_NODE)
        {
            array_read(compiler->tree, petn->ietn2, &etn2);
            petn->cetnDeep = max_word(petn->cetnDeep, etn2.cetnDeep + 1);
            if (petn->ietn3 != NO_NODE)
            {
                array_read(compiler->tree, petn->ietn3, &etn1);
                petn->cetnDeep = max_word(petn->cetnDeep, etn1.cetnDeep + 1);
            }
        }
    }
}

/***************************************************************************
    Return true iff the given etn is constant.  If so, set *plw to its
    value.
***************************************************************************/
static int constant_at(InfixCompiler *compiler, MMI32 ietn, MMI32 *plw)
{
    Expression etn;

    if (ietn != NO_NODE)
    {
        array_read(compiler->tree, ietn, &etn);
        if (etn.op == opNil && etn.tt == ttLong)
        {
            *plw = etn.lwValue;
            return 1;
        }
    }
    return 0;
}

/***************************************************************************
    Combine the constant values lw1 and lw2 using the given operator.
    Put the result in *plw.  Return false if we can't combine the values
    using op.
***************************************************************************/
static int fold_binary(InfixCompiler *compiler, MMI32 op, MMI32 lw1, MMI32 lw2, MMI32 *plw)
{

    switch (op)
    {
    default:
        return 0;

    case kopAdd:
        *plw = mm_signed_word((MMU32)lw1 + (MMU32)lw2);
        break;
    case kopSub:
        *plw = mm_signed_word((MMU32)lw1 - (MMU32)lw2);
        break;
    case kopMul:
        *plw = mm_signed_word((MMU32)lw1 * (MMU32)lw2);
        break;
    case kopDiv:
    case kopMod:
        if (lw2 == 0)
        {
            report(compiler, MM_SCRIPT_COMPILE_DIVIDE_BY_ZERO);
            *plw = 0;
        }
        else if (lw1 == (-2147483647 - 1) && lw2 == -1)
        {
            report(compiler, MM_SCRIPT_COMPILE_DIVISION_OVERFLOW);
            *plw = 0;
        }
        else
            *plw = divide_constants(op, lw1, lw2);
        break;
    case kopShr:
        *plw = mm_signed_word((MMU32)lw1 >> ((MMU32)lw2 & 31U));
        break;
    case kopShl:
        *plw = mm_signed_word((MMU32)lw1 << ((MMU32)lw2 & 31U));
        break;
    case kopBOr:
        *plw = mm_signed_word((MMU32)lw1 | (MMU32)lw2);
        break;
    case kopBAnd:
        *plw = mm_signed_word((MMU32)lw1 & (MMU32)lw2);
        break;
    case kopBXor:
        *plw = mm_signed_word((MMU32)lw1 ^ (MMU32)lw2);
        break;
    case kopLXor:
        *plw = (lw1 != 0) != (lw2 != 0);
        break;
    case kopEq:
        *plw = lw1 == lw2;
        break;
    case kopNe:
        *plw = lw1 != lw2;
        break;
    case kopGt:
        *plw = lw1 > lw2;
        break;
    case kopLt:
        *plw = lw1 < lw2;
        break;
    case kopGe:
        *plw = lw1 >= lw2;
        break;
    case kopLe:
        *plw = lw1 <= lw2;
        break;
    }

    return 1;
}

/***************************************************************************
    Emit code for the expression tree with top at ietnTop.  If pclwArg
    is not nil, we are pushing function arguments, so the comma operator
    has to keep the values of both its operands.  Otherwise, the comma
    operator discards the value of its left operand.

    Values of grfscc include fsccTop, indicating whether a control structure
    primitive is legal, and fsccWantVoid, indicating whether the emitted
    code should (not) leave a value on the execution stack.  If fsccTop
    is set, fsccWantVoid should also be set.

    This is highly recursive, so limit the stack space needed.
***************************************************************************/
static void emit_expression(InfixCompiler *compiler, MMI32 ietnTop, MMU32 grfscc, MMI32 *pclwArg)
{
    Expression etn;
    VariableName rtvn;
    MMI32 opPush, opPop;
    MMI32 clwStack;
    MMI32 lw1, lw2;
    MMI32 cst;

    if (!array_read(compiler->tree, ietnTop, &etn))
    {
        report(compiler, MM_SCRIPT_COMPILE_SYNTAX);
        return;
    }

    /* all non-nil operands should come before any nils */

    if (etn.grfop & fopAssign)
    {
        /* an operator that changes the value of a variable */

        /* if we're doing an operation before assigning (eg, +=), */
        /* push the variable first */
        if (etn.op != opNil)
        {
            emit_variable_access(compiler, etn.ietn1, &rtvn, &opPush, &opPop, &clwStack);
            if (clwStack == 1)
                mm_script_emit_opcode(compiler->emitter, kopDup); /* duplicate the variable access id */
            else if (clwStack > 0)
            {
                mm_script_emit_immediate(compiler->emitter, clwStack);
                mm_script_emit_opcode(compiler->emitter, kopDupList);
            }
            mm_script_emit_variable(compiler->emitter, opPush, rtvn.high, rtvn.low);
        }

        /* generate the second operand */
        if (etn.ietn2 != NO_NODE)
            emit_expression(compiler, etn.ietn2, fsccNil, NULL);

        /* do the operation (if not post-inc or post-dec) */
        if (etn.op != opNil && !(etn.grfop & fopPostAssign))
            mm_script_emit_opcode(compiler->emitter, etn.op);

        /* duplicate the result */
        if (!(grfscc & fsccWantVoid))
            mm_script_emit_opcode(compiler->emitter, kopDup);

        /* do the operation if post-inc or post-dec */
        if (etn.op != opNil && (etn.grfop & fopPostAssign))
            mm_script_emit_opcode(compiler->emitter, etn.op);

        /* pop the result into the variable */
        if (etn.op == opNil)
            emit_variable_access(compiler, etn.ietn1, &rtvn, NULL, &opPop, NULL);
        else if (clwStack == 1)
        {
            /* need to bring the variable access result to the top */
            if (grfscc & fsccWantVoid)
                mm_script_emit_opcode(compiler->emitter, kopSwap);
            else
            {
                /* top two items on the stack are the result */
                mm_script_emit_immediate(compiler->emitter, 3);
                mm_script_emit_opcode(compiler->emitter, kopRev);
            }
        }
        else if (clwStack > 0)
        {
            /* top one or two values are the result, next */
            /* clwStack values are the variable access values. */
            mm_script_emit_immediate(compiler->emitter, clwStack + 1 + !(grfscc & fsccWantVoid));
            mm_script_emit_immediate(compiler->emitter, clwStack);
            mm_script_emit_opcode(compiler->emitter, kopRot);
        }

        mm_script_emit_variable(compiler->emitter, opPop, rtvn.high, rtvn.low);
        return;
    }

    if (opNil != etn.op)
    {
        if (NO_NODE != etn.ietn1)
        {
            emit_expression(compiler, etn.ietn1, fsccNil, NULL);
            if (NO_NODE != etn.ietn2)
            {
                emit_expression(compiler, etn.ietn2, fsccNil, NULL);
                if (NO_NODE != etn.ietn3)
                    emit_expression(compiler, etn.ietn3, fsccNil, NULL);
            }
        }
        mm_script_emit_opcode(compiler->emitter, etn.op);
        if (grfscc & fsccWantVoid)
            mm_script_emit_opcode(compiler->emitter, kopPop);
        return;
    }

    /* special cases */
    switch (etn.tt)
    {
    default:
        report(compiler, MM_SCRIPT_COMPILE_SYNTAX);
        break;
    case ttName:
        /* first check for a control structure keyword */
        if (emit_control_keyword(compiler, ietnTop, grfscc))
            break;
        /* fall through */
    case ttCloseRef:
    case ttDot:
    case ttScope:
    case ttArrow:
        /* handle pushing a variable */
        emit_variable_access(compiler, ietnTop, &rtvn, &opPush, NULL, NULL);
        mm_script_emit_variable(compiler->emitter, opPush, rtvn.high, rtvn.low);
        if (grfscc & fsccWantVoid)
            mm_script_emit_opcode(compiler->emitter, kopPop);
        break;
    case ttLong:
        /* push a constant */
        if (!(grfscc & fsccWantVoid))
            mm_script_emit_immediate(compiler->emitter, etn.lwValue);
        break;
    case ttString:
        /* "push" a constant string */
        if (!(grfscc & fsccWantVoid))
            emit_string(compiler, etn.lwValue);
        break;
    case ttDollar:
        /* label reference */
        if (!(grfscc & fsccWantVoid))
            emit_named_label(compiler, etn.ietn1, 0);
        break;
    case ttAt:
        /* label declaration */
        emit_named_label(compiler, etn.ietn1, 1);
        break;
    case ttCloseParen:
        /* a function call or control structure */

        if ((grfscc & fsccTop) && cstNil != (cst = conditional_kind(compiler, etn.ietn1)))
        {
            /* control structure */
            begin_control(compiler, cst, etn.ietn2);
        }
        else
        {
            /* get the arguments and count them */
            MMI32 clwArg;

            if (etn.ietn2 == NO_NODE)
                clwArg = 0;
            else
            {
                clwArg = 1;
                emit_expression(compiler, etn.ietn2, fsccNil, &clwArg);
            }
            emit_function(compiler, etn.ietn1, grfscc, clwArg);
        }
        break;
    case ttComma:
        if (NULL != pclwArg)
        {
            /* we're pushing function arguments */

            /* push function arguments from right to left! */
            emit_expression(compiler, etn.ietn2, fsccNil, pclwArg);
            (*pclwArg)++;
            emit_expression(compiler, etn.ietn1, fsccNil, pclwArg);
        }
        else
        {
            emit_expression(compiler, etn.ietn1, fsccWantVoid, NULL);
            emit_expression(compiler, etn.ietn2, grfscc & ~fsccTop, NULL);
        }
        break;
    case ttColon:
        /* ternary operator */
        internal_label(compiler, lw1 = ++compiler->next_label, 0);
        emit_expression(compiler, etn.ietn1, fsccNil, NULL);
        mm_script_emit_opcode(compiler->emitter, kopGoNz);
        emit_expression(compiler, etn.ietn3, grfscc & ~fsccTop, NULL);
        internal_label(compiler, lw2 = ++compiler->next_label, 0);
        mm_script_emit_opcode(compiler->emitter, kopGo);
        internal_label(compiler, lw1, 1);
        emit_expression(compiler, etn.ietn2, grfscc & ~fsccTop, NULL);
        internal_label(compiler, lw2, 1);
        break;
    case ttLOr:
        /* logical or - handles short circuiting */
        if (!(grfscc & fsccWantVoid))
            mm_script_emit_immediate(compiler->emitter, 1); /* assume true */
        internal_label(compiler, lw1 = ++compiler->next_label, 0);
        emit_expression(compiler, etn.ietn1, fsccNil, NULL);
        mm_script_emit_opcode(compiler->emitter, kopGoNz);
        if (!(grfscc & fsccWantVoid))
            internal_label(compiler, lw1, 0);
        emit_expression(compiler, etn.ietn2, grfscc & ~fsccTop, NULL);
        if (!(grfscc & fsccWantVoid))
        {
            mm_script_emit_opcode(compiler->emitter, kopGoNz);
            mm_script_emit_opcode(compiler->emitter, kopDec);
        }
        internal_label(compiler, lw1, 1);
        break;
    case ttLAnd:
        /* logical and - handles short circuiting */
        if (!(grfscc & fsccWantVoid))
            mm_script_emit_immediate(compiler->emitter, 0); /* assume false */
        internal_label(compiler, lw1 = ++compiler->next_label, 0);
        emit_expression(compiler, etn.ietn1, fsccNil, NULL);
        mm_script_emit_opcode(compiler->emitter, kopGoZ);
        if (!(grfscc & fsccWantVoid))
            internal_label(compiler, lw1, 0);
        emit_expression(compiler, etn.ietn2, grfscc & ~fsccTop, NULL);
        if (!(grfscc & fsccWantVoid))
        {
            mm_script_emit_opcode(compiler->emitter, kopGoZ);
            mm_script_emit_opcode(compiler->emitter, kopInc);
        }
        internal_label(compiler, lw1, 1);
        break;
    }
}

/***************************************************************************
    If the etn is for a control structure, return the cst.
***************************************************************************/
static MMI32 conditional_kind(InfixCompiler *compiler, MMI32 node)
{
    switch (keyword(compiler, node_name(compiler, node)))
    {
    case KEY_IF: return cstIf;
    case KEY_ELIF: return cstElif;
    case KEY_WHILE: return cstWhile;
    default: return cstNil;
    }
}

/***************************************************************************
    Begin a new control structure.
***************************************************************************/
static void begin_control(InfixCompiler *compiler, MMI32 cst, MMI32 ietn)
{
    ControlFrame cstd;
    ControlFrame cstdPrev;

    memset(&cstd, 0, sizeof(cstd));
    cstd.cst = cst;
    cstd.lwLabel1 = ++compiler->next_label;
    switch (cst)
    {
    default:
        return;

    case cstWhile:
        /* we do jump optimized loops (putting the conditional at the end). */
        /* label 1 is where Continue jumps to */
        cstd.ietnTop = ietn;
        if (ietn == NO_NODE)
        {
            /* no conditional expression, an "infinite" loop */
            internal_label(compiler, cstd.lwLabel1, 1);
        }
        else
        {
            cstd.condition = clone_tree(compiler->tree);
            if (!cstd.condition)
            {
                report(compiler, MM_SCRIPT_COMPILE_MEMORY);
                return;
            }

            /* jump to where the condition will be */
            internal_label(compiler, cstd.lwLabel1, 0);
            mm_script_emit_opcode(compiler->emitter, kopGo);

            /* add the label for the top of the loop */
            cstd.lwLabel2 = ++compiler->next_label;
            internal_label(compiler, cstd.lwLabel2, 1);
        }
        break;

    case cstIf:
        /* label 1 is used for where to jump if the condition fails */
        goto LTest;

    case cstElif:
        /* label 1 is used for where to jump if the condition fails */
        /* label 2 is used for the end of the entire if block */
        cstd.cst = cstIf;
        if (array_pop(&compiler->controls, &cstdPrev))
        {
            if (cstdPrev.cst != cstIf)
            {
                if (!array_push(&compiler->controls, &cstdPrev))
                {
                    destroy_tree(cstdPrev.condition);
                    report(compiler, MM_SCRIPT_COMPILE_MEMORY);
                    return;
                }
                goto LError;
            }
        }
        else
        {
        LError:
            report(compiler, MM_SCRIPT_COMPILE_UNEXPECTED_ELIF);
            memset(&cstdPrev, 0, sizeof(cstdPrev));
            cstdPrev.cst = cstIf;
            cstdPrev.lwLabel1 = ++compiler->next_label;
        }

        /* emit code for jumping to the end of the if block */
        cstd.lwLabel2 = cstdPrev.lwLabel2;
        if (0 == cstd.lwLabel2)
            cstd.lwLabel2 = ++compiler->next_label;
        internal_label(compiler, cstd.lwLabel2, 0);
        mm_script_emit_opcode(compiler->emitter, kopGo);

        /* add label that previous condition jumps to on failure */
        internal_label(compiler, cstdPrev.lwLabel1, 1);

    LTest:
        /* emit code for the expression testing */
        internal_label(compiler, cstd.lwLabel1, 0);
        if (ietn == NO_NODE)
            report(compiler, MM_SCRIPT_COMPILE_SYNTAX);
        else
            emit_expression(compiler, ietn, fsccNil, NULL);
        mm_script_emit_opcode(compiler->emitter, kopGoZ);
        break;
    }

    /* put the cstd on the stack */
    if (!array_push(&compiler->controls, &cstd))
    {
        destroy_tree(cstd.condition);
        report(compiler, MM_SCRIPT_COMPILE_MEMORY);
    }
}

/***************************************************************************
    If this name token is "End", "Break", "Continue" or "Else", deal with it
    and return true.
***************************************************************************/
static int emit_control_keyword(InfixCompiler *compiler, MMI32 node, MMU32 flags)
{
    int key = keyword(compiler, node_name(compiler, node));
    ControlFrame cstd;
    MMI32 icstd;
    MMI32 *plwLabel;
    CompileArray *saved;
    if (key < KEY_ELSE || key == KEY_WHILE)
        return 0;
    if (!(flags & fsccTop))
    {
        report(compiler, MM_SCRIPT_COMPILE_SYNTAX);
        return 1;
    }

    if (key == KEY_ELSE)
    {
        /* an Else */
        if (0 == (icstd = compiler->controls.array.count))
        {
            report(compiler, MM_SCRIPT_COMPILE_UNEXPECTED_ELSE);
            return 1;
        }
        array_read(&compiler->controls, --icstd, &cstd);
        if (cstd.cst != cstIf)
        {
            report(compiler, MM_SCRIPT_COMPILE_UNEXPECTED_ELSE);
            return 1;
        }

        /* emit code to jump to the end of the if block */
        if (0 == cstd.lwLabel2)
            cstd.lwLabel2 = ++compiler->next_label;
        internal_label(compiler, cstd.lwLabel2, 0);
        mm_script_emit_opcode(compiler->emitter, kopGo);

        /* add label that previous condition jumps to on failure */
        internal_label(compiler, cstd.lwLabel1, 1);

        cstd.cst = cstNil; /* so we don't accept another else */
        cstd.lwLabel1 = cstd.lwLabel2;
        array_write(&compiler->controls, icstd, &cstd);
        return 1;
    }

    if (key == KEY_BREAK)
    {
        /* a Break - label 3 is where a Break jumps to */
        plwLabel = &cstd.lwLabel3;
        goto LWhileJump;
    }

    if (key == KEY_CONTINUE)
    {
        /* a Continue - label 1 is where a Continue jumps to */
        plwLabel = &cstd.lwLabel1;

    LWhileJump:
        /* find the enclosing While */
        for (icstd = compiler->controls.array.count;;)
        {
            if (icstd-- <= 0)
            {
                if (plwLabel == &cstd.lwLabel1) report(compiler, MM_SCRIPT_COMPILE_UNEXPECTED_CONTINUE);
                else report(compiler, MM_SCRIPT_COMPILE_UNEXPECTED_BREAK);
                return 1;
            }
            array_read(&compiler->controls, icstd, &cstd);
            if (cstd.cst == cstWhile)
                break;
        }

        if (*plwLabel == 0)
        {
            *plwLabel = ++compiler->next_label;
            array_write(&compiler->controls, icstd, &cstd);
        }
        internal_label(compiler, *plwLabel, 0);
        mm_script_emit_opcode(compiler->emitter, kopGo);
        return 1;
    }

    if (key == KEY_END)
    {
        /* an End */
        if (!array_pop(&compiler->controls, &cstd))
        {
            report(compiler, MM_SCRIPT_COMPILE_UNEXPECTED_END);
            return 1;
        }

        switch (cstd.cst)
        {
        case cstWhile:
            if (cstd.ietnTop == NO_NODE)
            {
                /* an "infinite" loop - jump to the top */
                internal_label(compiler, cstd.lwLabel1, 0);
                mm_script_emit_opcode(compiler->emitter, kopGo);
            }
            else
            {
                internal_label(compiler, cstd.lwLabel1, 1);

                /* emit code for the expression testing */
                internal_label(compiler, cstd.lwLabel2, 0);
                saved = compiler->tree;
                compiler->tree = cstd.condition;
                emit_expression(compiler, cstd.ietnTop, fsccNil, NULL);
                compiler->tree = saved;
                destroy_tree(cstd.condition);
                mm_script_emit_opcode(compiler->emitter, kopGoNz);
            }

            /* add the Break label if it was used */
            if (cstd.lwLabel3 != 0)
                internal_label(compiler, cstd.lwLabel3, 1);
            break;

        case cstIf:
            if (cstd.lwLabel2 != 0)
                internal_label(compiler, cstd.lwLabel2, 1);
            internal_label(compiler, cstd.lwLabel1, 1);
            break;

        default:
            internal_label(compiler, cstd.lwLabel1, 1);
            break;
        }

        return 1;
    }

    return 0;
}

/***************************************************************************
    Generate the code that handles remote variable access and fill
    *popPush and *popPop with the appropriate op-codes (kopPushLocVar, etc).
    Sets *pclwStack to the number of longs on the stack used to access the
    variable (iff the variable is a remote variable or an array access).
    Fills in *prtvn.
***************************************************************************/
static void emit_variable_access(InfixCompiler *compiler, MMI32 ietn, VariableName *prtvn, MMI32 *popPush, MMI32 *popPop, MMI32 *pclwStack)
{
    Expression etn;
    MMI32 opPop, opPush;
    MMI32 clwStack = 0;

    array_read(compiler->tree, ietn, &etn);
    switch (etn.tt)
    {
    default:
        opPop = kopPopLocVar;
        opPush = kopPushLocVar;
        break;

    case ttDot:
        ietn = etn.ietn1;
        opPop = kopPopThisVar;
        opPush = kopPushThisVar;
        break;

    case ttScope:
        ietn = etn.ietn1;
        opPop = kopPopGlobalVar;
        opPush = kopPushGlobalVar;
        break;

    case ttArrow:
        emit_expression(compiler, etn.ietn1, fsccNil, NULL);
        clwStack++;
        ietn = etn.ietn2;
        opPop = kopPopRemoteVar;
        opPush = kopPushRemoteVar;
        break;
    }

    array_read(compiler->tree, ietn, &etn);
    if (etn.tt == ttCloseRef)
    {
        /* an array reference */
        emit_expression(compiler, etn.ietn2, fsccNil, NULL);
        clwStack++;
        ietn = etn.ietn1;
        opPop += kopPopLocArray - kopPopLocVar;
        opPush += kopPushLocArray - kopPushLocVar;
    }

    variable_name(compiler, node_name(compiler, ietn), prtvn);

    if (NULL != pclwStack)
        *pclwStack = clwStack;
    if (NULL != popPush)
        *popPush = opPush;
    if (NULL != popPop)
        *popPop = opPop;
}

/***************************************************************************
    Push the opcode for a function and verify parameters and return type.
***************************************************************************/
static void emit_function(InfixCompiler *compiler, MMI32 node, MMU32 flags, MMI32 arguments)
{
    CompileName name = node_name(compiler, node);
    const MMScriptFunction *function = mm_script_find_function(compiler->input->dialect, name.text, name.length,
                                                               compiler->names.unit_bytes);

    if (!function)
        report(compiler, MM_SCRIPT_COMPILE_UNKNOWN_FUNCTION);
    else if (arguments < function->fixed ||
             (function->group == 0 && arguments != function->fixed) ||
             (function->group != 0 && ((arguments - function->fixed) % function->group != 0 ||
              (arguments - function->fixed) / function->group < function->minimum_groups)))
        report(compiler, MM_SCRIPT_COMPILE_ARGUMENT_COUNT);
    else if (!(flags & fsccWantVoid) && function->returns_void)
        report(compiler, MM_SCRIPT_COMPILE_VOID_VALUE);
    else
    {
        if (function->group > 0)
            mm_script_emit_immediate(compiler->emitter, (arguments - function->fixed) / function->group);
        mm_script_emit_opcode(compiler->emitter, function->opcode);
        if ((flags & fsccWantVoid) && !function->returns_void)
            mm_script_emit_opcode(compiler->emitter, kopPop);
    }
}

static int next_token(InfixCompiler *compiler, MMScriptToken *token)
{
    if (compiler->eof)
        return 0;
    if (compiler->input->next(compiler->input->context, token))
        return 1;
    compiler->eof = 1;
    return 0;
}

static void finish_statement(InfixCompiler *compiler)
{
    MMI32 root;
    if (compiler->operators.array.count > 1 && resolve_precedence(compiler, koplComma, koplComma, &root))
    {
        if (compiler->operators.array.count != 1)
            report(compiler, MM_SCRIPT_COMPILE_SYNTAX);
        else
            emit_expression(compiler, root, fsccTop | fsccWantVoid, NULL);
    }
    mm_array_set_count(&compiler->operators.array, 1);
    mm_array_set_count(&compiler->tree->array, 0);
}

static int parse_token(InfixCompiler *compiler, const MMScriptToken *token)
{
    Expression node, previous;
    const OperatorToken *rule;
    MMI32 root;
    array_read(&compiler->operators, compiler->operators.array.count - 1, &previous);
    rule = find_operator(token->kind, previous.grfop & fopOp);
    if (!rule)
    {
        report(compiler, MM_SCRIPT_COMPILE_SYNTAX);
        return 0;
    }

    memset(&node, 0, sizeof(node));
    node.tt = (short)token->kind;
    node.lwValue = token->value;
    node.op = rule->op;
    node.opl = rule->oplPush;
    node.grfop = rule->grfop;
    node.ietn1 = node.ietn2 = node.ietn3 = NO_NODE;
    if (token->kind == ttName || token->kind == ttString)
        node.lwValue = intern_name(compiler, token->text, token->length);

    if (rule->oplResolve != oplNil &&
        !resolve_precedence(compiler, rule->oplResolve, rule->oplMinRes, &node.ietn1))
        return 0;
    if (rule->ttPop != ttNil)
    {
        if (compiler->operators.array.count == 1 || !array_pop(&compiler->operators, &previous) ||
            previous.tt != rule->ttPop || !(previous.grfop & fopOp))
        {
            report(compiler, MM_SCRIPT_COMPILE_SYNTAX);
            return 0;
        }
        if (previous.ietn1 != NO_NODE)
        {
            node.ietn2 = node.ietn1;
            node.ietn1 = previous.ietn1;
            node.grfop |= previous.grfop & fopFunction;
        }
    }

    if (!array_push(&compiler->operators, &node))
    {
        report(compiler, MM_SCRIPT_COMPILE_MEMORY);
        return 0;
    }
    if (token->kind == ttAt)
    {
        if (!resolve_precedence(compiler, koplComma, koplComma, &root))
            return 0;
        if (compiler->operators.array.count != 1)
        {
            report(compiler, MM_SCRIPT_COMPILE_SYNTAX);
            return 0;
        }
        emit_expression(compiler, root, fsccTop | fsccWantVoid, NULL);
        mm_array_set_count(&compiler->tree->array, 0);
    }
    return 1;
}

void mm_script_compile_infix(MMScriptEmitter *emitter, const MMScriptInput *input)
{
    const MMScriptHost *host;
    InfixCompiler compiler;
    Expression sentinel;
    ControlFrame frame;
    MMScriptToken token;
    if (!emitter)
    {
        while (input->next(input->context, &token))
            ;
        return;
    }

    host = mm_script_emitter_host(emitter);
    memset(&compiler, 0, sizeof(compiler));
    memset(&sentinel, 0, sizeof(sentinel));
    sentinel.grfop = fopOp;
    compiler.emitter = emitter;
    compiler.input = input;
    compiler.tree = &compiler.expressions;
    if (!array_init(&compiler.expressions, host->storage, sizeof(Expression), 100) ||
        !array_init(&compiler.operators, host->storage, sizeof(Expression), 100) ||
        !array_init(&compiler.controls, host->storage, sizeof(ControlFrame), 8) ||
        mm_storage_init(&compiler.name_storage, host->storage) != MM_STORAGE_OK ||
        !array_push(&compiler.operators, &sentinel))
    {
        report(&compiler, MM_SCRIPT_COMPILE_MEMORY);
        goto cleanup;
    }

    compiler.name_storage.min_grow[0] = 100 * host->unit_bytes;
    compiler.name_storage.min_grow[1] = 5 * 4;
    compiler.names.stride = 4;
    compiler.names.free_count = -1;
    compiler.names.unit_bytes = host->unit_bytes;
    compiler.names.context = &compiler.name_storage;
    compiler.names.reserve = reserve_names;
    while (next_token(&compiler, &token))
    {
        if (token.kind == ttSemi)
            finish_statement(&compiler);
        else if (!parse_token(&compiler, &token))
        {
            while (next_token(&compiler, &token) && token.kind != ttSemi)
                ;
            mm_array_set_count(&compiler.operators.array, 1);
            mm_array_set_count(&compiler.tree->array, 0);
        }
    }
    finish_statement(&compiler);
    if (compiler.controls.array.count)
        report(&compiler, MM_SCRIPT_COMPILE_UNEXPECTED_EOF);

cleanup:
    while (compiler.controls.array.count && array_pop(&compiler.controls, &frame))
        destroy_tree(frame.condition);
    mm_storage_dispose(&compiler.controls.storage);
    mm_storage_dispose(&compiler.operators.storage);
    mm_storage_dispose(&compiler.expressions.storage);
    mm_storage_dispose(&compiler.name_storage);
}
