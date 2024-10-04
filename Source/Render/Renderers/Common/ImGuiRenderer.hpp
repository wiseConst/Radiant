#pragma once

#include <Core/Core.hpp>
#include <imgui.h>
#include <vulkan/vulkan.hpp>

namespace Radiant
{

    class GfxContext;
    class RenderGraph;
    class ImGuiRenderer final : private Uncopyable, private Unmovable
    {
      public:
        ImGuiRenderer(const Unique<GfxContext>& gfxContext) noexcept : m_GfxContext(gfxContext) { Init(); }
        ~ImGuiRenderer() noexcept;

        void RenderFrame(const vk::Extent2D& viewportExtent, Unique<RenderGraph>& renderGraph, const std::string& backbufferName,
                         std::function<void()>&& uiFunc) noexcept;

      private:
        const Unique<GfxContext>& m_GfxContext;
        vk::UniqueDescriptorPool m_ImGuiPool{};

        constexpr ImGuiRenderer() noexcept = delete;
        void Init() noexcept;
    };

    // NOTE: Credits to https://github.com/Raikiri/LegitProfiler/tree/master
    namespace ImGuiUtils
    {
        class ProfilerGraph final
        {
          public:
            u32 FrameWidth{0};
            u32 FrameSpacing{0};
            bool bUseColoredLegendText{false};
            bool bStopProfiling{false};

            constexpr ProfilerGraph() noexcept = delete;
            ProfilerGraph(const u64 framesCount) noexcept
            {
                m_FrameDatas.resize(framesCount);
                for (auto& frame : m_FrameDatas)
                    frame.tasks.reserve(100);
                FrameWidth            = 3;
                FrameSpacing          = 1;
                bUseColoredLegendText = false;
            }
            ~ProfilerGraph() noexcept = default;

            void LoadFrameData(const std::vector<ProfilerTask>& tasks) noexcept
            {
                if (bStopProfiling) return;

                auto& currFrame = m_FrameDatas[m_CurrentFrameIndex];
                currFrame.tasks.clear();
                for (u64 taskIndex{}; taskIndex < tasks.size(); ++taskIndex)
                {
                    if (taskIndex == 0)
                        currFrame.tasks.emplace_back(tasks[taskIndex]);
                    else
                    {
                        if (tasks[taskIndex - 1].Color != tasks[taskIndex].Color || tasks[taskIndex - 1].Name != tasks[taskIndex].Name)
                            currFrame.tasks.emplace_back(tasks[taskIndex]);
                        else
                            currFrame.tasks.back().EndTime = tasks[taskIndex].EndTime;
                    }
                }
                currFrame.taskStatsIndex.resize(currFrame.tasks.size());

                for (u64 taskIndex{}; taskIndex < currFrame.tasks.size(); ++taskIndex)
                {
                    const auto& task = currFrame.tasks[taskIndex];
                    if (const auto it = m_TaskNameToStatsIndex.find(task.Name); it == m_TaskNameToStatsIndex.end())
                    {
                        m_TaskNameToStatsIndex[task.Name] = taskStats.size();
                        TaskStats taskStat                = {};
                        taskStat.maxTime                  = -1.0;
                        taskStats.emplace_back(taskStat);
                    }
                    currFrame.taskStatsIndex[taskIndex] = m_TaskNameToStatsIndex[task.Name];
                }
                m_CurrentFrameIndex = (m_CurrentFrameIndex + 1) % m_FrameDatas.size();

                RebuildTaskStats(m_CurrentFrameIndex, m_FrameDatas.size());
            }

            void RenderTimings(i32 graphWidth, i32 legendWidth, i32 height, i32 frameIndexOffset) noexcept
            {
                ImDrawList* drawList      = ImGui::GetWindowDrawList();
                const glm::vec2 widgetPos = (glm::vec2&)ImGui::GetCursorScreenPos();
                RenderGraph(drawList, widgetPos, glm::vec2(graphWidth, height), frameIndexOffset);
                RenderLegend(drawList, widgetPos + glm::vec2(graphWidth, 0.0f), glm::vec2(legendWidth, height), frameIndexOffset);
                ImGui::Dummy(ImVec2(f32(graphWidth + legendWidth), f32(height)));
            }

