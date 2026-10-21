package com.pixovr.apex;

/**
 * The native methods of apex_java (native/src/ApexJavaJni.c), one per function of
 * the core's C interface (include/apex/Apex.h). Strings cross as UTF-8 byte
 * arrays; a null array is a NULL string. Use {@link ApexClient} instead.
 */
final class ApexNativeBridge {
    /** Receives the core's callbacks, on the owner thread. */
    interface Listener {
        void onResult(long callId, int error, int httpStatus, byte[] message, byte[] json);

        void onLog(int logLevel, byte[] message);

        void onTelemetryPacket(byte[] packetJson);
    }

    /** The core ABI the bridge and this SDK were written for. */
    static final int EXPECTED_ABI_MAJOR = 0;
    static final int EXPECTED_ABI_MINOR = 7;

    private ApexNativeBridge() {
    }

    static native long create(int environment, byte[] headsetApiUrl, byte[] platformApiUrl, byte[] eventPath,
        int moduleId, byte[] moduleVersion, byte[] deviceId, byte[] deviceModel, byte[] platform, byte[] clientIp,
        double heartbeatIntervalSeconds, int requestTimeoutMilliseconds, int logLevel, int telemetrySink,
        byte[] telemetryUrl, double telemetryFlushIntervalSeconds, double telemetryPoseSamplesPerSecond,
        int telemetryMaximumRecordsPerPacket, int sessionFormat, Listener listener);

    static native void destroy(long clientHandle);

    static native void poll(long clientHandle, double nowSeconds);

    static native void cancel(long clientHandle, long callId);

    static native long ping(long clientHandle);

    static native long login(long clientHandle, byte[] username, byte[] password);

    static native long loginWithToken(long clientHandle, byte[] userToken);

    static native long quickIdGetUsers(long clientHandle, byte[] serialNumber);

    static native long quickIdLogin(long clientHandle, byte[] serialNumber, byte[] username);

    static native long generateLoginCode(long clientHandle, int userId);

    static native void logout(long clientHandle);

    static native long getUserModules(long clientHandle, int userId);

    static native long checkModuleAccess(long clientHandle, int moduleId);

    static native long getModuleList(long clientHandle, byte[] platformName);

    static native long getModulePage(long clientHandle, byte[] platformName, int lifecycleMask, int sortOrder,
        int pageIndex, int pageSize, boolean refresh);

    static native long sessionJoin(long clientHandle, byte[] moduleData);

    static native long sessionEvent(long clientHandle, byte[] moduleData);

    static native long sessionComplete(long clientHandle, byte[] moduleData);

    static native boolean isLoggedIn(long clientHandle);

    static native byte[] getCurrentUser(long clientHandle);

    static native long getSessionId(long clientHandle);

    static native void setDeviceId(long clientHandle, byte[] deviceId);

    static native void setSessionFormat(long clientHandle, int sessionFormat);

    static native void launchParseArgs(long clientHandle, byte[][] argumentValues);

    static native void launchParseUrl(long clientHandle, byte[] launchUrl);

    static native void launchSet(long clientHandle, byte[] key, byte[] launchValue);

    static native byte[] launchGet(long clientHandle, byte[] key);

    static native byte[] launchExitTarget(long clientHandle, byte[] nextTarget);

    /** Poses are float[6]: position x, y, z, then rotation x, y, z; null for none. */
    static native void telemetryObjectRegistered(long clientHandle, byte[] trackedId, byte[] objectName,
        byte[] meshName, float[] pose);

    static native void telemetryObjectUnregistered(long clientHandle, byte[] trackedId, float[] pose);

    static native void telemetryEngagementBegin(long clientHandle, byte[] trackedId, byte[] engagementName);

    static native void telemetryEngagementEnd(long clientHandle, byte[] trackedId, byte[] engagementName);

    static native void telemetryInteraction(long clientHandle, byte[] trackedId, byte[] actionName);

    static native void telemetryStepBegin(long clientHandle, byte[] stepName);

    static native void telemetryStepEnd(long clientHandle, byte[] stepName, boolean succeeded, double score);

    static native boolean telemetryPoseDue(long clientHandle);

    static native void telemetryPose(long clientHandle, byte[] trackedId, float[] pose);

    static native void telemetryFlush(long clientHandle);

    static native int abiVersion();

    static native byte[] versionString();

    static native byte[] builtinTransportName();

    static native boolean moduleVersionIsValid(byte[] moduleVersion);
}
