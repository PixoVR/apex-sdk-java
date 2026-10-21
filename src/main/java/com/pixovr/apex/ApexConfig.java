package com.pixovr.apex;

/**
 * Settings for an ApexClient (ApexConfig in the C interface). Unset values keep
 * the core's defaults.
 *
 * <pre>
 * ApexConfig config = new ApexConfig(ApexEnvironment.NA_STAGING, 42)
 *     .setModuleVersion("1.4.0")
 *     .setDeviceId(serialNumber);
 * </pre>
 */
public final class ApexConfig {
    ApexEnvironment environment;
    String headsetApiUrl;
    String platformApiUrl;
    String eventPath;
    int moduleId;
    String moduleVersion;
    String deviceId;
    String deviceModel;
    String platform;
    String clientIp;
    double heartbeatIntervalSeconds;
    int requestTimeoutMilliseconds;
    ApexLogLevel logLevel = ApexLogLevel.INFO;
    ApexTelemetrySink telemetrySink = ApexTelemetrySink.OFF;
    String telemetryUrl;
    double telemetryFlushIntervalSeconds;
    double telemetryPoseSamplesPerSecond;
    int telemetryMaximumRecordsPerPacket;
    ApexSessionFormat sessionFormat = ApexSessionFormat.DATA;

    /** moduleId is this module's platform ID. */
    public ApexConfig(ApexEnvironment environment, int moduleId) {
        this.environment = environment;
        this.moduleId = moduleId;
    }

    /** Overrides the environment's Headset API host; with CUSTOM, both hosts are required. */
    public ApexConfig setHeadsetApiUrl(String headsetApiUrl) {
        this.headsetApiUrl = headsetApiUrl;
        return this;
    }

    /** Overrides the environment's Platform API host. */
    public ApexConfig setPlatformApiUrl(String platformApiUrl) {
        this.platformApiUrl = platformApiUrl;
        return this;
    }

    /** Session event route on the Headset API; default "/event". */
    public ApexConfig setEventPath(String eventPath) {
        this.eventPath = eventPath;
        return this;
    }

    /** For example "1.4.0". */
    public ApexConfig setModuleVersion(String moduleVersion) {
        this.moduleVersion = moduleVersion;
        return this;
    }

    /** The device's serial number, for Quick ID and module access. */
    public ApexConfig setDeviceId(String deviceId) {
        this.deviceId = deviceId;
        return this;
    }

    public ApexConfig setDeviceModel(String deviceModel) {
        this.deviceModel = deviceModel;
        return this;
    }

    /** For example "android" or "windows". */
    public ApexConfig setPlatform(String platform) {
        this.platform = platform;
        return this;
    }

    public ApexConfig setClientIp(String clientIp) {
        this.clientIp = clientIp;
        return this;
    }

    /** Default 5. */
    public ApexConfig setHeartbeatIntervalSeconds(double heartbeatIntervalSeconds) {
        this.heartbeatIntervalSeconds = heartbeatIntervalSeconds;
        return this;
    }

    /** Default 30000. */
    public ApexConfig setRequestTimeoutMilliseconds(int requestTimeoutMilliseconds) {
        this.requestTimeoutMilliseconds = requestTimeoutMilliseconds;
        return this;
    }

    /** Minimum level passed to the log listener; default INFO. */
    public ApexConfig setLogLevel(ApexLogLevel logLevel) {
        this.logLevel = logLevel;
        return this;
    }

    public ApexConfig setTelemetrySink(ApexTelemetrySink telemetrySink) {
        this.telemetrySink = telemetrySink;
        return this;
    }

    /** Absolute URL, for ApexTelemetrySink.HTTP. */
    public ApexConfig setTelemetryUrl(String telemetryUrl) {
        this.telemetryUrl = telemetryUrl;
        return this;
    }

    /** While a session is active; default 10. */
    public ApexConfig setTelemetryFlushIntervalSeconds(double telemetryFlushIntervalSeconds) {
        this.telemetryFlushIntervalSeconds = telemetryFlushIntervalSeconds;
        return this;
    }

    /** Default 2. */
    public ApexConfig setTelemetryPoseSamplesPerSecond(double telemetryPoseSamplesPerSecond) {
        this.telemetryPoseSamplesPerSecond = telemetryPoseSamplesPerSecond;
        return this;
    }

    /** Default 500. */
    public ApexConfig setTelemetryMaximumRecordsPerPacket(int telemetryMaximumRecordsPerPacket) {
        this.telemetryMaximumRecordsPerPacket = telemetryMaximumRecordsPerPacket;
        return this;
    }

    public ApexConfig setSessionFormat(ApexSessionFormat sessionFormat) {
        this.sessionFormat = sessionFormat;
        return this;
    }
}
