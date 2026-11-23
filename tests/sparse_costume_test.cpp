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
    auto material = mm_brender_material_new(ivNil, ivNil);
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
    const auto writer = file ? file->WriteIndex() : MMArchiveWriter{};
    ok = ok && table(kctgGlpi, parents) && table(kctgGlbs, sets) && table(kctgGgcm, costumes) &&
         mm_material_owner_write(material, &writer, kctgMtrl, &materialNumber) &&
         file->FAddChildPv(kctgTmpl, number, 0, &custom, sizeof(custom), kctgCmtl, &customNumber) &&
         file->FAdoptChild(kctgCmtl, customNumber, kctgMtrl, materialNumber, 0) &&
         file->FAdoptChild(kctgTmpl, number, kctgCmtl, customNumber, 2);
    auto cache = ok ? ChunkyResourceFile::PcrfNew(file, 0) : nullptr;
    auto tmpl = cache ? mm_brender_template_fetch(cache->CoreCache(), kctgTmpl, number, nullptr) : nullptr;
    if (!ok || !tmpl) std::fputs("Cannot construct the sparse template fixture.\n", stderr);
    ok = ok && tmpl && mm_brender_template_costume_count(tmpl, 0) == 2 && mm_brender_template_costume_id(tmpl, 0, 1) == 2;
    auto body = tmpl ? mm_brender_template_body(tmpl) : nullptr;
    auto sparse = tmpl ? mm_brender_template_costume(tmpl, 2) : nullptr;
    auto missing = tmpl ? mm_brender_template_costume(tmpl, 1) : nullptr;
    if (!body || !sparse || missing) std::fputs("Sparse costume lookup/application failed.\n", stderr);
    ok = ok && body && sparse && !missing && mm_material_owner_custom(sparse)->set == 0;
    if (body && sparse) EngineBody::SetPartSetCmtl(body, sparse);
    mm_material_owner_release(missing); mm_material_owner_release(sparse); mm_body_owner_release(body); mm_template_owner_release(tmpl);
    ReleasePpo(&cache); mm_material_owner_release(material); ReleasePpo(&parents); ReleasePpo(&sets); ReleasePpo(&costumes);
    ReleasePpo(&file);
    BrEnd();
    if (!ok || assertions) return 1;
    puts("Sparse costume child IDs load and apply; missing IDs fail without assertions.");
    return 0;
}
