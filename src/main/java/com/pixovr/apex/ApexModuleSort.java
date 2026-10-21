package com.pixovr.apex;

/** Order of the modules ApexClient.getModulePage pages through (EApexModuleSort). */
public enum ApexModuleSort {
    /** The order the platform returns. */
    PLATFORM(0),
    /** Name A-Z, ignoring case. */
    NAME(1),
    /** Module ID, lowest first. */
    ID(2),
    /** Newest matching version first. */
    NEWEST_VERSION(3);

    private final int code;

    ApexModuleSort(int code) {
        this.code = code;
    }

    /** The value in the C interface (apex-sdk-core/include/apex/Apex.h). */
    public int code() {
        return code;
    }

    /** The constant for a C interface value, or null for one this SDK does not know. */
    public static ApexModuleSort fromCode(int code) {
        for (ApexModuleSort constant : values()) {
            if (constant.code == code) {
                return constant;
            }
        }

        return null;
    }
}
