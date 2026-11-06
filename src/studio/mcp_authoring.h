/* Copyright (c) 3DMMForever contributors. Licensed under the MIT License. */
// Included inside mcp's implementation namespace, after its JSON helpers.
// Author through the same scene/actor APIs as Studio; never patch saved bytes.

static PMovie AuthorMovie()
{
    auto studio = vpapp->Pstdio();
    auto movie = studio ? studio->Pmvie() : nullptr;
    return movie && !movie->FPlaying() && !vpcex->PgobTracking() ? movie : nullptr;
}

static bool AuthorFilename(const std::string &utf8, Filename *filename)
{
    std::basic_string<KauaiFiles::PathChar> native;
    if (utf8.find('\0') != std::string::npos || !KauaiFiles::NativePathFromUtf8(utf8, native) || native.empty() ||
        native.size() > kcchMaxStn)
        return false;
    String text;
    text.SetRgch(reinterpret_cast<achar *>(&native[0]), static_cast<LegacyLong>(native.size()));
    return filename->FBuildFromPath(&text);
}

static std::string AuthorName(String &name)
{
    std::string result;
    KauaiFonts::NameToUtf8({name.Prgch(), static_cast<std::size_t>(name.Cch()),
                            sizeof(achar) == 2 ? KauaiFonts::Encoding::utf16 : KauaiFonts::Encoding::legacyBytes},
                           result);
    return result;
}

static JVal AuthorVector(double x, double y, double z)
{
    JVal values = JVal::MkArr();
    for (double value : {x, y, z})
        values.a->push_back(JVal::MkDbl(value));
    return values;
}

static bool AuthorReadVector(const JVal *value, double *values, double minimum, double maximum)
{
    if (!value || !value->IsArr() || value->a->size() != 3)
        return false;
    for (int axis = 0; axis < 3; ++axis)
    {
        const auto &number = (*value->a)[axis];
        if (number.t != JT::Int && number.t != JT::Dbl)
            return false;
        values[axis] = number.t == JT::Int ? double(number.i) : number.d;
        if (!std::isfinite(values[axis]) || values[axis] < minimum || values[axis] > maximum)
            return false;
    }
    return true;
}

static JVal AuthorActorInfo(PActor actor)
{
    JVal result = JVal::MkObj();
    String name;
    actor->GetName(&name);
    result.Set("actor_id", JVal::MkInt(actor->Arid()));
    result.Set("name", JVal::MkStr(AuthorName(name)));
    BRS x, y, z;
    actor->Pbody()->GetPosition(&x, &y, &z);
    result.Set("position", AuthorVector(BrScalarToFloat(x), BrScalarToFloat(y), BrScalarToFloat(z)));
    const auto &pose = actor->EvaluatedFrameState().transform;
    const double scale = BrScalarToFloat(pose.rScaleStep);
    result.Set("scale", JVal::MkDbl(scale));
    result.Set("stretch", AuthorVector(BrScalarToFloat(pose.aevpull.rScaleX), BrScalarToFloat(pose.aevpull.rScaleY),
                                       BrScalarToFloat(pose.aevpull.rScaleZ)));
    JVal matrix = JVal::MkArr();
    for (int row = 0; row < 3; ++row)
        matrix.a->push_back(AuthorVector(BrScalarToFloat(pose.bmat34Cur.m[row][0]),
                                         BrScalarToFloat(pose.bmat34Cur.m[row][1]),
                                         BrScalarToFloat(pose.bmat34Cur.m[row][2])));
    result.Set("rotation_matrix", std::move(matrix));
    RC bounds;
    actor->GetRcBounds(&bounds);
    result.Set("screen_bounds", AuthorVector(bounds.xpLeft, bounds.ypTop, bounds.Dxp()));
    result.Set("screen_height", JVal::MkInt(bounds.Dyp()));
    const int count = actor->Pbody()->Cbact() - 2;
    result.Set("parts", JVal::MkInt(count));
    double largestVolume = -1;
    for (int part = 0; part < count; ++part)
    {
        const auto model = actor->Pbody()->PbactPart(part)->model;
        if (model)
        {
            const double dx = BrScalarToFloat(model->bounds.max.v[0] - model->bounds.min.v[0]);
            const double dy = BrScalarToFloat(model->bounds.max.v[1] - model->bounds.min.v[1]);
            const double dz = BrScalarToFloat(model->bounds.max.v[2] - model->bounds.min.v[2]);
            if (dx * dy * dz <= largestVolume)
                continue;
            largestVolume = dx * dy * dz;
            result.Set("model_size", AuthorVector(dx, dy, dz));
            result.Set("model_center",
                       AuthorVector(BrScalarToFloat((model->bounds.max.v[0] + model->bounds.min.v[0]) / 2),
                                    BrScalarToFloat((model->bounds.max.v[1] + model->bounds.min.v[1]) / 2),
                                    BrScalarToFloat((model->bounds.max.v[2] + model->bounds.min.v[2]) / 2)));
        }
    }
    BRA rx, ry, rz;
    actor->Ptmpl()->GetRestOrien(&rx, &ry, &rz);
    result.Set("rest_rotation", AuthorVector(BrScalarToFloat(BrAngleToDegree(rx)), BrScalarToFloat(BrAngleToDegree(ry)),
                                             BrScalarToFloat(BrAngleToDegree(rz))));
    return result;
}

