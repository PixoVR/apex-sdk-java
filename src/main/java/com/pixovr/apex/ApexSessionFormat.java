package com.pixovr.apex;

/** How session calls reach the platform (EApexSessionFormat). */
public enum ApexSessionFormat {
    /** {"uuid", "eventType", "moduleId", "data": base64 of the module's bytes} */
    DATA(0),
    /** xAPI statements, as the Unity SDK 1.x sent them. Session data must be a JSON object. */
    XAPI(1);

    private final int code;

    ApexSessionFormat(int code) {
        this.code = code;
    }

    /** The value in the C interface (apex-sdk-core/include/apex/Apex.h). */
    public int code() {
        return code;
    }

    /** The constant for a C interface value, or null for one this SDK does not know. */
    public static ApexSessionFormat fromCode(int code) {
        for (ApexSessionFormat constant : values()) {
            if (constant.code == code) {
                return constant;
            }
        }

        return null;
    }
}
