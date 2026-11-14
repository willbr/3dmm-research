#ifndef ENGINE_BODY_GEOMETRY_FIXTURE_H
#define ENGINE_BODY_GEOMETRY_FIXTURE_H
class BodyGeometryFixtureAccess : public Body
{
  public:
    static void Seed(PBody body, int mode)
    {
        auto member = &BodyGeometryFixtureAccess::_geometry;
        auto &g = body->*member;
        g.bounds = {mode == 0 ? 0 : -13, mode == 0 ? 0 : -7, mode < 2 ? 0 : 28, mode < 2 ? 0 : 22};
        g.last_visible = {-41, -32, 62, 51};
    }
    static void Prepare(PBody body)
    {
        _PrepareToRender(body->PbactRoot());
    }
    static void Rendered(PBACT actor, RC *rect)
    {
        _BactRendered(actor, rect);
    }
    static void RawRect(PBACT actor, RC *rect)
    {
        _GetRc(actor, rect);
    }
    static void State(PBody body, LegacyLong *out)
    {
        auto member = &BodyGeometryFixtureAccess::_geometry;
        out[0] = (body->*member).found;
        out[1] = (body->*member).set;
    }
    static int Search(PBody target, PBACT actor)
    {
        BVEC3 v = {};
        return _FFilterSearch(actor, nullptr, nullptr, &v, &v, 0, 0, target);
    }
    static void ResetSearch(PBody target)
    {
        auto member = &BodyGeometryFixtureAccess::_geometry;
        three_mm_body_search_begin(&(target->*member));
    }
    static void Closest(PBody a, PBody b, LegacyLong *out)
    {
        ThreeMMBodyPick pick;
        three_mm_body_pick_begin(&pick);
        BVEC3 v = {};
        _FFilter(a->PbactRoot() + 1, nullptr, nullptr, &v, &v, BR_SCALAR(3), 0, &pick);
        _FFilter(b->PbactRoot() + 2, nullptr, nullptr, &v, &v, BR_SCALAR(3), 0, &pick);
        _FFilter(b->PbactRoot(), nullptr, nullptr, &v, &v, BR_SCALAR(2), 0, &pick);
        _FFilter(a->PbactRoot() + 2, nullptr, nullptr, &v, &v, BR_SCALAR(-1), 0, &pick);
        out[0] = pick.body == a ? 1 : pick.body == b ? 2 : 0;
        out[1] = pick.body ? pick.depth : 0;
        out[2] = -1;
        if (pick.body)
            PbodyFromBact(static_cast<PBACT>(pick.actor), &out[2]);
    }
};
static bool TestBodyGeometry()
{
    FILE *snapshot = nullptr;
    if (const char *path = std::getenv("THREE_MM_BODY_GEOMETRY_SNAPSHOT"))
        if (!(snapshot = std::fopen(path, "wb")))
            return false;
    bool ok = true;
    int cases = 0;
    ThreeMMU32 fingerprint = 2166136261U, bytes = 0;
    auto record = [&](const void *p, size_t n) {
        auto data = static_cast<const unsigned char *>(p);
        for (size_t i = 0; i < n; ++i)
            fingerprint = (fingerprint ^ data[i]) * 16777619U;
        bytes += static_cast<ThreeMMU32>(n);
        return !snapshot || std::fwrite(p, 1, n, snapshot) == n;
    };
    BRV vertices[4] = {};
    BRF faces[2] = {};
    vertices[0].p.v[0] = BR_SCALAR(-2);
    vertices[0].p.v[1] = BR_SCALAR(-1);
    vertices[1].p.v[0] = BR_SCALAR(3);
    vertices[2].p.v[1] = BR_SCALAR(4);
    vertices[3].p.v[2] = BR_SCALAR(5);
    faces[0].vertices[1] = 1;
    faces[0].vertices[2] = 2;
    faces[1].vertices[0] = 1;
    faces[1].vertices[1] = 2;
    faces[1].vertices[2] = 3;
    auto model = Model::PmodlNew(4, vertices, 2, faces);
    auto material = Material_MTRL::PmtrlNew();
    ok = model && material;
    for (int shape = 0; ok && shape < 3; ++shape)
        for (int matrix = 0; ok && matrix < 6; ++matrix)
            for (int highlight = 0; ok && highlight < 2; ++highlight)
                for (int bounds = 0; ok && bounds < 4; ++bounds)
                {
#ifdef DEBUG
                    const LegacyLong base = vdmglob.dmaglBase.cv, hq = vdmglob.dmaglHq.cv, pv = vdmglob.dmaglPv.cv;
#endif
                    auto parents = DynamicArray::PglNew(sizeof(short)), sets = DynamicArray::PglNew(sizeof(short));
                    int count = shape == 0 ? 1 : shape == 1 ? 3 : 6;
                    ok = parents && sets;
                    for (int part = 0; ok && part < count; ++part)
                    {
                        short parent = part ? static_cast<short>((part - 1) / 2) : -1,
                              set = static_cast<short>(part % 2);
                        ok = parents->FAdd(&parent) && sets->FAdd(&set);
                    }
                    auto body = ok ? Body::PbodyNew(parents, sets) : nullptr;
                    auto other = ok ? Body::PbodyNew(parents, sets) : nullptr;
                    ok = body && other && ok;
                    if (ok)
                    {
                        for (int set = 0; set < body->Cbset(); ++set)
                        {
                            body->SetPartSetMtrl(set, material);
                            other->SetPartSetMtrl(set, material);
                        }
                        for (int part = 0; part < count; ++part)
                        {
                            BMAT34 m;
                            BrMatrix34Identity(&m);
                            m.m[3][0] = BR_SCALAR(part * 3 - 4);
                            m.m[3][1] = BR_SCALAR(part - 2);
                            m.m[3][2] = BR_SCALAR(part * 2);
                            if (part & 1)
                                m.m[0][1] = BR_SCALAR(0.375);
                            body->SetPartModel(part, model);
                            other->SetPartModel(part, model);
                            body->SetPartMatrix(part, &m);
                        }
                        BMAT34 root;
                        BrMatrix34Identity(&root);
                        if (matrix == 1)
                        {
                            root.m[3][0] = BR_SCALAR(20);
                            root.m[3][1] = BR_SCALAR(-3);
                        }
                        if (matrix == 2)
                        {
                            root.m[0][0] = BR_SCALAR(-2);
                            root.m[1][1] = BR_SCALAR(0.5);
                        }
                        if (matrix == 3)
                        {
                            root.m[0][1] = BR_SCALAR(0.333);
                            root.m[2][0] = BR_SCALAR(-0.625);
                        }
                        if (matrix == 4)
                        {
                            BrMatrix34RotateY(&root, BR_ANGLE_DEG(37));
                            root.m[3][2] = BR_SCALAR(13.25);
                        }
                        if (matrix == 5)
                        {
                            root.m[3][0] = BR_SCALAR(12000);
                            root.m[3][1] = BR_SCALAR(-12000);
                        }
                        *(&body->PbactRoot()->t.t.mat) = root;
                        if (highlight)
                            body->Hilite();
                        BodyGeometryFixtureAccess::Seed(body, bounds);
                        BodyGeometryFixtureAccess::Seed(other, 3);
                        LegacyLong key[] = {shape, matrix, highlight, bounds};
                        ok = record(key, sizeof(key));
                        BCB local, world;
                        body->GetBcbBounds(&local);
                        body->GetBcbBounds(&world, true);
                        ok = record(&local, sizeof(local)) && record(&world, sizeof(world)) && ok;
                        BodyGeometryFixtureAccess::Prepare(body);
                        ok = record(&body->PbactRoot()[1].t.t.mat, sizeof(BMAT34)) && ok;
                        for (int stage = 0; stage < 5; ++stage)
                        {
                            RC r;
                            if (stage == 0)
                                r.Set(0, 0, 0, 0);
                            if (stage == 1)
                                r.Set(-5, -7, 15, 17);
                            if (stage == 2)
                                r.Set(18, 19, 30, 31);
                            if (stage == 3)
                                r.Set(40, 50, 30, 20);
                            if (stage == 4)
                                r.Set(-21, -23, 31, 33);
                            BodyGeometryFixtureAccess::Rendered(body->PbactRoot() + stage % body->Cbact(), &r);
                            RC current, fallback;
                            BodyGeometryFixtureAccess::RawRect(body->PbactRoot(), &current);
                            body->GetRcBounds(&fallback);
                            LegacyLong center[2];
                            body->GetCenter(center, center + 1);
                            BRS position[3];
                            body->GetPosition(position, position + 1, position + 2);
                            LegacyLong visible = body->FIsInView();
                            ok = record(&current, sizeof(current)) && record(&fallback, sizeof(fallback)) &&
                                 record(center, sizeof(center)) && record(position, sizeof(position)) &&
                                 record(&visible, sizeof(visible)) && ok;
                        }
                        for (int actor = 0; actor < body->Cbact(); ++actor)
                        {
                            LegacyLong set = 99;
                            auto owner = Body::PbodyFromBact(body->PbactRoot() + actor, &set);
                            LegacyLong result[] = {owner == body, set};
                            ok = record(result, sizeof(result)) && ok;
                            BodyGeometryFixtureAccess::ResetSearch(body);
                            result[0] = BodyGeometryFixtureAccess::Search(body, other->PbactRoot() + actor);
                            result[1] = BodyGeometryFixtureAccess::Search(body, body->PbactRoot() + actor);
                            LegacyLong state[2];
                            BodyGeometryFixtureAccess::State(body, state);
                            ok = record(result, sizeof(result)) && record(state, sizeof(state)) && ok;
                        }
                        LegacyLong closest[3];
                        BodyGeometryFixtureAccess::Closest(body, other, closest);
                        ok = record(closest, sizeof(closest)) && ok;
                        ++cases;
                    }
                    ReleasePpo(&body);
                    ReleasePpo(&other);
                    ReleasePpo(&parents);
                    ReleasePpo(&sets);
#ifdef DEBUG
                    ok = vdmglob.dmaglBase.cv == base && vdmglob.dmaglHq.cv == hq && vdmglob.dmaglPv.cv == pv && ok;
#endif
                }
    ReleasePpo(&model);
    ReleasePpo(&material);
    if (snapshot)
        ok = std::fclose(snapshot) == 0 && ok;
    std::fprintf(stderr, "[body geometry] cases=%d\n", cases);
    // Frozen native geometry/render trace before the C conversion.
    const unsigned endian = 1;
    if (*reinterpret_cast<const unsigned char *>(&endian))
        ok = ok && bytes == 76608 && fingerprint == 0xecfb9185U;
    return ok && cases == 144;
}
#endif
