#pragma once

#include "Core/Core.hpp"

namespace xor
{
    struct FPSCamera
    {
        struct Keys
        {
            int forward   = 0;
            int backward  = 0;
            int left      = 0;
            int right     = 0;
            int lookUp    = 0;
            int lookDown  = 0;
            int lookLeft  = 0;
            int lookRight = 0;
            int moveFast  = 0;
        } keys;

        float3 position = 0;
        Angle azimuth   = Angle(0);
        Angle elevation = Angle(0);
        float speed     = 10;
        float turnSpeed = .075f;

        void update(const Window &window)
        {
            float x = 0;
            float z = 0;
            float s = 1;

            if (window.isKeyHeld(keys.moveFast)) s = 10;

            if (window.isKeyHeld(keys.forward))  z -= 1;
            if (window.isKeyHeld(keys.backward)) z += 1;
            if (window.isKeyHeld(keys.left))     x -= 1;
            if (window.isKeyHeld(keys.right))    x += 1;

            Matrix M = orientation();

            if (x != 0)
            {
                x *= speed * s;
                position += M.getRotationXAxis() * x;
            }

            if (z != 0)
            {
                z *= speed * s;
                position += M.getRotationZAxis() * z;
            }

            if (window.isKeyHeld(keys.lookLeft))  azimuth.radians   += turnSpeed;
            if (window.isKeyHeld(keys.lookRight)) azimuth.radians   -= turnSpeed;
            if (window.isKeyHeld(keys.lookUp))    elevation.radians += turnSpeed;
            if (window.isKeyHeld(keys.lookDown))  elevation.radians -= turnSpeed;
        }

        Matrix orientation() const
        {
            Matrix A = Matrix::axisAngle({ 0, 1, 0 }, azimuth);
            Matrix E = Matrix::axisAngle({ 1, 0, 0 }, elevation);
            return A * E;
        }

        Matrix viewMatrix() const
        {
            Matrix T = Matrix::translation(-position);
            Matrix R = orientation().transpose();
            return R * T;
        }
    };
}
