package com.pixovr.apex;

/** Where telemetry packets go (EApexTelemetrySink). */
public enum ApexTelemetrySink {
    /** Records are dropped. */
    OFF(0),
    /** POST each packet to ApexConfig.telemetryUrl with the user's token. */
    HTTP(1),
    /** Hand each packet to the ApexClient.TelemetryListener. */
    CALLBACK(2);

    private final int code;

    ApexTelemetrySink(int code) {
        this.code = code;
    }

    /** The value in the C interface (apex-sdk-core/include/apex/Apex.h). */
    public int code() {
        return code;
    }

    /** The constant for a C interface value, or null for one this SDK does not know. */
    public static ApexTelemetrySink fromCode(int code) {
        for (ApexTelemetrySink constant : values()) {
            if (constant.code == code) {
                return constant;
            }
        }

        return null;
    }
}
