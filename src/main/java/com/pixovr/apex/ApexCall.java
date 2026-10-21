package com.pixovr.apex;

/** A call in flight. Its result arrives inside ApexClient.poll(); isDone() is true from then on. */
public final class ApexCall {
    private final ApexClient client;
    private final long callId;
    private final ApexClient.ResultCallback callback;
    private ApexResult result;

    ApexCall(ApexClient client, long callId, ApexClient.ResultCallback callback) {
        this.client = client;
        this.callId = callId;
        this.callback = callback;
    }

    public long getCallId() {
        return callId;
    }

    public boolean isDone() {
        return result != null;
    }

    /** Null until the result has arrived. */
    public ApexResult getResult() {
        return result;
    }

    /** Its result then arrives on the next poll with CANCELLED, unless it already finished. */
    public void cancel() {
        client.cancel(this);
    }

    /** Polls the client until the result arrives; see ApexClient.waitFor. */
    public ApexResult await(long timeoutMilliseconds) throws InterruptedException {
        return client.waitFor(this, timeoutMilliseconds);
    }

    void finish(ApexResult finishedResult) {
        result = finishedResult;
        if (callback != null) {
            callback.onResult(finishedResult);
        }
    }
}
