#include "ZEngine/UI/ImGuiLayer.h"

#include "ZEngine/Core/Window.h"
#include "ZEngine/Renderer/Renderer.h"
#include "ZEngine/Renderer/VulkanContext.h"

#include <GLFW/glfw3.h>
#include <ImGuizmo.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>

#include <filesystem>

namespace ze {

ImGuiLayer::ImGuiLayer(Window& window, Renderer& renderer, const std::string& iniPath)
    : m_Renderer(renderer), m_IniPath(iniPath)
{
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_DockingEnable;
    io.IniFilename = m_IniPath.c_str();
    io.ConfigWindowsMoveFromTitleBarOnly = true;

    ApplyTheme();
    LoadFonts();

    ImGui_ImplGlfw_InitForVulkan(window.Handle(), true);

    VulkanContext& ctx = renderer.Context();
    m_ColorFormat = renderer.ScreenFormat();

    ImGui_ImplVulkan_InitInfo info{};
    info.ApiVersion = VK_API_VERSION_1_3;
    info.Instance = ctx.Instance();
    info.PhysicalDevice = ctx.PhysicalDevice();
    info.Device = ctx.Device();
    info.QueueFamily = ctx.GraphicsQueueFamily();
    info.Queue = ctx.GraphicsQueue();
    info.DescriptorPoolSize = 1024;
    info.MinImageCount = 2;
    info.ImageCount = std::max(2u, renderer.ScreenImageCount());
    info.UseDynamicRendering = true;
    info.PipelineInfoMain.PipelineRenderingCreateInfo = {VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
    info.PipelineInfoMain.PipelineRenderingCreateInfo.colorAttachmentCount = 1;
    info.PipelineInfoMain.PipelineRenderingCreateInfo.pColorAttachmentFormats = &m_ColorFormat;
    info.CheckVkResultFn = [](VkResult r) {
        if (r != VK_SUCCESS)
            Log::Error("ImGui Vulkan error: {}", VkResultString(r));
    };
    ImGui_ImplVulkan_Init(&info);
}

ImGuiLayer::~ImGuiLayer()
{
    m_Renderer.WaitIdle();
    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
}

void ImGuiLayer::BeginFrame()
{
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
    ImGuizmo::BeginFrame();
}

void ImGuiLayer::Render(VkCommandBuffer cmd)
{
    ImGui::Render();
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd);
}

ImTextureID ImGuiLayer::TextureFor(VkImageView view, VkImageView& cachedView, VkDescriptorSet& cachedSet)
{
    if (cachedView != view) {
        if (cachedSet)
            ImGui_ImplVulkan_RemoveTexture(cachedSet);
        cachedSet = ImGui_ImplVulkan_AddTexture(view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        cachedView = view;
    }
    return static_cast<ImTextureID>(reinterpret_cast<uintptr_t>(cachedSet));
}

ImTextureID ImGuiLayer::Texture(RenderTarget& target)
{
    return TextureFor(target.color.view, target.uiView, target.uiTexture);
}

ImTextureID ImGuiLayer::Texture(ze::Texture& texture)
{
    return TextureFor(texture.image.view, texture.uiView, texture.uiTexture);
}

void ImGuiLayer::ReleaseTexture(RenderTarget& target)
{
    if (target.uiTexture)
        ImGui_ImplVulkan_RemoveTexture(target.uiTexture);
    target.uiTexture = VK_NULL_HANDLE;
    target.uiView = VK_NULL_HANDLE;
}

void ImGuiLayer::LoadFonts()
{
    ImGuiIO& io = ImGui::GetIO();
    const char* candidates[] = {
        "C:/Windows/Fonts/segoeui.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/TTF/DejaVuSans.ttf",
    };
    for (const char* path : candidates) {
        if (std::filesystem::exists(path)) {
            io.Fonts->AddFontFromFileTTF(path, 16.0f);
            return;
        }
    }
    ImFontConfig config;
    config.SizePixels = 15.0f;
    io.Fonts->AddFontDefault(&config);
}

void ImGuiLayer::ApplyTheme()
{
    // Dark theme close to Unity's editor.
    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 3.0f;
    style.FrameRounding = 3.0f;
    style.PopupRounding = 3.0f;
    style.GrabRounding = 3.0f;
    style.TabRounding = 3.0f;
    style.ScrollbarRounding = 3.0f;
    style.WindowBorderSize = 1.0f;
    style.FrameBorderSize = 0.0f;
    style.WindowPadding = ImVec2(8.0f, 6.0f);
    style.FramePadding = ImVec2(6.0f, 3.0f);
    style.ItemSpacing = ImVec2(6.0f, 4.0f);
    style.IndentSpacing = 14.0f;

    ImVec4* c = style.Colors;
    auto rgb = [](int r, int g, int b, float a = 1.0f) { return ImVec4(r / 255.0f, g / 255.0f, b / 255.0f, a); };
    c[ImGuiCol_Text] = rgb(210, 210, 210);
    c[ImGuiCol_TextDisabled] = rgb(120, 120, 120);
    c[ImGuiCol_WindowBg] = rgb(56, 56, 56);
    c[ImGuiCol_ChildBg] = rgb(56, 56, 56, 0.0f);
    c[ImGuiCol_PopupBg] = rgb(45, 45, 45);
    c[ImGuiCol_Border] = rgb(30, 30, 30);
    c[ImGuiCol_FrameBg] = rgb(42, 42, 42);
    c[ImGuiCol_FrameBgHovered] = rgb(50, 50, 50);
    c[ImGuiCol_FrameBgActive] = rgb(36, 36, 36);
    c[ImGuiCol_TitleBg] = rgb(40, 40, 40);
    c[ImGuiCol_TitleBgActive] = rgb(40, 40, 40);
    c[ImGuiCol_TitleBgCollapsed] = rgb(40, 40, 40);
    c[ImGuiCol_MenuBarBg] = rgb(40, 40, 40);
    c[ImGuiCol_ScrollbarBg] = rgb(48, 48, 48);
    c[ImGuiCol_CheckMark] = rgb(200, 200, 200);
    c[ImGuiCol_SliderGrab] = rgb(110, 110, 110);
    c[ImGuiCol_SliderGrabActive] = rgb(140, 140, 140);
    c[ImGuiCol_Button] = rgb(88, 88, 88);
    c[ImGuiCol_ButtonHovered] = rgb(103, 103, 103);
    c[ImGuiCol_ButtonActive] = rgb(70, 96, 124);
    c[ImGuiCol_Header] = rgb(70, 70, 70);
    c[ImGuiCol_HeaderHovered] = rgb(80, 80, 80);
    c[ImGuiCol_HeaderActive] = rgb(44, 93, 135);
    c[ImGuiCol_Separator] = rgb(35, 35, 35);
    c[ImGuiCol_Tab] = rgb(45, 45, 45);
    c[ImGuiCol_TabHovered] = rgb(70, 70, 70);
    c[ImGuiCol_TabSelected] = rgb(60, 60, 60);
    c[ImGuiCol_TabDimmed] = rgb(45, 45, 45);
    c[ImGuiCol_TabDimmedSelected] = rgb(56, 56, 56);
    c[ImGuiCol_TabSelectedOverline] = rgb(58, 121, 187);
    c[ImGuiCol_DockingPreview] = rgb(58, 121, 187, 0.6f);
    c[ImGuiCol_DockingEmptyBg] = rgb(30, 30, 30);
    c[ImGuiCol_NavCursor] = rgb(58, 121, 187);
    c[ImGuiCol_DragDropTarget] = rgb(58, 121, 187);
}

} // namespace ze
