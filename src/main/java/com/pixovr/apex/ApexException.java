package com.pixovr.apex;

/** A call that finished with an error. */
public final class ApexException extends Exception {
    private static final long serialVersionUID = 1L;

    private final transient ApexResult result;

    public ApexException(ApexResult result) {
        super(result.toString());
        this.result = result;
    }

    public ApexResult getResult() {
        return result;
    }

    public ApexErrorCode getError() {
        return result.getError();
    }

    public int getHttpStatus() {
        return result.getHttpStatus();
    }
}
