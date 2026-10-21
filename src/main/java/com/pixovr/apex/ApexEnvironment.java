package com.pixovr.apex;

/** Which Apex platform to talk to (EApexEnvironment). */
public enum ApexEnvironment {
    /** https://modules.apex.pixovr.com and https://apex.pixovr.com */
    NA_PRODUCTION(0),
    NA_STAGING(1),
    NA_DEVELOPMENT(2),
    SA_PRODUCTION(3),
    /** http://localhost:8001 and :8000 */
    LOCAL(4),
    /** Hosts come only from ApexConfig.headsetApiUrl and platformApiUrl. */
    CUSTOM(5);

    private final int code;

    ApexEnvironment(int code) {
        this.code = code;
    }

    /** The value in the C interface (apex-sdk-core/include/apex/Apex.h). */
    public int code() {
        return code;
    }

    /** The constant for a C interface value, or null for one this SDK does not know. */
    public static ApexEnvironment fromCode(int code) {
        for (ApexEnvironment constant : values()) {
            if (constant.code == code) {
                return constant;
            }
        }

        return null;
    }
}
