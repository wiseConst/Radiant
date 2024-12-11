#pragma once

#include <Core/Core.hpp>
#include <Render/GfxContext.hpp>
#include <Render/GfxBuffer.hpp>
#include <Render/GfxTexture.hpp>

#include <aw2/aw2_defines.hpp>

namespace Radiant
{
    struct AW2PrimitiveData final
    {
        u32 MaterialID{};
        u32 IndexOffset{};
        u32 IndexCount{};

        Sphere BoundingSphere{};
        vk::CullModeFlags CullMode{vk::CullModeFlagBits::eNone};
        EAlphaMode AlphaMode{EAlphaMode::ALPHA_MODE_OPAQUE};
    };

    // indirection level: MeshletIndexBuffer -> MeshletVertexBuffer -> IndexBuffer -> PositionBuffer
    struct AW2MeshNode final
    {
        glm::mat4 TRS{1.0f};
        std::vector<AW2PrimitiveData> Primitives;
        u32 MeshletCount{};
        Unique<GfxBuffer> MeshletBuffer;
        Unique<GfxBuffer> Meshlet—ullDataBuffer;
        Unique<GfxBuffer> MeshletVertexBuffer;
        Unique<GfxBuffer> MeshletIndexBuffer;
        Unique<GfxBuffer> PositionBuffer;
        Unique<GfxBuffer> IndexBuffer;
        Unique<GfxBuffer> NormalsBuffer;
        Unique<GfxBuffer> TangentsBuffer;
        Unique<GfxBuffer> UVs0Buffer;
        Unique<GfxBuffer> Colors0Buffer;
    };

    struct AW2Scene final
    {
        std::vector<WeakPtr<AW2MeshNode>> RootNodes;
        std::vector<Shared<AW2MeshNode>> AllNodes;
        Unique<GfxBuffer> MaterialBuffer{nullptr};
    };

    struct AW2World final
    {
      public:
        AW2World() noexcept  = default;
        ~AW2World() noexcept = default;

        void LoadScene(const Unique<GfxContext>& gfxContext, const std::filesystem::path& scenePath) noexcept;

      private:
        std::vector<AW2Scene> m_Scenes;
    };

}  // namespace Radiant
