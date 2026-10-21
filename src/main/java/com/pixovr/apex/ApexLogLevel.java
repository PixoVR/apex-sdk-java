package com.pixovr.apex;

/** Log levels (EApexLogLevel). */
public enum ApexLogLevel {
    DEBUG(0),
    INFO(1),
    WARNING(2),
    ERROR(3);

    private final int code;

    ApexLogLevel(int code) {
        this.code = code;
    }

    /** The value in the C interface (apex-sdk-core/include/apex/Apex.h). */
    public int code() {
        return code;
    }

    /** The constant for a C interface value, or null for one this SDK does not know. */
    public static ApexLogLevel fromCode(int code) {
        for (ApexLogLevel constant : values()) {
            if (constant.code == code) {
                return constant;
            }
        }

        return null;
    }
}
