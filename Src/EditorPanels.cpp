#include "EditorPanels.h"

#include "imgui.h"

namespace EditorPanels
{
    static EditorPanel g_scene{
        "Scene",
        true,
        []()
        {
            auto& panel = Scene();
            if (!panel.open)
                return;
            if (ImGui::Begin(panel.name, &panel.open))
            {
                ImGui::Text("\xef\x8e\xae Scene View");
            }
            ImGui::End();
        }
    };

    static EditorPanel g_hierarchy{
        "Hierarchy",
        true,
        []()
        {
            auto& panel = Hierarchy();
            if (!panel.open)
                return;
            if (ImGui::Begin(panel.name, &panel.open))
            {
                ImGui::Text("\xef\x8c\xb3 Scene Hierarchy");
            }
            ImGui::End();
        }
    };

    static EditorPanel g_inspector{
        "Inspector",
        true,
        []()
        {
            auto& panel = Inspector();
            if (!panel.open)
                return;
            if (ImGui::Begin(panel.name, &panel.open))
            {
                ImGui::Text("\xef\x94\x8d Inspector");
            }
            ImGui::End();
        }
    };

    static EditorPanel g_assets{
        "Assets",
        true,
        []()
        {
            auto& panel = Assets();
            if (!panel.open)
                return;
            if (ImGui::Begin(panel.name, &panel.open))
            {
                ImGui::Text("\xef\x93\xa6 Asset Browser");
            }
            ImGui::End();
        }
    };

    EditorPanel& Scene() { return g_scene; }
    EditorPanel& Hierarchy() { return g_hierarchy; }
    EditorPanel& Inspector() { return g_inspector; }
    EditorPanel& Assets() { return g_assets; }

    void DrawAll()
    {
        if (g_scene.open) g_scene.draw();
        if (g_hierarchy.open) g_hierarchy.draw();
        if (g_inspector.open) g_inspector.draw();
        if (g_assets.open) g_assets.draw();
    }
}