static int authorDeferredRefresh = 0;
static void AuthorRefresh(PMovie movie)
{
    movie->SetDirty();
    movie->Pbwld()->MarkDirty();
    movie->InvalViewsAndScb();
    movie->Pmcc()->UpdateScrollbars();
    if (!authorDeferredRefresh)
        vpappb->UpdateMarked();
}

static JVal Tool_ListAssets(const JVal &args)
{
    const auto kindValue = args.Get("kind");
    const auto kind = kindValue ? kindValue->AsStr() : "props";
    ChunkTagOrType category = kind == "props"         ? kctgPrth
                              : kind == "actors"      ? kctgTmth
                              : kind == "backgrounds" ? kctgBkth
                              : kind == "materials"   ? kctgMtth
                                                      : ctgNil;
    if (category == ctgNil)
        return ContentError("kind must be props, actors, backgrounds or materials");
    ChunkIdentification root{category, cnoNil};
    auto list = BrowserContentList::PbclNew(nullptr, &root, ctgNil, nullptr, true);
    if (!list)
        return ContentError("Cannot enumerate original asset catalog");
    JVal result = JVal::MkArr();
    for (long i = 0; i < list->IthdMac(); ++i)
    {
        ThumbnailDescriptor descriptor;
        list->GetThd(i, &descriptor);
        String name;
        if (kind == "props" || kind == "actors")
        {
            auto asset = static_cast<PTemplate>(vptagm->PbacoFetch(&descriptor.tag, Template::FReadTmpl));
            if (!asset)
            {
                ReleasePpo(&list);
                return ContentError("Cannot read a template in the catalog");
            }
            asset->GetName(&name);
            ReleasePpo(&asset);
        }
        else if (kind == "backgrounds")
        {
            auto asset = static_cast<PBackground>(vptagm->PbacoFetch(&descriptor.tag, Background::FReadBkgd));
            if (!asset)
            {
                ReleasePpo(&list);
                return ContentError("Cannot read a background in the catalog");
            }
            asset->GetName(&name);
            ReleasePpo(&asset);
        }
        else
            name.FFormatSz(PszLit("Material %d"), static_cast<LegacyLong>(descriptor.tag.cno));
        JVal item = JVal::MkObj();
        item.Set("name", JVal::MkStr(AuthorName(name)));
        item.Set("sid", JVal::MkInt(descriptor.tag.sid));
        item.Set("cno", JVal::MkInt(descriptor.tag.cno));
        result.a->push_back(std::move(item));
    }
    ReleasePpo(&list);
    return _WrapInfoAsContent(result);
}

