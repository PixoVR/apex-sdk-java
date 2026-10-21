package com.pixovr.apex;

/** How a call ended (EApexError). */
public enum ApexErrorCode {
    OK(0),
    /** No HTTP response: DNS, TLS, timeout, offline. */
    NETWORK(1),
    /** HTTP status outside 2xx with no platform error body. */
    HTTP(2),
    /** The response body was not what the platform should send. */
    PARSE(3),
    /** The platform reported an error; see the message. */
    PLATFORM(4),
    /** The call needs a logged-in user. */
    NOT_LOGGED_IN(5),
    CANCELLED(6),
    INVALID_ARGUMENT(7),
    /** For example, a session event with no active session. */
    INVALID_STATE(8),
    NO_TRANSPORT(9),
    INTERNAL(10);

    private final int code;

    ApexErrorCode(int code) {
        this.code = code;
    }

    /** The value in the C interface (apex-sdk-core/include/apex/Apex.h). */
    public int code() {
        return code;
    }

    /** The constant for a C interface value, or null for one this SDK does not know. */
    public static ApexErrorCode fromCode(int code) {
        for (ApexErrorCode constant : values()) {
            if (constant.code == code) {
                return constant;
            }
        }

        return null;
    }
}
