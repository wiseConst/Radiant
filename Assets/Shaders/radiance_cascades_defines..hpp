

uint GetRayIndex(const float2 pixelCoord, const uint2 probeDimensions)
{
    const uint2 probeCoords = pixelCoord % probeDimensions;
    return probeCoords.y * probeDimensions.x + probeCoords.x;
}

float GetRayAngle(const uint rayIndex, const uint2 probeDimensions)
{
    const uint rayCount = probeDimensions.x * probeDimensions.y;
    return rayIndex / rayCount * PI * 2;
}

