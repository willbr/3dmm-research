#include "prop_gizmo.h"
#include "dialog_widgets.h"
#include "transform_gizmo.h"
int prop_gizmo_properties(MarcusPropGizmo *gizmo)
{
    static const char *axes[] = {"X", "Y", "Z"};
    MarcusPropFields state;
    float value;
    int axis, changed;
    prop_gizmo_field_state(gizmo, &state);
    if (!state.enabled) return 0;
    igBeginDisabled(state.dragging_gizmo);
    for (axis = 0; axis < 3; ++axis) {
        value = state.value[axis]; igSetNextItemWidth(-28);
        changed = igDragFloat(axes[axis], &value, state.mode ? .5f : .1f, state.low, state.high,
            state.mode == MARCUS_GIZMO_SCALE ? "%.1f %%" : state.mode == MARCUS_GIZMO_ROTATE ? "%.2f deg" : "%.3f",
            ImGuiSliderFlags_AlwaysClamp);
        if (changed || igIsItemActivated() || igIsItemDeactivated())
            prop_gizmo_field_edit(gizmo, axis, value, igIsItemActivated(), igIsItemDeactivated());
        if (igIsItemHovered(0)) igSetTooltip("Drag to adjust; Ctrl+click to type. Escape cancels. Applies to the whole object track.");
    }
    igEndDisabled(); return 1;
}
