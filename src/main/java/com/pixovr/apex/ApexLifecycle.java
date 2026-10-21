package com.pixovr.apex;

/** Module version lifecycles, as bit flags for ApexClient.getModulePage (EApexLifecycle). */
public final class ApexLifecycle {
    public static final int RELEASED = 1;
    public static final int DEVELOPMENT = 2;
    public static final int QA = 4;
    public static final int UAT = 8;
    /** Any other lifecycle name, or none. */
    public static final int OTHER = 16;
    public static final int ALL = RELEASED | DEVELOPMENT | QA | UAT | OTHER;

    private ApexLifecycle() {
    }
}