static JVal Tool_CreateScene(const JVal &args)
{
    auto movie = AuthorMovie();
    const auto number = args.Get("background_cno"), source = args.Get("sid");
    if (!movie || !number || !number->IsInt() || number->AsInt() < 0 || number->AsInt() > UINT32_MAX ||
        (source && (!source->IsInt() || source->AsInt() < 1 || source->AsInt() > INT32_MAX)))
        return ContentError("An idle movie and a valid background tag are required");
    TAG tag{source ? LegacyLong(source->AsInt()) : 2, nullptr, kctgBkgd, ChunkNumber(number->AsInt())};
    if (!movie->FAddScen(&tag))
        return ContentError("Cannot add the requested background");
    AuthorRefresh(movie);
    BMAT34 camera;
    BRS nearPlane, farPlane;
    BRA fieldOfView;
    movie->Pbwld()->GetCamera(&camera, &nearPlane, &farPlane, &fieldOfView);
    JVal result = JVal::MkObj(), matrix = JVal::MkArr();
    for (int row = 0; row < 4; ++row)
        matrix.a->push_back(AuthorVector(BrScalarToFloat(camera.m[row][0]), BrScalarToFloat(camera.m[row][1]),
                                         BrScalarToFloat(camera.m[row][2])));
    result.Set("scene", JVal::MkInt(movie->Iscen() + 1));
    result.Set("camera_matrix", std::move(matrix));
    result.Set("fov_degrees", JVal::MkDbl(BrScalarToFloat(BrAngleToDegree(fieldOfView))));
    return _WrapInfoAsContent(result);
}

static JVal Tool_EditFrame(const JVal &args)
{
    auto movie = AuthorMovie();
    const auto value = args.Get("frame");
    if (!movie || !movie->Pscen() || !value || !value->IsInt() || value->AsInt() < 1 || value->AsInt() > 100000)
        return ContentError("An idle scene and a one-based frame in 1..100000 are required");
    auto scene = movie->Pscen();
    if (!scene->FGotoFrm(scene->NfrmFirst() + long(value->AsInt()) - 1))
        return ContentError("Cannot edit that frame");
    AuthorRefresh(movie);
    JVal result = JVal::MkObj();
    result.Set("frame", JVal::MkInt(scene->Nfrm() - scene->NfrmFirst() + 1));
    result.Set("last_frame", JVal::MkInt(scene->NfrmLast() - scene->NfrmFirst() + 1));
    return _WrapInfoAsContent(result);
}

static JVal Tool_SelectScene(const JVal &args)
{
    auto movie = AuthorMovie();
    const auto value = args.Get("scene");
    if (!movie || !value || !value->IsInt() || value->AsInt() < 1 || value->AsInt() > movie->Cscen())
        return ContentError("An idle movie and an existing one-based scene are required");
    if (!movie->FSwitchScen(long(value->AsInt()) - 1))
        return ContentError("Cannot select that scene");
    movie->InvalViewsAndScb();
    movie->Pmcc()->UpdateScrollbars();
    vpappb->UpdateMarked();
    return ContentText("Scene selected.");
}

