#pragma execution_character_set("utf-8")
#pragma once

// Windows 相关定义
#define WIN32_LEAN_AND_MEAN
//#define NOMINMAX
//#define _WIN32_WINNT 0x0A00

// DirectX 相关
#include <d3d11.h>
#include <tchar.h>

// ImGui 相关
#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"
#include <iostream>





void MainWindow();  // 函数声明





