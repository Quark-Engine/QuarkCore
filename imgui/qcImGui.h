#pragma once

#include "QuarkCore/QuarkCore.hpp"
#include "imgui.h"

QCAPI void qcImGuiBeginInitImGui();
QCAPI void qcImGuiEndInitImGui();
QCAPI void qcImGuiSetup(bool dark);
QCAPI void qcImGuiBegin();
QCAPI void qcImGuiBeginDelta(float deltaTime);
QCAPI void qcImGuiEnd();
QCAPI void qcImGuiShutdown();

QCAPI void qcImGuiImage(const qc::Texture2D* image);
QCAPI bool qcImGuiImageButton(const char* name, const qc::Texture2D* image);
QCAPI bool qcImGuiImageButtonSize(const char* name, const qc::Texture2D* image, qc::Vec2 size);
QCAPI void qcImGuiImageSize(const qc::Texture2D* image, int width, int height);
QCAPI void qcImGuiImageSizeV(const qc::Texture2D* image, qc::Vec2 size);
QCAPI void qcImGuiImageRect(const qc::Texture2D* image, int destWidth, int destHeight, qc::Rectangle sourceRect);
QCAPI void qcImGuiImageRenderTexture(const qc::RenderTexture2D* target);
QCAPI void qcImGuiImageRenderTextureFit(const qc::RenderTexture2D* target, bool center);

QCAPI bool ImGui_ImplQc_Init();
QCAPI void ImGui_ImplQc_Shutdown();
QCAPI void ImGui_ImplQc_NewFrame();
QCAPI void ImGui_ImplQc_UpdateTexture(ImTextureData* tex);
QCAPI void ImGui_ImplQc_RenderDrawData(ImDrawData* draw_data);
QCAPI bool ImGui_ImplQc_ProcessEvents();