static JVal Tool_CreateTextBox(const JVal &args)
{
    auto movie = AuthorMovie();
    const auto text = args.Get("text"), bounds = args.Get("bounds"), fontSize = args.Get("font_size");
    const auto bold = args.Get("bold"), color = args.Get("color");
    const auto lastFrame = args.Get("last_frame"), alignment = args.Get("align"), font = args.Get("font");
    if (!movie || !movie->Pscen() || !text || !text->IsStr() || text->AsStr().empty() || text->AsStr().size() > 4096 ||
        text->AsStr().find('\0') != std::string::npos || !bounds || !bounds->IsArr() || bounds->a->size() != 4 ||
        (fontSize && (!fontSize->IsInt() || fontSize->AsInt() < 6 || fontSize->AsInt() > 120)) ||
        (bold && bold->t != JT::Bool) ||
        (lastFrame &&
         (!lastFrame->IsInt() || lastFrame->AsInt() < movie->Pscen()->Nfrm() - movie->Pscen()->NfrmFirst() + 1 ||
          lastFrame->AsInt() > movie->Pscen()->NfrmLast() - movie->Pscen()->NfrmFirst() + 1)) ||
        (alignment && (!alignment->IsStr() || (alignment->AsStr() != "left" && alignment->AsStr() != "center" &&
                                               alignment->AsStr() != "right"))) ||
        (font && (!font->IsStr() || font->AsStr().empty() || font->AsStr().size() > 200 ||
                  font->AsStr().find('\0') != std::string::npos)))
        return ContentError("Supply text, bounds [left, top, right, bottom], and optional font_size 6..120/bold/color");
    long coordinates[4];
    for (int i = 0; i < 4; ++i)
    {
        const auto &v = (*bounds->a)[i];
        if (!v.IsInt() || v.AsInt() < 0 || v.AsInt() > (i % 2 ? 306 : 544))
            return ContentError("Text box bounds must lie in the 544 by 306 movie canvas");
        coordinates[i] = long(v.AsInt());
    }
    if (coordinates[2] - coordinates[0] < 26 || coordinates[3] - coordinates[1] < 12)
        return ContentError("Text box bounds are too small");
    unsigned rgb = 0xffffff;
    if (color)
    {
        if (!color->IsStr() || color->AsStr().size() != 7 || color->AsStr()[0] != '#')
            return ContentError("color must be #RRGGBB");
        rgb = 0;
        for (char c : color->AsStr().substr(1))
        {
            if (!std::isxdigit(static_cast<unsigned char>(c)))
                return ContentError("color must be #RRGGBB");
            rgb = (rgb << 4) | (c <= '9' ? c - '0' : std::tolower(static_cast<unsigned char>(c)) - 'a' + 10);
        }
    }
    std::vector<std::uint8_t> encoded;
    if (!KauaiFonts::NameFromUtf8(text->AsStr(),
                                  sizeof(achar) == 2 ? KauaiFonts::Encoding::utf16 : KauaiFonts::Encoding::legacyBytes,
                                  encoded))
        return ContentError("Text cannot be represented by the legacy movie format");
    long fontIndex = -1;
    if (font)
    {
        std::vector<std::uint8_t> name;
        if (!KauaiFonts::NameFromUtf8(
                font->AsStr(), sizeof(achar) == 2 ? KauaiFonts::Encoding::utf16 : KauaiFonts::Encoding::legacyBytes,
                name))
            return ContentError("Invalid font name");
        String label;
        label.SetRgch(reinterpret_cast<achar *>(name.data()), long(name.size() / sizeof(achar)));
        if (!vntl.FGetOnn(&label, &fontIndex))
            return ContentError("Font is not installed");
    }
    RC rectangle(coordinates[0], coordinates[1], coordinates[2], coordinates[3]);
    auto caption = TextBox::PtboxNew(movie->Pscen(), &rectangle, true);
    if (!caption)
        return ContentError("Cannot create text box");
    CHP chp;
    caption->FetchChp(0, &chp);
    chp.dypFont = fontSize ? long(fontSize->AsInt()) : 18;
    chp.acrFore.Set(byte(rgb >> 16), byte(rgb >> 8), byte(rgb));
    chp.acrBack = kacrClear;
    chp.grfont = bold && bold->AsBool() ? fontBold : 0;
    if (fontIndex >= 0)
        chp.onn = fontIndex;
    PAP pap;
    caption->FetchPap(0, &pap);
    if (alignment)
        pap.jc = alignment->AsStr() == "center" ? jcCenter : alignment->AsStr() == "right" ? jcRight : jcLeft;
    const bool added = movie->Pscen()->FAddTbox(caption);
    bool ok = added && caption->FReplaceRgch(encoded.data(), long(encoded.size() / sizeof(achar)), 0, 0, &chp, &pap);
    if (ok && lastFrame)
    {
        ok = caption->FGotoFrame(movie->Pscen()->NfrmFirst() + long(lastFrame->AsInt()));
        if (ok)
        {
            caption->HideCore();
            ok = caption->FGotoFrame(movie->Pscen()->Nfrm());
        }
    }
    if (added && !ok)
    {
        movie->Pscen()->FRemTboxCore(caption);
        movie->ClearUndo();
    }
    ReleasePpo(&caption);
    if (!ok)
        return ContentError("Cannot insert text box");
    AuthorRefresh(movie);
    return ContentText("Native text box created at the current frame.");
}

