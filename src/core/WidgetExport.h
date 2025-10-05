#pragma once

// Widget DLL 導出宏
#ifdef _WIN32
    #ifdef WIDGET_EXPORTS
        #define WIDGET_API __declspec(dllexport)
    #else
        #define WIDGET_API __declspec(dllimport)
    #endif
#else
    #define WIDGET_API
#endif

// Widget 工廠函式類型定義
class IWidget;
typedef IWidget* (*CreateWidgetFunc)(void* params);
typedef void (*DestroyWidgetFunc)(IWidget* widget);
typedef void (*ExecuteCommandFunc)(IWidget* widget, int commandId);

// Widget 自定義命令 ID
#define WIDGET_CMD_CREATE_NEW       1001
#define WIDGET_CMD_CLEAR_ALL_DATA   1002

// 每個 Widget DLL 必須導出這些函式
extern "C" {
    WIDGET_API IWidget* CreateWidget(void* params);
    WIDGET_API void DestroyWidget(IWidget* widget);
    WIDGET_API const wchar_t* GetWidgetName();
    WIDGET_API const wchar_t* GetWidgetVersion();
    WIDGET_API void ExecuteCommand(IWidget* widget, int commandId);
}
