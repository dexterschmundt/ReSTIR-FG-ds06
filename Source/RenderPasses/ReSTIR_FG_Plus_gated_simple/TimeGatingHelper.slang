#pragma once
#include "Utils/Math/MathConstants.slangh"

import StructsAndHelpers;

cbuffer Gate
{
    bool enableTimeGating;
    float gateValue;
    float gateTol;
    uint _pad; 
}

// Helper struct for Gates so i can access its data with point notation
struct Gate
{
    float value;
    float tol;

    __init(float value, float tol) {
        this.value = value;
        this.tol = tol;
    }

    // property float lowerBound {
    //     get { return value - tol; }
    // }

    // property float upperBound {
    //     get { return value + tol; }
    // }

    // totalPathDistance = LightPathDistance + CamPathDistance
    // rejects paths with negative lentgh //TODO maybe optimize this by just enforcing it with how gate value and tol can be set or just make it lower and upper on cpu side
    bool pass(float totalPathDistance) {
        return ((0 <= totalPathDistance)
        && (value - tol) <= totalPathDistance)
        && (totalPathDistance <= (value + tol));
    }
}

static const Gate gate = Gate(gateValue, gateTol);