static JVal Tool_CreateActor(const JVal &args)
{
    auto movie = AuthorMovie();
    const auto number = args.Get("template_cno"), source = args.Get("sid"), name = args.Get("name");
    if (!movie || !movie->Pscen() || !number || !number->IsInt() || number->AsInt() < 0 ||
        number->AsInt() > UINT32_MAX ||
        (source && (!source->IsInt() || source->AsInt() < 1 || source->AsInt() > INT32_MAX)) ||
        (name && (!name->IsStr() || name->AsStr().empty() || name->AsStr().size() > 200)))
        return ContentError("An idle scene, template tag and optional nonempty name are required");
    String label;
    if (name)
    {
        std::vector<std::uint8_t> encoded;
        if (name->AsStr().find('\0') != std::string::npos ||
            !KauaiFonts::NameFromUtf8(
                name->AsStr(), sizeof(achar) == 2 ? KauaiFonts::Encoding::utf16 : KauaiFonts::Encoding::legacyBytes,
                encoded))
            return ContentError("Name cannot be represented by the legacy movie format");
        label.SetRgch(reinterpret_cast<achar *>(encoded.data()),
                      static_cast<LegacyLong>(encoded.size() / sizeof(achar)));
    }
    TAG tag{source ? LegacyLong(source->AsInt()) : 2, nullptr, kctgTmpl, ChunkNumber(number->AsInt())};
    if (!movie->FInsActr(&tag))
        return ContentError("Cannot create the requested actor or prop");
    auto actor = movie->Pscen()->PactrSelected();
    if (!movie->FAddToRollCall(actor, name ? &label : nullptr))
        return ContentError("Cannot register the new actor");
    if (name && !movie->FNameActr(actor->Arid(), &label))
        return ContentError("Cannot name the new actor");
    PActor copy = nullptr;
    auto undo = SceneActorUndo::PsunaNew();
    if (!undo || !actor->FDup(&copy, true))
    {
        ReleasePpo(&undo);
        ReleasePpo(&copy);
        return ContentError("Cannot record actor creation undo");
    }
    copy->SetArid(actor->Arid());
    undo->SetType(utAdd);
    undo->SetActr(copy);
    const bool ok = movie->FAddUndo(undo);
    ReleasePpo(&undo);
    if (!ok)
        return ContentError("Cannot record actor creation undo");
    movie->Pmcc()->EnableActorTools();
    AuthorRefresh(movie);
    return _WrapInfoAsContent(AuthorActorInfo(actor));
}