          private:
            void RebuildTaskStats(u64 endFrame, u64 framesCount) noexcept
            {
                for (auto& taskStat : taskStats)
                {
                    taskStat.maxTime       = -1.0f;
                    taskStat.priorityOrder = std::numeric_limits<u64>::max();
                    taskStat.onScreenIndex = std::numeric_limits<u64>::max();
                }

                for (u64 frameNumber{}; frameNumber < framesCount; ++frameNumber)
                {
                    u64 frameIndex = (endFrame - 1 - frameNumber + m_FrameDatas.size()) % m_FrameDatas.size();
                    auto& frame    = m_FrameDatas[frameIndex];
                    for (u64 taskIndex = 0; taskIndex < frame.tasks.size(); taskIndex++)
                    {
                        auto& task    = frame.tasks[taskIndex];
                        auto& stats   = taskStats[frame.taskStatsIndex[taskIndex]];
                        stats.maxTime = std::max(stats.maxTime, task.EndTime - task.StartTime);
                    }
                }
                std::vector<u64> statPriorities;
                statPriorities.resize(taskStats.size());
                for (u64 statIndex{}; statIndex < taskStats.size(); ++statIndex)
                    statPriorities[statIndex] = statIndex;

                std::sort(std::execution::par, statPriorities.begin(), statPriorities.end(),
                          [this](u64 left, u64 right) { return taskStats[left].maxTime > taskStats[right].maxTime; });
                for (u64 statNumber{}; statNumber < taskStats.size(); ++statNumber)
                {
                    const u64 statIndex                = statPriorities[statNumber];
                    taskStats[statIndex].priorityOrder = statNumber;
                }
            }

            void RenderGraph(ImDrawList* drawList, const glm::vec2& graphPos, const glm::vec2& graphSize, u64 frameIndexOffset) noexcept
            {
                Rect(drawList, graphPos, graphPos + graphSize, 0xffffffff, false);

                constexpr f32 heightThreshold = 1.0f;
                for (u64 frameNumber{}; frameNumber < m_FrameDatas.size(); ++frameNumber)
                {
                    const u64 frameIndex =
                        (m_CurrentFrameIndex - frameIndexOffset - 1 - frameNumber + 2 * m_FrameDatas.size()) % m_FrameDatas.size();
                    const glm::vec2 framePos =
                        graphPos + glm::vec2(graphSize.x - 1 - FrameWidth - (FrameWidth + FrameSpacing) * frameNumber, graphSize.y - 1);
                    if (framePos.x < graphPos.x + 1) break;

                    const glm::vec2 taskPos = framePos + glm::vec2(0.0f, 0.0f);
                    for (const auto& task : m_FrameDatas[frameIndex].tasks)
                    {
                        const f32 taskStartHeight = (f32(task.StartTime) / m_MaxFrameTime) * graphSize.y;
                        const f32 taskEndHeight   = (f32(task.EndTime) / m_MaxFrameTime) * graphSize.y;
                        if (abs(taskEndHeight - taskStartHeight) > heightThreshold)
                            Rect(drawList, taskPos + glm::vec2(0.0f, -taskStartHeight), taskPos + glm::vec2(FrameWidth, -taskEndHeight),
                                 task.Color, true);
                    }
                }
            }

