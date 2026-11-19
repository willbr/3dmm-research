# External SDL/cimgui declarations use bool/64-bit extensions; the application's own
# sources and public bridge header must pass strict C89 diagnostics.
set(includes)
foreach(directory IN LISTS SDL_INCLUDES CIMGUI_INCLUDES)
  list(APPEND includes -isystem "${directory}")
endforeach()
execute_process(COMMAND "${COMPILER}" -std=c89 -pedantic-errors
  -DIMGUI_ENABLE_FREETYPE -DIMGUI_ENABLE_STB_TRUETYPE
  -Werror=declaration-after-statement -Wall -Wextra -fsyntax-only ${includes}
  "-I${SOURCE}/libraries/engine/include" "-I${SOURCE}/libraries/v3dmm/include"
  "-I${SOURCE}/src/marcus" "-I${GENERATED}" "${SOURCE}/src/marcus/main.c" "${SOURCE}/src/marcus/ui.c"
  "${SOURCE}/src/marcus/settings.c"
  "${SOURCE}/src/marcus/text_dialog.c"
  "${SOURCE}/src/marcus/caption_editor.c"
  "${SOURCE}/src/marcus/costume_dialog.c"
  "${SOURCE}/src/marcus/sound_dialog.c"
  "${SOURCE}/src/marcus/sound_schedule.c"
  "${SOURCE}/src/marcus/waveform_core.c"
  "${SOURCE}/tests/marcus_waveform_test.c"
  "${SOURCE}/src/marcus/dialog_views.c"
  "${SOURCE}/src/marcus/dialog_widgets.c"
  "${SOURCE}/src/marcus/movie_browser.c"
  "${SOURCE}/src/marcus/movie_browser_preferences.c"
  "${SOURCE}/src/marcus/movie_zip.c"
  "${SOURCE}/src/marcus/scene_organiser.c"
  "${SOURCE}/src/marcus/camera_picker.c"
  "${SOURCE}/src/marcus/background_browser.c"
  "${SOURCE}/src/marcus/prop_fields.c"
  "${SOURCE}/src/marcus/prop_gizmo.c"
  "${SOURCE}/src/marcus/transform_gizmo.c"
  "${SOURCE}/src/marcus/transport_skin.c"
  "${SOURCE}/src/marcus/storyboard_view.c"
  "${SOURCE}/src/marcus/storyboard_editor.c"
  "${SOURCE}/src/marcus/storyboard_edit_data.c"
  "${SOURCE}/src/marcus/storyboard_draft.c"
  "${SOURCE}/src/marcus/storyboard_resource.c"
  "${SOURCE}/src/marcus/ribbon.c"
  "${SOURCE}/src/marcus/camera_guide.c"
  "${SOURCE}/src/marcus/camera_controls.c"
  "${SOURCE}/tests/marcus_camera_controls_test.c"
  "${SOURCE}/tests/marcus_gizmo_anchor_test.c"
  "${SOURCE}/src/marcus/renderer_ids.c"
  "${SOURCE}/tests/marcus_player_test.c" RESULT_VARIABLE result)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "Marcus frontend/C API sources failed C89 verification: ${result}")
endif()
