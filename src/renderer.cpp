#include "renderer.hpp"

#include <utility>

namespace App {

void Renderer::log(std::string msg)
{
    std::lock_guard<std::mutex> lock(logMutex);
    logBuffer.append(std::move(msg));
    logBuffer.push_back('\n');
    logScrollToBottom = true;
}

void Renderer::LogPanel()
{
    if (!ImGui::Begin("Console")) {
        ImGui::End();
        return;
    }

    if (ImGui::Button("Clear")) {
        std::lock_guard<std::mutex> lock(logMutex);
        logBuffer.clear();
    }

    ImGui::Separator();
    ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(0, 0, 0, 255));
    ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(230, 230, 230, 255));

    if (ImGui::BeginChild("##console-output", ImVec2(0, 0), false,
                          ImGuiWindowFlags_HorizontalScrollbar)) {
        bool scrollToBottom = false;
        {
            std::lock_guard<std::mutex> lock(logMutex);
            ImGui::TextUnformatted(logBuffer.c_str(),
                                   logBuffer.c_str() + logBuffer.size());
            scrollToBottom = logScrollToBottom;
            logScrollToBottom = false;
        }

        if (scrollToBottom) {
            ImGui::SetScrollHereY(1.0f);
        }
    }

    ImGui::EndChild();
    ImGui::PopStyleColor(2);
    ImGui::End();
}

} // namespace App
