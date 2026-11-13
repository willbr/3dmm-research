#include "soc_core.h"
#include <cstdio>
ASSERTNAME

static int assertions;
void WarnProc(schar *, LegacyLong, schar *) {}
bool FAssertProc(schar *file, LegacyLong line, schar *message, void *, LegacyLong)
{
    std::fprintf(stderr, "%s:%d: %s\n", file, int(line), message ? message : "assertion");
    ++assertions;
    return false;
}

int main()
{
    BrBegin();
    auto file = ChunkyFile::PcflCreateTemp();
    auto parents = DynamicArray::PglNew(sizeof(short));
    auto sets = DynamicArray::PglNew(sizeof(short));
    auto costumes = GeneralGroup::PggNew(sizeof(int32_t));
    auto material = Material_MTRL::PmtrlNew();
    short parent = -1, set = 0;
    int32_t ids[] = {0, 2}, count = 2;
    TemplateOnFile header = {};
    header.bo = kboCur; header.osk = koskCur; header.grftmpl = ftmplProp;
    ChunkNumber number = 0, child = 0, materialNumber = 0, customNumber = 0;
    bool ok = file && parents && sets && costumes && material &&
              parents->FAdd(&parent) && sets->FAdd(&set) &&
              costumes->FAdd(sizeof(ids), nullptr, ids, &count) &&
              file->FAddPv(&header, sizeof(header), kctgTmpl, &number);
    String name;
    name.SetSzs(PszLit("Sparse costume fixture"));
    ok = ok && file->FSetName(kctgTmpl, number, &name);
    auto table = [&](ChunkTagOrType type, auto data) {
        DataBlock block;
        return file->FAddChild(kctgTmpl, number, 0, data->CbOnFile(), type, &child, &block) && data->FWrite(&block);
    };
    CustomMaterialOnFile custom = {kboCur, koskCur, 0};
    ok = ok && table(kctgGlpi, parents) && table(kctgGlbs, sets) && table(kctgGgcm, costumes) &&
         material->FWrite(file, kctgMtrl, &materialNumber) &&
         file->FAddChildPv(kctgTmpl, number, 0, &custom, sizeof(custom), kctgCmtl, &customNumber) &&
         file->FAdoptChild(kctgCmtl, customNumber, kctgMtrl, materialNumber, 0) &&
         file->FAdoptChild(kctgTmpl, number, kctgCmtl, customNumber, 2);
    auto cache = ok ? ChunkyResourceFile::PcrfNew(file, 0) : nullptr;
    auto tmpl = cache ? static_cast<PTemplate>(cache->PbacoFetch(kctgTmpl, number, Template::FReadTmpl)) : nullptr;
    if (!ok || !tmpl) std::fputs("Cannot construct the sparse template fixture.\n", stderr);
    ok = ok && tmpl && tmpl->CcmidOfBset(0) == 2 && tmpl->CmidOfBset(0, 1) == 2;
    auto body = tmpl ? tmpl->PbodyCreate() : nullptr;
    auto sparse = tmpl ? tmpl->PcmtlFetch(2) : nullptr;
    auto missing = tmpl ? tmpl->PcmtlFetch(1) : nullptr;
    if (!body || !sparse || missing) std::fputs("Sparse costume lookup/application failed.\n", stderr);
    ok = ok && body && sparse && !missing && sparse->Ibset() == 0;
    if (body && sparse) body->SetPartSetCmtl(sparse);
    ReleasePpo(&missing); ReleasePpo(&sparse); ReleasePpo(&body); ReleasePpo(&tmpl);
    ReleasePpo(&cache); ReleasePpo(&material); ReleasePpo(&parents); ReleasePpo(&sets); ReleasePpo(&costumes);
    ReleasePpo(&file);
    BrEnd();
    if (!ok || assertions) return 1;
    puts("Sparse costume child IDs load and apply; missing IDs fail without assertions.");
    return 0;
}
