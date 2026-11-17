#include "prop_gizmo.h"
#include "dialog_widgets.h"
int prop_gizmo_properties(MarcusPropGizmo *gizmo)
{
    static const char *axes[] = {"X", "Y", "Z"};
    MarcusPropFields state;
    float value, low, high;
    int axis, changed;
    prop_gizmo_field_state(gizmo, &state);
    if (!state.enabled) return 0;
    low = state.mode == 2 ? 1 : state.mode == 1 ? -36000 : -32700;
    high = state.mode == 2 ? 1000 : state.mode == 1 ? 36000 : 32700;
    igBeginDisabled(state.editing == 1);
    for (axis = 0; axis < 3; ++axis) {
        value = state.value[axis]; igSetNextItemWidth(-28);
        changed = igDragFloat(axes[axis], &value, state.mode ? .5f : .1f, low, high,
            state.mode == 2 ? "%.1f %%" : state.mode == 1 ? "%.2f deg" : "%.3f", ImGuiSliderFlags_AlwaysClamp);
        if (changed || igIsItemActivated() || igIsItemDeactivated())
            prop_gizmo_field_edit(gizmo, axis, value, igIsItemActivated(), igIsItemDeactivated());
        if (igIsItemHovered(0)) igSetTooltip("Drag to adjust; Ctrl+click to type. Escape cancels. Applies to the whole object track.");
    }
    igEndDisabled(); return 1;
}
