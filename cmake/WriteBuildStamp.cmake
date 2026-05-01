string(TIMESTAMP NOW "%Y-%m-%d %H:%M:%S")
file(WRITE "${OUT}" "extern const char kszBuildStamp[] = \"${NOW}\";\n")
