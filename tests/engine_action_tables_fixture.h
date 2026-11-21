#ifndef ENGINE_ACTION_TABLES_FIXTURE_H
#define ENGINE_ACTION_TABLES_FIXTURE_H

#include <cstring>

class FixtureActionTables
{
  public:
    FixtureActionTables() = default;
    FixtureActionTables(const FixtureActionTables &) = delete;
    FixtureActionTables &operator=(const FixtureActionTables &) = delete;
    ~FixtureActionTables() { Reset(); }

    bool Copy(PGeneralGroup sourceCels, PDynamicArray sourceMatrices)
    {
        const auto allocator = CollectionAllocator();
        _cels = mm_action_table_new(&allocator);
        _matrices = mm_action_table_new(&allocator);
        if (!_cels || !_matrices)
            return false;
        mm_action_cels_prepare(_cels);
        for (LegacyLong i = 0; i < sourceCels->IvMac(); ++i)
        {
            AnimationCel cel;
            sourceCels->GetFixed(i, &cel);
            const auto bytes = sourceCels->Cb(i);
            auto parts = mm_action_cel_append(_cels, cel.chidSnd, cel.dwr, bytes / sizeof(CelPartSpec));
            if (!parts)
                return false;
            if (bytes)
                std::memcpy(parts, sourceCels->QvGet(i), bytes);
        }
        if (!mm_action_matrices_prepare(_matrices, sourceMatrices->IvMac()))
            return false;
        for (LegacyLong i = 0; i < sourceMatrices->IvMac(); ++i)
            mm_action_matrix_put(_matrices, i, sourceMatrices->QvGet(i));
        return true;
    }

    MMActionResource *Create(LegacyUlong flags, MMActionTable *sounds = nullptr)
    {
        const MMActionDefinition definition{_cels, _matrices, sounds, flags};
        auto action = mm_action_resource_create(&definition, &EngineTemplate::ActionResourceHost());
        if (action)
            _cels = _matrices = nullptr;
        return action;
    }

    void Reset()
    {
        mm_action_table_dispose(_cels);
        mm_action_table_dispose(_matrices);
        _cels = _matrices = nullptr;
    }

  private:
    MMActionTable *_cels = nullptr;
    MMActionTable *_matrices = nullptr;
};

inline MMActionResource *MakeFixtureAction(PGeneralGroup cels, PDynamicArray matrices, LegacyUlong flags,
                                           MMActionTable *sounds = nullptr)
{
    FixtureActionTables tables;
    return tables.Copy(cels, matrices) ? tables.Create(flags, sounds) : nullptr;
}

#endif
