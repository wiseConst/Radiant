#include <pch.h>
#include "Mesh.hpp"

#include <fastgltf/glm_element_traits.hpp>
#include <fastgltf/types.hpp>

#include <Render/GfxContext.hpp>

namespace Radiant
{

    namespace FastGltfUtils
    {
        NODISCARD static vk::Filter FastGltfFilterToVulkan(const fastgltf::Filter filter)
        {
            switch (filter)
            {
                case fastgltf::Filter::Linear:
                case fastgltf::Filter::LinearMipMapLinear:
                case fastgltf::Filter::LinearMipMapNearest: return vk::Filter::eLinear;

                case fastgltf::Filter::Nearest:
                case fastgltf::Filter::NearestMipMapLinear:
                case fastgltf::Filter::NearestMipMapNearest:
                default: return vk::Filter::eNearest;
            }
        }

        NODISCARD static vk::SamplerMipmapMode FastGltfMipMapModeToVulkan(const fastgltf::Filter filter)
        {
            switch (filter)
            {
                case fastgltf::Filter::LinearMipMapLinear:
                case fastgltf::Filter::NearestMipMapLinear: return vk::SamplerMipmapMode::eLinear;

                case fastgltf::Filter::LinearMipMapNearest:
                case fastgltf::Filter::NearestMipMapNearest:
                default: return vk::SamplerMipmapMode::eNearest;
            }
        }

    }  // namespace FastGltfUtils



StaticMesh::StaticMesh(const std::string& meshPath) noexcept {}

}  // namespace Radiant