static bool AuthorColor(PMovie movie, PActor actor, unsigned rgb, bool shaded)
{
    RC pixel(0, 0, 1, 1);
    Color colors[256];
    auto port = GraphicsPort::PgptNewOffscreen(&pixel, 8);
    const bool paletteOk = port && port->FGetIndexedPalette(colors);
    ReleasePpo(&port);
    if (!paletteOk)
        return false;
    long nearest = 0;
    long best = INT32_MAX;
    for (int i = 0; i < 256; ++i)
    {
        const long r = long((rgb >> 16) & 255) - colors[i].bRed;
        const long g = long((rgb >> 8) & 255) - colors[i].bGreen;
        const long b = long(rgb & 255) - colors[i].bBlue;
        const long distance = r * r + g * g + b * b;
        if (distance < best)
        {
            best = distance;
            nearest = i;
        }
    }
    // Reuse the original shaded palette ramps for a solid colour. In an
    // indexed renderer RGB alone does not choose the material's ramp.
    long base = nearest, range = 0;
    ChunkIdentification materialRoot{kctgMtth, cnoNil};
    auto catalog = shaded ? BrowserContentList::PbclNew(nullptr, &materialRoot, ctgNil, nullptr, true) : nullptr;
    long rampDistance = INT32_MAX;
    for (long i = 0; catalog && i < catalog->IthdMac(); ++i)
    {
        ThumbnailDescriptor descriptor;
        catalog->GetThd(i, &descriptor);
        auto existing = static_cast<PMaterial_MTRL>(vptagm->PbacoFetch(&descriptor.tag, Material_MTRL::FReadMtrl));
        if (!existing)
            continue;
        const auto material = existing->Pbmtl();
        if (!material->colour_map && int(material->index_base) + material->index_range < 256)
        {
            const auto &color = colors[material->index_base + material->index_range];
            const long r = long((rgb >> 16) & 255) - color.bRed;
            const long g = long((rgb >> 8) & 255) - color.bGreen;
            const long b = long(rgb & 255) - color.bBlue;
            const long distance = r * r + g * g + b * b;
            if (distance < rampDistance)
            {
                rampDistance = distance;
                base = material->index_base;
                range = material->index_range;
            }
        }
        ReleasePpo(&existing);
    }
    ReleasePpo(&catalog);
    // Black facial details need the nearest dark entry, rather than the
    // light end of a brown shading ramp.
    if (((rgb >> 16) & 255) < 48 && ((rgb >> 8) & 255) < 48 && (rgb & 255) < 48)
    {
        base = nearest;
        range = 0;
    }
    PChunkyResourceFile resources;
    if (!movie->FEnsureAutosave(&resources))
        return false;
    ChunkNumber materialNumber;
    MaterialOnFile material{kboCur, koskCur,    rgb,         BR_UFRACTION(.35), BR_UFRACTION(.65),
                            0,      byte(base), byte(range), BR_SCALAR(50)};
    auto file = resources->Pcfl();
    const bool written = file->FAddPv(&material, sizeof(material), kctgMtrl, &materialNumber);
    if (!written)
        return false;
    TAG tag{0, resources, kctgMtrl, materialNumber};
    for (long set = 0; set < actor->Pbody()->Cbset(); ++set)
        if (!actor->FSetCostumeCore(set, &tag, 0, tNo))
            return false;
    return true;
}

