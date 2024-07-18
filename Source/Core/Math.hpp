#pragma once

#define GLM_ENABLE_EXPERIMENTAL
#define GLM_FORCE_QUAT_DATA_WXYZ     // to store quat data as w,x,y,z instead of x,y,z,w
#define GLM_FORCE_DEPTH_ZERO_TO_ONE  // to have depth range [0, 1]

#include <glm/gtc/constants.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/matrix_inverse.hpp>

#include <glm/gtx/matrix_decompose.hpp>
#include <glm/gtx/string_cast.hpp>
#include <glm/gtx/compatibility.hpp>

namespace Radiant
{

    namespace Math
    {

        static constexpr float s_KINDA_SMALL_FLOAT_NUMBER = 10.e-4f;
        static constexpr float s_SMALL_FLOAT_NUMBER       = 10.e-9f;

        static constexpr double s_KINDA_SMALL_DOUBLE_NUMBER = 10.e-4;
        static constexpr double s_SMALL_DOUBLE_NUMBER       = 10.e-9;

    }  // namespace Math

}  // namespace Radiant