            void RenderLegend(ImDrawList* drawList, const glm::vec2& legendPos, const glm::vec2& legendSize, u64 frameIndexOffset) noexcept
            {
                constexpr f32 markerLeftRectMargin   = 3.0f;
                constexpr f32 markerLeftRectWidth    = 5.0f;
                constexpr f32 markerMidWidth         = 30.0f;
                constexpr f32 markerRightRectWidth   = 10.0f;
                constexpr f32 markerRigthRectMargin  = 3.0f;
                constexpr f32 markerRightRectHeight  = 10.0f;
                constexpr f32 markerRightRectSpacing = 4.0f;
                constexpr f32 nameOffset             = 30.0f;
                constexpr glm::vec2 textMargin       = glm::vec2(5.0f, -3.0f);

                const auto& currFrame =
                    m_FrameDatas[(m_CurrentFrameIndex - frameIndexOffset - 1 + 2 * m_FrameDatas.size()) % m_FrameDatas.size()];
                const u64 maxTasksCount = u64(legendSize.y / (markerRightRectHeight + markerRightRectSpacing));

                for (auto& taskStat : taskStats)
                    taskStat.onScreenIndex = std::numeric_limits<u64>::max();

                const u64 tasksToShow = std::min<u64>(taskStats.size(), maxTasksCount);
                u64 tasksShownCount   = 0;
                for (u64 taskIndex{}; taskIndex < currFrame.tasks.size(); ++taskIndex)
                {
                    const auto& task = currFrame.tasks[taskIndex];
                    auto& stat       = taskStats[currFrame.taskStatsIndex[taskIndex]];

                    if (stat.priorityOrder >= tasksToShow) continue;

                    if (stat.onScreenIndex == std::numeric_limits<u64>::max())
                        stat.onScreenIndex = tasksShownCount++;
                    else
                        continue;

                    const f32 taskStartHeight = (f32(task.StartTime) / m_MaxFrameTime) * legendSize.y;
                    const f32 taskEndHeight   = (f32(task.EndTime) / m_MaxFrameTime) * legendSize.y;

                    const glm::vec2 markerLeftRectMin = legendPos + glm::vec2(markerLeftRectMargin, legendSize.y - taskStartHeight);
                    const glm::vec2 markerLeftRectMax = markerLeftRectMin + glm::vec2(markerLeftRectWidth, -taskEndHeight);

                    const glm::vec2 markerRightRectMin =
                        legendPos + glm::vec2(markerLeftRectMargin + markerLeftRectWidth + markerMidWidth,
                                              legendSize.y - markerRigthRectMargin -
                                                  (markerRightRectHeight + markerRightRectSpacing) * stat.onScreenIndex);
                    const glm::vec2 markerRightRectMax = markerRightRectMin + glm::vec2(markerRightRectWidth, -markerRightRectHeight);
                    RenderTaskMarker(drawList, markerLeftRectMin, markerLeftRectMax, markerRightRectMin, markerRightRectMax, task.Color);

                    const u32 textColor = bUseColoredLegendText ? task.Color : Colors::imguiText;

                    const f32 taskTimeMs = f32(task.EndTime - task.StartTime);
                    std::ostringstream timeText;
                    timeText.precision(2);
                    timeText << std::fixed << std::string("[") << (taskTimeMs * 1000.0f);

                    Text(drawList, markerRightRectMax + textMargin, textColor, timeText.str().data());
                    Text(drawList, markerRightRectMax + textMargin + glm::vec2(nameOffset, 0.0f), textColor,
                         (std::string("   ms] ") + task.Name).data());
                }
            }

            static void Rect(ImDrawList* drawList, const glm::vec2& minPoint, const glm::vec2& maxPoint, u32 col,
                             bool filled = true) noexcept
            {
                if (filled)
                    drawList->AddRectFilled((ImVec2&)minPoint, (ImVec2&)maxPoint, col);
                else
                    drawList->AddRect((ImVec2&)minPoint, (ImVec2&)maxPoint, col);
            }

            static void Text(ImDrawList* drawList, const glm::vec2& point, u32 col, const std::string_view text) noexcept
            {
                drawList->AddText((ImVec2&)point, col, text.data());
            }

            static void Triangle(ImDrawList* drawList, const std::array<glm::vec2, 3>& points, u32 col, bool filled = true) noexcept
            {
                if (filled)
                    drawList->AddTriangleFilled((ImVec2&)points[0], (ImVec2&)points[1], (ImVec2&)points[2], col);
                else
                    drawList->AddTriangle((ImVec2&)points[0], (ImVec2&)points[1], (ImVec2&)points[2], col);
            }

            static void RenderTaskMarker(ImDrawList* drawList, const glm::vec2& leftMinPoint, const glm::vec2& leftMaxPoint,
                                         const glm::vec2& rightMinPoint, const glm::vec2& rightMaxPoint, u32 col) noexcept
            {
                Rect(drawList, leftMinPoint, leftMaxPoint, col, true);
                Rect(drawList, rightMinPoint, rightMaxPoint, col, true);
                const std::array<ImVec2, 4> points = {ImVec2(leftMaxPoint.x, leftMinPoint.y), ImVec2(leftMaxPoint.x, leftMaxPoint.y),
                                                      ImVec2(rightMinPoint.x, rightMaxPoint.y), ImVec2(rightMinPoint.x, rightMinPoint.y)};
                drawList->AddConvexPolyFilled(points.data(), (i32)points.size(), col);
            }
            struct FrameData
            {
                std::vector<ProfilerTask> tasks;
                std::vector<u64> taskStatsIndex;
            };

            struct TaskStats
            {
                f64 maxTime;
                u64 priorityOrder;
                u64 onScreenIndex;
            };
            std::vector<TaskStats> taskStats;
            UnorderedMap<std::string, u64> m_TaskNameToStatsIndex;

            std::vector<FrameData> m_FrameDatas;
            u64 m_CurrentFrameIndex{0};
            f32 m_MaxFrameTime{1.0f / 30.0f};
        };

