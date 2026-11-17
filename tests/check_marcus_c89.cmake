# External SDL/cimgui declarations use bool/64-bit extensions; the application's own
# sources and public bridge header must pass strict C89 diagnostics.
set(includes)
foreach(directory IN LISTS SDL_INCLUDES FREETYPE_INCLUDES CIMGUI_INCLUDES)
  list(APPEND includes -isystem "${directory}")
endforeach()
execute_process(COMMAND "${COMPILER}" -std=c89 -pedantic-errors
  -Werror=declaration-after-statement -Wall -Wextra -fsyntax-only ${includes}
  "-I${SOURCE}/src/marcus" "${SOURCE}/src/marcus/main.c" "${SOURCE}/src/marcus/ui.c"
  "${SOURCE}/src/marcus/settings.c"
  "${SOURCE}/src/marcus/text_dialog.c"
  "${SOURCE}/src/marcus/costume_dialog.c"
  "${SOURCE}/src/marcus/ribbon.c"
  "${SOURCE}/src/marcus/camera_guide.c"
  "${SOURCE}/src/marcus/camera_controls.c"
  "${SOURCE}/tests/marcus_camera_controls_test.c"
  "${SOURCE}/tests/marcus_gizmo_anchor_test.c"
  "${SOURCE}/src/marcus/renderer_ids.c"
  "${SOURCE}/tools/marcus_dialog_capture.c"
  "${SOURCE}/tests/marcus_player_test.c" RESULT_VARIABLE result)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "Marcus frontend/C API sources failed C89 verification: ${result}")
endif()