static JVal Tool_TransformActor(const JVal &args)
{
    auto movie = AuthorMovie();
    const auto id = args.Get("actor_id");
    if (!movie || !movie->Pscen() || !id || !id->IsInt() || id->AsInt() < 0 || id->AsInt() > INT32_MAX)
        return ContentError("An idle scene and a valid actor_id are required");
    auto actor = movie->Pscen()->PactrFromArid(long(id->AsInt()));
    if (!actor || !actor->FOnStage())
        return ContentError("Actor is not on stage at this frame");
    const auto position = args.Get("position"), offset = args.Get("offset"), rotation = args.Get("rotation");
    const auto scale = args.Get("scale"), stretch = args.Get("stretch"), color = args.Get("color");
    const auto shaded = args.Get("shaded");
    if (shaded && (shaded->t != JT::Bool || !color))
        return ContentError("shaded requires a color and must be boolean");
    double pos[3], delta[3], angles[3], pulls[3];
    double factor = 1;
    unsigned rgb = 0;
    if ((position && offset) || (position && !AuthorReadVector(position, pos, -8000, 8000)) ||
        (offset && !AuthorReadVector(offset, delta, -8000, 8000)) ||
        (rotation && !AuthorReadVector(rotation, angles, -36000, 36000)) ||
        (stretch && !AuthorReadVector(stretch, pulls, .25, 4)))
        return ContentError("Supply finite xyz vectors; position/offset are exclusive, stretch is .25..4");
    if (scale)
    {
        if (scale->t != JT::Int && scale->t != JT::Dbl)
            return ContentError("scale must be a number");
        factor = scale->t == JT::Int ? double(scale->i) : scale->d;
        if (!std::isfinite(factor) || factor < .25 || factor > 10)
            return ContentError("scale must be .25..10");
    }
    if (color)
    {
        const auto text = color->AsStr();
        if (text.size() != 7 || text[0] != '#')
            return ContentError("color must be #RRGGBB");
        for (std::size_t i = 1; i < 7; ++i)
        {
            const char c = text[i];
            const int digit = c >= '0' && c <= '9'   ? c - '0'
                              : c >= 'a' && c <= 'f' ? c - 'a' + 10
                              : c >= 'A' && c <= 'F' ? c - 'A' + 10
                                                     : -1;
            if (digit < 0)
                return ContentError("color must be #RRGGBB");
            rgb = rgb * 16 + unsigned(digit);
        }
    }
    if (!position && !offset && !rotation && !scale && !stretch && !color)
        return ContentError("No edit was supplied");
    PActor before = nullptr;
    if (!actor->FDup(&before))
        return ContentError("Cannot snapshot the actor");
    bool ok = true;
    if (position || offset)
    {
        BRS x, y, z;
        actor->Pbody()->GetPosition(&x, &y, &z);
        if (position)
        {
            delta[0] = pos[0] - BrScalarToFloat(x);
            delta[1] = pos[1] - BrScalarToFloat(y);
            delta[2] = pos[2] - BrScalarToFloat(z);
        }
        ok = actor->FMoveRoute(BrFloatToScalar(float(delta[0])), BrFloatToScalar(float(delta[1])),
                               BrFloatToScalar(float(delta[2])));
    }
    const bool relative = args.Get("relative") && args.Get("relative")->AsBool();
    // The UI helper swaps X/Z for its canonical actor tool labels. Swap the
    // arguments back to expose actual world X, Y, Z rotations over MCP.
    if (ok && rotation)
        ok = (relative || actor->FNormalizeCore(fnormRotate)) &&
             actor->FRotateAroundWorld(BR_ANGLE_DEG(std::fmod(angles[2], 360.0)),
                                       BR_ANGLE_DEG(std::fmod(angles[1], 360.0)),
                                       BR_ANGLE_DEG(std::fmod(angles[0], 360.0)), false);
    if (ok && (scale || stretch))
    {
        // Absolute size commands preserve whichever component was omitted.
        const auto saved = actor->EvaluatedFrameState().transform;
        if (!relative)
            ok = actor->FNormalizeCore(fnormSize);
        const auto uniform = scale ? BrFloatToScalar(float(factor)) : relative ? rOne : saved.rScaleStep;
        if (ok)
            ok = actor->FScale(uniform);
        if (ok)
            ok = actor->FPull(stretch    ? BrFloatToScalar(float(pulls[0]))
                              : relative ? rOne
                                         : saved.aevpull.rScaleX,
                              stretch    ? BrFloatToScalar(float(pulls[1]))
                              : relative ? rOne
                                         : saved.aevpull.rScaleY,
                              stretch    ? BrFloatToScalar(float(pulls[2]))
                              : relative ? rOne
                                         : saved.aevpull.rScaleZ);
    }
    if (ok && color)
        ok = AuthorColor(movie, actor, rgb, !shaded || shaded->AsBool());
    if (ok)
        ok = actor->FCreateUndo(before);
    if (!ok)
        actor->Restore(before);
    ReleasePpo(&before);
    if (!ok)
        return ContentError("Actor edit failed; the actor was restored");
    movie->Pscen()->SelectActr(actor);
    AuthorRefresh(movie);
    return _WrapInfoAsContent(AuthorActorInfo(actor));
}

