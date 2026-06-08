#include "Scene/SceneDefines.slangh"
#include "Utils/Math/MathConstants.slangh"

import Scene.Raytracing;
import Utils.Math.MathHelpers;
import Utils.Geometry.GeometryHelpers;
import Utils.Sampling.SampleGenerator;
import Rendering.Lights.LightHelpers;

//Helper struct to generate a photon form the lightSources
struct PhotonLightSample {
    float3 posW;
    float normalX;
    float3 dirW;
    float normalY;
    float3 flux;
    float normalZ;

    property float3 normal {
        get { return float3(normalX, normalY, normalZ); }
        set {
            normalX = newValue.x;
            normalY = newValue.y;
            normalZ = newValue.z;
        }
    }
};

bool generateLightSample(inout SampleGenerator sg, out PhotonLightSample ls)
{
    ls = {};

    float3 woLocal = float3(0);
    bool isEmissive = kUseEmissive;
    if (kUseAnalytic && kUseEmissive)
    {
        isEmissive = sampleNext1D(sg) > gMixedLightsAnalyticProbability;
    }

    if (isEmissive)
    {
        const uint lightCount = gScene.lightCollection.triangleCount;
        const uint selectedLight = min(uint(floor(lightCount * sampleNext1D(sg))), lightCount - 1);
        // Get emissive triangle data
        const EmissiveTriangle tri = gScene.lightCollection.getTriangle(selectedLight);
        const float3 barycentrics = sample_triangle(sampleNext2D(sg)); // Sample random triangle position
        ls.posW = tri.getPosition(barycentrics);
        ls.normal = tri.normal;

        // Sample emissive material of the triangle
        float2 uv = tri.getTexCoord(barycentrics);
        ls.flux = gScene.materials.evalEmissive(tri.materialID, uv) * tri.area;

        ls.posW = computeRayOrigin(ls.posW, ls.normal); // Offset as this light is real geometry

        // Sample random direction in a cosine lobe
        float lightDirPDF = 1.0;
        woLocal = sample_cosine_hemisphere_concentric(sampleNext2D(sg), lightDirPDF);
        if (lightDirPDF <= 0.) // Invalid direction
            return false;
        ls.flux *= M_PI;               // Lambert Emitter (or reduced cos/pdf)
        ls.flux *= lightCount;         // Inv light select pdf
        if (kUseAnalytic && kUseEmissive)
            ls.flux /= 1.0 - gMixedLightsAnalyticProbability; // Probability to select an emissive light
    } else
    {
        const uint lightCount = gScene.getLightCount();
        const uint selectedLight = min(uint(floor(lightCount * sampleNext1D(sg))), lightCount - 1);
        LightData lightData = gScene.getLight(selectedLight);
        // Only Point (and Spot) lights are supported
        if (lightData.type != uint(LightType::Point))
            return false;

        float spotAngle = lightData.cosOpeningAngle;
        float2 rndDir = sampleNext2D(sg); // Random direction variable
        // Depending on the opening angle, the light is a spot or point light
        if (lightData.openingAngle < M_2PI) // Spot Light
        {
            woLocal = sample_cone(rndDir, spotAngle);
        } else // Point Light
        {
            spotAngle = 0; // Just to be sure
            woLocal = sample_sphere(rndDir);
        }
        float invLightPdf = M_2PI * (1.0 - spotAngle);
        ls.normal = lightData.dirW;
        ls.posW = lightData.posW;
        ls.flux = lightData.intensity * invLightPdf;
        ls.flux *= lightCount; // Inv light select pdf
        if (kUseAnalytic && kUseEmissive)
            ls.flux /= gMixedLightsAnalyticProbability; // Probability to select an emissive light
    }

    // Local to world direction
    // Convert direction local -> world
    float3 bitangent = perp_stark(ls.normal);
    float3 tangent = cross(bitangent, ls.normal);
    ls.dirW = woLocal.x * tangent + woLocal.y * bitangent + woLocal.z * ls.normal;

    //Divide by the number of light paths
    ls.flux /= gDispatchDimension * gDispatchDimension;
    return true;
}