        struct ProfilersWindow final
        {
          public:
            ProfilersWindow() noexcept  = default;
            ~ProfilersWindow() noexcept = default;

            void Render() noexcept
            {
                ++m_FrameCounter;
                {
                    const auto currentFrameTime = std::chrono::system_clock::now();
                    const f32 fpsDeltaTime      = std::chrono::duration<f32>(currentFrameTime - m_PrevFpsFrameTime).count();
                    if (fpsDeltaTime > 0.5f)
                    {
                        m_AvgFrameTime     = fpsDeltaTime / (f32)m_FrameCounter;
                        m_FrameCounter     = 0;
                        m_PrevFpsFrameTime = currentFrameTime;
                    }
                }

                std::stringstream title;
                title.precision(2);
                title << std::fixed << "Legit Profiler [" << 1.0f / m_AvgFrameTime << "FPS\t" << m_AvgFrameTime * 1000.0f
                      << "ms]###ProfilerWindow";
                ImGui::Begin(title.str().data(), 0, ImGuiWindowFlags_NoScrollbar);
                const ImVec2 canvasSize = ImGui::GetContentRegionAvail();

                const u32 sizeMargin           = (u32)ImGui::GetStyle().ItemSpacing.y;
                constexpr u32 maxGraphHeight   = 300;
                const u32 availableGraphHeight = ((u32)canvasSize.y - sizeMargin) / 2;
                const u32 graphHeight          = std::min(maxGraphHeight, availableGraphHeight);
                constexpr u32 legendWidth      = 200;
                const u32 graphWidth           = (u32)canvasSize.x - legendWidth;
                m_GPUGraph.RenderTimings(graphWidth, legendWidth, graphHeight, m_FrameOffset);
                m_CPUGraph.RenderTimings(graphWidth, legendWidth, graphHeight, m_FrameOffset);
                if (graphHeight * 2 + sizeMargin + sizeMargin < canvasSize.y)
                {
                    ImGui::Columns(2);
                    ImGui::Checkbox("Stop Profiling", &m_bStopProfiling);
                    ImGui::Checkbox("Colored Legend Text", &m_bUseColoredLegendText);
                    ImGui::DragInt("Frame Offset", &m_FrameOffset, 1.0f, 0, 400);
                    ImGui::NextColumn();

                    constexpr u32 frameWidthMin = 1, frameWidthMax = 4;
                    ImGui::SliderScalar("Frame Width", ImGuiDataType_U32, &m_FrameWidth, &frameWidthMin, &frameWidthMax);
                    constexpr u32 frameSpacingMin = 0, frameSpacingMax = 2;
                    ImGui::SliderScalar("Frame Spacing", ImGuiDataType_U32, &m_FrameSpacing, &frameSpacingMin, &frameSpacingMax);
                    ImGui::SliderFloat("Transparency", &ImGui::GetStyle().Colors[ImGuiCol_WindowBg].w, 0.0f, 1.0f);
                    ImGui::Columns(1);
                }
                if (!m_bStopProfiling) m_FrameOffset = 0;
                m_GPUGraph.FrameWidth            = m_FrameWidth;
                m_GPUGraph.FrameSpacing          = m_FrameSpacing;
                m_GPUGraph.bUseColoredLegendText = m_bUseColoredLegendText;
                m_GPUGraph.bStopProfiling        = m_bStopProfiling;
                m_CPUGraph.FrameWidth            = m_FrameWidth;
                m_CPUGraph.FrameSpacing          = m_FrameSpacing;
                m_CPUGraph.bUseColoredLegendText = m_bUseColoredLegendText;
                m_CPUGraph.bStopProfiling        = m_bStopProfiling;

                ImGui::End();
            }

            ProfilerGraph m_CPUGraph = ProfilerGraph(300);
            ProfilerGraph m_GPUGraph = ProfilerGraph(300);
            u32 m_FrameWidth{3};
            u32 m_FrameSpacing{1};
            bool m_bUseColoredLegendText{true};
            bool m_bStopProfiling{false};
            i32 m_FrameOffset{0};
            using TimePoi32 = std::chrono::time_point<std::chrono::system_clock>;
            TimePoi32 m_PrevFpsFrameTime{std::chrono::system_clock::now()};
            u64 m_FrameCounter{0};
            f32 m_AvgFrameTime{1.0f};
        };
    }  // namespace ImGuiUtils

}  // namespace Radiant