static JVal Tool_InspectActor(const JVal &args)
{
    auto movie = AuthorMovie();
    const auto id = args.Get("actor_id");
    auto actor = movie && movie->Pscen() && id && id->IsInt() && id->AsInt() >= 0 && id->AsInt() <= INT32_MAX
                     ? movie->Pscen()->PactrFromArid(long(id->AsInt()))
                     : nullptr;
    return actor ? _WrapInfoAsContent(AuthorActorInfo(actor)) : ContentError("Unknown actor_id");
}

static JVal Tool_TransformActors(const JVal &args)
{
    const auto edits = args.Get("edits");
    auto movie = AuthorMovie();
    if (!movie || !edits || !edits->IsArr() || edits->a->empty() || edits->a->size() > 256)
        return ContentError("Supply 1..256 actor edits for an idle scene");
    struct Defer
    {
        Defer()
        {
            ++authorDeferredRefresh;
        }
        ~Defer()
        {
            --authorDeferredRefresh;
        }
    };
    JVal error;
    long completed = 0;
    {
        Defer defer;
        for (const auto &edit : *edits->a)
        {
            auto result = Tool_TransformActor(edit);
            if (result.Get("isError") && result.Get("isError")->AsBool())
            {
                error = std::move(result);
                break;
            }
            ++completed;
        }
    }
    AuthorRefresh(movie);
    if (!error.IsNull())
    {
        error.Set("completed", JVal::MkInt(completed));
        return error;
    }
    JVal result = JVal::MkObj();
    result.Set("completed", JVal::MkInt(completed));
    return _WrapInfoAsContent(result);
}

static JVal Tool_ImportSound(const JVal &args)
{
    auto movie = AuthorMovie();
    const auto path = args.Get("path");
    if (!movie || !movie->Pscen() || !path || !path->IsStr() || path->AsStr().empty() || path->AsStr().size() > 240)
        return ContentError("An idle scene and a sound file path are required");
    Filename filename;
    if (!AuthorFilename(path->AsStr(), &filename))
        return ContentError("Invalid sound path");
    auto file = FileObject::PfilOpen(&filename);
    if (!file)
        return ContentError("Cannot open the sound file");
    PChunkyResourceFile resources = nullptr;
    ChunkNumber number = cnoNil;
    String name;
    name.SetSzs(PszLit("Opening dialogue"));
    const bool copied = movie->FAutoSave() && movie->FEnsureAutosave(&resources) &&
                        movie->FCopySndFileToMvie(file, stySpeech, &number, &name);
    ReleasePpo(&file);
    if (!copied)
        return ContentError("Cannot import the sound file");
    TAG tag{0, resources, kctgMsnd, number};
    if (!movie->FAddBkgdSnd(&tag, tNo, tNo, vlmNil, stySpeech))
        return ContentError("Cannot place the sound cue");
    AuthorRefresh(movie);
    return ContentText("Sound imported and placed at the current frame.");
}

static JVal Tool_SaveMovie(const JVal &args)
{
    auto movie = AuthorMovie();
    const auto path = args.Get("path");
    if (!movie || !path || !path->IsStr() || path->AsStr().empty() || path->AsStr().size() > 240)
        return ContentError("An idle movie and a full destination path are required");
    Filename file;
    if (!AuthorFilename(path->AsStr(), &file))
        return ContentError("Invalid destination path");
    if (file.TExists() != tNo)
        return ContentError("Destination exists or cannot be checked; choose a new file");
    if (!movie->FSaveToFni(&file, true))
        return ContentError("Movie save failed");
    return ContentText("Movie saved through Studio's normal file writer.");
}
