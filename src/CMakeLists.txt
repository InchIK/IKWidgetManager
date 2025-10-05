# Widget 管理器核心庫 (保持為靜態庫，供 DLL 使用)
add_library(WidgetCore STATIC
    core/IWidget.h
    core/WidgetManager.h
    core/WidgetManager.cpp
    core/WidgetExport.h
    core/PluginLoader.h
    core/PluginLoader.cpp
)

target_include_directories(WidgetCore PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# 設定 UTF-8 編碼
if(MSVC)
    target_compile_options(WidgetCore PRIVATE /utf-8)
endif()

# Fences Widget DLL
add_library(FencesWidget SHARED
    widgets/FencesWidget.h
    widgets/FencesWidget.cpp
)

target_link_libraries(FencesWidget PRIVATE
    WidgetCore
)

target_include_directories(FencesWidget PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# 定義導出宏
target_compile_definitions(FencesWidget PRIVATE WIDGET_EXPORTS)

# 設定 UTF-8 編碼
if(MSVC)
    target_compile_options(FencesWidget PRIVATE /utf-8)
endif()

# 設定輸出目錄（與 exe 同目錄）
set_target_properties(FencesWidget PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}
)

# Sticky Notes Widget DLL
add_library(StickyNotesWidget SHARED
    widgets/StickyNotesWidget.h
    widgets/StickyNotesWidget.cpp
)

target_link_libraries(StickyNotesWidget PRIVATE
    WidgetCore
)

target_include_directories(StickyNotesWidget PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# 定義導出宏
target_compile_definitions(StickyNotesWidget PRIVATE WIDGET_EXPORTS)

# 設定 UTF-8 編碼
if(MSVC)
    target_compile_options(StickyNotesWidget PRIVATE /utf-8)
endif()

# 設定輸出目錄（與 exe 同目錄）
set_target_properties(StickyNotesWidget PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}
)

# 主程序（只依賴 WidgetCore，不直接鏈接 Widget DLL）
add_executable(DesktopWidgetManager WIN32
    main.cpp
)

target_link_libraries(DesktopWidgetManager PRIVATE
    WidgetCore
)

# 設定 UTF-8 編碼
if(MSVC)
    target_compile_options(DesktopWidgetManager PRIVATE /utf-8)
endif()

# Windows 特定庫
if(WIN32)
    target_link_libraries(DesktopWidgetManager PRIVATE
        comctl32
        gdi32
        user32
        shell32
    )
    target_link_libraries(FencesWidget PRIVATE
        comctl32
        gdi32
        user32
        shell32
        dwmapi
    )
    target_link_libraries(StickyNotesWidget PRIVATE
        comctl32
        gdi32
        user32
        shell32
        dwmapi
    )
endif()
