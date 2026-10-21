package com.pixovr.apex;

/** A pose for telemetry: meters and Euler degrees in Unity's axes (x right, y up, z forward). */
public final class ApexPose {
    private final float[] values;

    public ApexPose(float positionX, float positionY, float positionZ, float rotationX, float rotationY,
        float rotationZ) {
        values = new float[] {positionX, positionY, positionZ, rotationX, rotationY, rotationZ};
    }

    public static ApexPose atPosition(float positionX, float positionY, float positionZ) {
        return new ApexPose(positionX, positionY, positionZ, 0.0f, 0.0f, 0.0f);
    }

    /** Position x, y, z, then rotation x, y, z, as the native bridge takes it. */
    float[] toArray() {
        return values.clone();
    }

    static float[] toArray(ApexPose pose) {
        return pose != null ? pose.toArray() : null;
    }
}
