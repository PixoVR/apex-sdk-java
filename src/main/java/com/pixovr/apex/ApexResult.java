package com.pixovr.apex;

/** One call's outcome. The JSON payload (apex-sdk-core spec/schema) is there on success, null on failure. */
public final class ApexResult {
    private final long callId;
    private final int errorCode;
    private final int httpStatus;
    private final String message;
    private final String json;

    ApexResult(long callId, int errorCode, int httpStatus, String message, String json) {
        this.callId = callId;
        this.errorCode = errorCode;
        this.httpStatus = httpStatus;
        this.message = message != null ? message : "";
        this.json = json;
    }

    public long getCallId() {
        return callId;
    }

    /** Null only for an error code this SDK does not know; see getErrorCodeValue. */
    public ApexErrorCode getError() {
        return ApexErrorCode.fromCode(errorCode);
    }

    public int getErrorCodeValue() {
        return errorCode;
    }

    /** 0 when no HTTP response was received. */
    public int getHttpStatus() {
        return httpStatus;
    }

    /** Never null; empty on success. */
    public String getMessage() {
        return message;
    }

    /** The normalized payload on success, null on failure. */
    public String getJson() {
        return json;
    }

    public boolean isOk() {
        return errorCode == ApexErrorCode.OK.code();
    }

    /** Returns this result, or throws an ApexException when the call failed. */
    public ApexResult throwIfFailed() throws ApexException {
        if (!isOk()) {
            throw new ApexException(this);
        }

        return this;
    }

    @Override
    public String toString() {
        ApexErrorCode error = getError();
        return (error != null ? error.name() : String.valueOf(errorCode)) + " (HTTP " + httpStatus + ")"
            + (message.isEmpty() ? "" : ": " + message);
    }
}
